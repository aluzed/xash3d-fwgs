/*
net_thread.c - server socket read on a dedicated thread, early filtering (story 040)
Copyright (C) 2026 aluzed

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// Port of the ReHLDS fork's network receive thread (MT-302, MT-303).
//
// With -netthread on a dedicated server, a thread blocks until the server
// sockets (IPv4 and IPv6) are readable, drains them and queues the packets.
// NET_GetPacket( NS_SERVER ) pops them on the main thread, in arrival order,
// so the rest of the engine is unchanged; fake lag and loss still run on the
// main thread.
//
// Early filtering (net_thread_filter 1) drops on the thread what the main
// thread would ignore anyway, so that floods cost no frame time:
// - connectionless packets from addresses of the IP filter (addip);
// - challenges, connects and queries (A2S, info, ping) above the rate limits
//   the main thread applies (SV_CheckRateLimit), counted with buckets of the
//   thread's own: the main thread's limits still apply after it;
// - game packets from addresses that are not those of a client, the check of
//   SV_ReadPackets (the IP, not the port: a client behind a NAT may change it).
// Other connectionless packets (rcon, master server, the game library's own
// commands through pfnConnectionlessPacket) always reach the main thread.
//
// The main thread publishes the filter list, the client addresses and the
// limits as a copy, every frame when they changed and when a client is given
// a slot (before it is told), so that its first packets are never dropped.

#include "common.h"
#include "net_ws_private.h"
#include "net_thread.h"
#include "server.h"
#include "sys_thread.h"
#include "sv_profile.h"
#if !XASH_WIN32
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#define QUEUE_BYTES      ( 8 * 1024 * 1024 )
#define MIN_QUEUE        64
#define MAX_QUEUE        16384
#define DEFAULT_QUEUE    1024
#define RATE_CACHE_SIZE  8192 // per kind, like SV_CheckRateLimit
#define RATE_BURST       5.0f

typedef enum
{
	DROP_QUEUE_FULL,      // the main thread did not drain the queue in time
	DROP_MALFORMED,       // too short to be a packet the server reads
	DROP_RATE_LIMITED,    // challenge, connect or query above the rate limits
	DROP_BANNED,          // connectionless packet from an address of the IP filter
	DROP_UNKNOWN_ADDRESS, // game packet from an address that is not a client's

	DROP_COUNT
} drop_reason_t;

static const char *const drop_names[DROP_COUNT] =
{
	"queue full",
	"malformed",
	"rate limited",
	"banned",
	"unknown address",
};

typedef enum
{
	KIND_QUERY,
	KIND_CHALLENGE,
	KIND_CONNECT,

	KIND_COUNT,
	KIND_OTHER = KIND_COUNT
} packet_kind_t;

typedef struct
{
	netadr_t adr;
	uint prefixlen;
	uint64_t end_ns; // 0: permanent
} ban_t;

// immutable once published
typedef struct
{
	qboolean filter;
	float query_rate;
	int num_bans;
	ban_t *bans;
	int num_clients;
	netadr_t clients[MAX_CLIENTS + 1]; // + a client being given its slot
} net_thread_config_t;

typedef struct
{
	netadr_t from;
	uint32_t size;
	uint64_t arrival_ns;
} packet_header_t;

static CVAR_DEFINE_AUTO( net_thread_filter, "1", 0, "with -netthread, drop on the network thread the packets the main thread would ignore (bans, rate limits, unknown addresses)" );

static struct
{
	qboolean enabled; // -netthread
	qboolean running;
	int queue_limit;  // packets

	sys_thread_t thread;
	sys_mutex_t lock;
	volatile uint32_t stop;
	int sockets[2];
	int num_sockets;
#if !XASH_WIN32
	int wake_pipe[2];
#endif

	// queue, under the lock
	byte *buffer;
	uint64_t head, tail; // byte positions
	int queued;          // packets
	int max_queued;

	// configuration hand-off, under the lock
	net_thread_config_t *pending;
	uint32_t pending_version;

	// main thread
	int published_filter_version;
	float published_query_rate;
	qboolean published_filter;
	int published_num_clients;
	netadr_t published_clients[MAX_CLIENTS + 1];
	qboolean config_published;
	uint32_t config_version;
	uint32_t reported_drops;

	// network thread
	net_thread_config_t *config;
	uint32_t seen_version;
	struct { uint32_t last_ms; float tokens; } buckets[KIND_COUNT][RATE_CACHE_SIZE];
	uint32_t salt;
	byte scratch[NET_MAX_FRAGMENT];

	// statistics, written by the network thread
	volatile uint32_t received;
	volatile uint32_t dropped[DROP_COUNT];
	volatile uint32_t recv_errors;
} nt;

static uint64_t NetThread_Now( void )
{
	return SV_ProfileNow( );
}

/*
==================
Network thread
==================
*/
static packet_kind_t NetThread_Kind( const byte *data, size_t size )
{
	char word[32];
	size_t i;

	// first word of the command, as SV_ConnectionlessPacket tokenizes it
	for( i = 0; i < sizeof( word ) - 1 && 4 + i < size; i++ )
	{
		char c = data[4 + i];

		if( c == ' ' || c == '\n' || c == '\0' )
			break;
		word[i] = c;
	}
	word[i] = '\0';

	if( size >= 4 + sizeof( A2S_GOLDSRC_INFO ) - 1 && !memcmp( data + 4, A2S_GOLDSRC_INFO, sizeof( A2S_GOLDSRC_INFO ) - 1 ))
		return KIND_QUERY;
	if( word[0] == A2S_GOLDSRC_PLAYERS || word[0] == A2S_GOLDSRC_RULES )
		return KIND_QUERY;
	if( !Q_strcmp( word, A2A_INFO ) || !Q_strcmp( word, A2A_PING ) || !Q_strcmp( word, A2A_GOLDSRC_PING ))
		return KIND_QUERY;
	if( !Q_strcmp( word, C2S_GETCHALLENGE ))
		return KIND_CHALLENGE;
	if( !Q_strcmp( word, C2S_CONNECT ))
		return KIND_CONNECT;

	return KIND_OTHER;
}

static qboolean NetThread_RateLimited( const netadr_t *from, packet_kind_t kind, uint64_t now_ns )
{
	const float rate = kind == KIND_QUERY ? nt.config->query_rate : 1.0f;
	netadrtype_t type = NET_NetadrType( from );
	uint32_t hash = 2166136261u ^ nt.salt;
	byte bytes[16];
	size_t len, i;

	if( rate <= 0.0f )
		return false;

	if( type == NA_IP )
	{
		memcpy( bytes, from->ip, 4 );
		len = 4;
	}
	else if( type == NA_IP6 )
	{
		NET_NetadrToIP6Bytes( bytes, from );
		len = 16;
	}
	else return false;

	for( i = 0; i < len; i++ )
		hash = ( hash ^ bytes[i] ) * 16777619u;

	// the token bucket of SV_CheckRateLimit
	uint32_t now = (uint32_t)( now_ns / 1000000 );
	int slot = hash & ( RATE_CACHE_SIZE - 1 );
	uint32_t elapsed = now - nt.buckets[kind][slot].last_ms;
	float tokens = nt.buckets[kind][slot].tokens + elapsed * 0.001f * rate;

	if( tokens > RATE_BURST )
		tokens = RATE_BURST;
	nt.buckets[kind][slot].last_ms = now;

	if( tokens < 1.0f )
	{
		nt.buckets[kind][slot].tokens = tokens;
		return true;
	}

	nt.buckets[kind][slot].tokens = tokens - 1.0f;
	return false;
}

static qboolean NetThread_IsBanned( const netadr_t *from, uint64_t now_ns )
{
	int i;

	for( i = 0; i < nt.config->num_bans; i++ )
	{
		const ban_t *ban = &nt.config->bans[i];

		if( ban->end_ns && now_ns > ban->end_ns )
			continue;

		if( NET_CompareAdrByMask( *from, ban->adr, ban->prefixlen ))
			return true;
	}

	return false;
}

static qboolean NetThread_IsClientAddress( const netadr_t *from )
{
	int i;

	for( i = 0; i < nt.config->num_clients; i++ )
	{
		if( NET_CompareBaseAdr( *from, nt.config->clients[i] ))
			return true;
	}

	return false;
}

// -1 to queue the packet, or the reason for dropping it
static int NetThread_Classify( const byte *data, size_t size, const netadr_t *from, uint64_t now_ns )
{
	if( size < 4 )
		return DROP_MALFORMED;

	if( !nt.config || !nt.config->filter )
		return -1;

	if( *(const int *)data == -1 )
	{
		packet_kind_t kind;

		if( NetThread_IsBanned( from, now_ns ))
			return DROP_BANNED;

		kind = NetThread_Kind( data, size );
		if( kind != KIND_OTHER && NetThread_RateLimited( from, kind, now_ns ))
			return DROP_RATE_LIMITED;

		return -1;
	}

	if( !NetThread_IsClientAddress( from ))
		return DROP_UNKNOWN_ADDRESS;

	return -1;
}

static void NetThread_Drop( drop_reason_t reason )
{
	Sys_AtomicAdd( &nt.dropped[reason], 1 );
}

static void NetThread_CopyIn( uint64_t pos, const void *data, uint32_t size )
{
	uint32_t index = (uint32_t)( pos % QUEUE_BYTES );
	uint32_t first = Q_min( size, QUEUE_BYTES - index );

	memcpy( nt.buffer + index, data, first );
	memcpy( nt.buffer, (const byte *)data + first, size - first );
}

static void NetThread_CopyOut( uint64_t pos, void *data, uint32_t size )
{
	uint32_t index = (uint32_t)( pos % QUEUE_BYTES );
	uint32_t first = Q_min( size, QUEUE_BYTES - index );

	memcpy( data, nt.buffer + index, first );
	memcpy( (byte *)data + first, nt.buffer, size - first );
}

static void NetThread_Push( const netadr_t *from, const byte *data, uint32_t size, uint64_t now_ns )
{
	packet_header_t header;
	uint32_t total = sizeof( header ) + size;

	header.from = *from;
	header.size = size;
	header.arrival_ns = now_ns;

	Sys_MutexLock( &nt.lock );

	if( nt.queued >= nt.queue_limit || QUEUE_BYTES - (uint32_t)( nt.head - nt.tail ) < total )
	{
		Sys_MutexUnlock( &nt.lock );
		NetThread_Drop( DROP_QUEUE_FULL );
		return;
	}

	NetThread_CopyIn( nt.head, &header, sizeof( header ));
	NetThread_CopyIn( nt.head + sizeof( header ), data, size );
	nt.head += total;
	nt.queued++;
	if( nt.queued > nt.max_queued )
		nt.max_queued = nt.queued;

	Sys_MutexUnlock( &nt.lock );
}

static void NetThread_TakeConfig( void )
{
	net_thread_config_t *old = NULL;

	Sys_MutexLock( &nt.lock );
	if( nt.pending_version != nt.seen_version )
	{
		old = nt.config;
		nt.config = nt.pending;
		nt.pending = NULL;
		nt.seen_version = nt.pending_version;
	}
	Sys_MutexUnlock( &nt.lock );

	if( old )
	{
		free( old->bans );
		free( old );
	}
}

static void NetThread_Drain( int sock )
{
	for( ;; )
	{
		struct sockaddr_storage addr = { 0 };
		WSAsize_t addr_len = sizeof( addr );
		netadr_t from;
		uint64_t now;
		int ret, reason;

		ret = recvfrom( sock, (char *)nt.scratch, sizeof( nt.scratch ), 0, (struct sockaddr *)&addr, &addr_len );

		if( NET_IsSocketError( ret ))
		{
			int err = WSAGetLastError( );

			if( err != WSAEWOULDBLOCK )
			{
				// connection reset by an ICMP reply and the like: keep reading
				if( err != WSAECONNRESET && err != WSAECONNREFUSED && err != WSAEMSGSIZE && err != WSAETIMEDOUT )
					Sys_AtomicAdd( &nt.recv_errors, 1 );
				if( err == WSAECONNRESET || err == WSAECONNREFUSED || err == WSAEMSGSIZE )
					continue;
			}
			return;
		}

		Sys_AtomicAdd( &nt.received, 1 );
		now = NetThread_Now( );
		NET_SockadrToNetadr( &addr, &from );

		if( ret >= NET_MAX_FRAGMENT )
		{
			NetThread_Drop( DROP_MALFORMED );
			continue;
		}

		reason = NetThread_Classify( nt.scratch, ret, &from, now );
		if( reason >= 0 )
		{
			NetThread_Drop( reason );
			continue;
		}

		NetThread_Push( &from, nt.scratch, ret, now );
	}
}

static void NetThread_Run( void *unused )
{
	while( !Sys_AtomicLoad( &nt.stop ))
	{
		int i;

		NetThread_TakeConfig( );

#if XASH_WIN32
		fd_set readable;
		struct timeval timeout = { 0, 50000 }; // Stop waits for it

		FD_ZERO( &readable );
		for( i = 0; i < nt.num_sockets; i++ )
			FD_SET( nt.sockets[i], &readable );

		if( select( 0, &readable, NULL, NULL, &timeout ) <= 0 )
			continue;

		for( i = 0; i < nt.num_sockets; i++ )
		{
			if( FD_ISSET( nt.sockets[i], &readable ))
				NetThread_Drain( nt.sockets[i] );
		}
#else
		struct pollfd fds[3];

		for( i = 0; i < nt.num_sockets; i++ )
		{
			fds[i].fd = nt.sockets[i];
			fds[i].events = POLLIN;
			fds[i].revents = 0;
		}
		fds[i].fd = nt.wake_pipe[0];
		fds[i].events = POLLIN;
		fds[i].revents = 0;

		if( poll( fds, nt.num_sockets + 1, 1000 ) <= 0 )
			continue;

		for( i = 0; i < nt.num_sockets; i++ )
		{
			if( fds[i].revents )
				NetThread_Drain( nt.sockets[i] );
		}
#endif
	}
}

/*
==================
Main thread
==================
*/
static void NetThread_Stats_f( void )
{
	int i;

	if( !nt.running )
	{
		Con_Printf( "Network thread: not running (%s)\n", nt.enabled ? "no server socket" : "start the server with -netthread" );
		return;
	}

	Sys_MutexLock( &nt.lock );
	int queued = nt.queued, max_queued = nt.max_queued;
	Sys_MutexUnlock( &nt.lock );

	Con_Printf( "Network thread: running, queue %d/%d packets (max %d), filter %s\n",
		queued, nt.queue_limit, max_queued, net_thread_filter.value ? "on" : "off" );
	Con_Printf( "  received      %u\n", Sys_AtomicLoad( &nt.received ));
	for( i = 0; i < DROP_COUNT; i++ )
		Con_Printf( "  dropped: %-15s %u\n", drop_names[i], Sys_AtomicLoad( &nt.dropped[i] ));
	Con_Printf( "  recv errors   %u\n", Sys_AtomicLoad( &nt.recv_errors ));
}

void NetThread_Init( void )
{
	int size;

	Cvar_RegisterVariable( &net_thread_filter );
	Cmd_AddCommand( "net_thread_stats", NetThread_Stats_f, "network thread counters" );

	// -nonetthread wins, so that a command line can turn it off again
	nt.enabled = Sys_CheckParm( "-netthread" ) && !Sys_CheckParm( "-nonetthread" ) && Host_IsDedicated( );
	nt.queue_limit = DEFAULT_QUEUE;
	if( Sys_GetIntFromCmdLine( "-netthread_queue", &size ))
		nt.queue_limit = bound( MIN_QUEUE, size, MAX_QUEUE );
}

qboolean NetThread_IsRunning( void )
{
	return nt.running;
}

static void NetThread_AddBan( const netadr_t *adr, uint prefixlen, double end_time, void *ctx )
{
	net_thread_config_t *config = ctx;
	ban_t *ban = &config->bans[config->num_bans++];
	uint64_t now = NetThread_Now( );

	ban->adr = *adr;
	ban->prefixlen = prefixlen;
	ban->end_ns = 0;
	if( end_time )
		ban->end_ns = end_time > host.realtime ? now + (uint64_t)(( end_time - host.realtime ) * 1e9 ) : now;
}

static void NetThread_CountBan( const netadr_t *adr, uint prefixlen, double end_time, void *ctx )
{
	( *(int *)ctx )++;
}

static int NetThread_ClientAddresses( netadr_t *out )
{
	int i, count = 0;

	if( !svs.clients )
		return 0;

	for( i = 0; i < svs.maxclients; i++ )
	{
		const sv_client_t *cl = &svs.clients[i];

		if( cl->state == cs_free || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		out[count++] = cl->netchan.remote_address;
	}

	return count;
}

static void NetThread_Publish( qboolean force, const netadr_t *connecting )
{
	netadr_t clients[MAX_CLIENTS + 1];
	int num_clients = NetThread_ClientAddresses( clients );
	int filter_version = SV_IPFilterVersion( );
	qboolean filter = net_thread_filter.value != 0.0f;
	float query_rate = sv_query_rate_limit.value;
	net_thread_config_t *config, *old;
	int num_bans = 0;

	if( connecting )
		clients[num_clients++] = *connecting;

	if( !force && nt.config_published
		&& filter == nt.published_filter
		&& query_rate == nt.published_query_rate
		&& filter_version == nt.published_filter_version
		&& num_clients == nt.published_num_clients
		&& !memcmp( clients, nt.published_clients, sizeof( clients[0] ) * num_clients ))
		return;

	SV_IPFilterList( NetThread_CountBan, &num_bans );

	config = calloc( 1, sizeof( *config ));
	config->filter = filter;
	config->query_rate = query_rate;
	config->bans = calloc( num_bans ? num_bans : 1, sizeof( *config->bans ));
	SV_IPFilterList( NetThread_AddBan, config );
	config->num_clients = num_clients;
	memcpy( config->clients, clients, sizeof( clients[0] ) * num_clients );

	Sys_MutexLock( &nt.lock );
	old = nt.pending;
	nt.pending = config;
	nt.pending_version++;
	Sys_MutexUnlock( &nt.lock );

	if( old )
	{
		free( old->bans );
		free( old );
	}

	nt.config_published = true;
	nt.published_filter = filter;
	nt.published_query_rate = query_rate;
	nt.published_filter_version = filter_version;
	nt.published_num_clients = num_clients;
	memcpy( nt.published_clients, clients, sizeof( clients[0] ) * num_clients );
}

void NetThread_BeginFrame( void )
{
	uint32_t drops = 0;
	int i;

	if( !nt.running )
		return;

	NetThread_Publish( false, NULL );

	for( i = 0; i < DROP_COUNT; i++ )
		drops += Sys_AtomicLoad( &nt.dropped[i] );

	SV_ProfileCount( SVC_NET_THREAD_DROPS, drops - nt.reported_drops );
	nt.reported_drops = drops;
}

void NetThread_ClientConnecting( const netadr_t *adr )
{
	if( nt.running )
		NetThread_Publish( false, adr );
}

void NetThread_Start( int sock4, int sock6 )
{
	if( !nt.enabled || nt.running )
		return;

	nt.num_sockets = 0;
	if( NET_IsSocketValid( sock4 ))
		nt.sockets[nt.num_sockets++] = sock4;
	if( NET_IsSocketValid( sock6 ))
		nt.sockets[nt.num_sockets++] = sock6;
	if( !nt.num_sockets )
		return;

	nt.buffer = malloc( QUEUE_BYTES );
	if( !nt.buffer )
		return;

#if !XASH_WIN32
	if( pipe( nt.wake_pipe ) < 0 )
	{
		free( nt.buffer );
		nt.buffer = NULL;
		return;
	}
#endif

	Sys_MutexInit( &nt.lock );
	nt.head = nt.tail = 0;
	nt.queued = nt.max_queued = 0;
	Sys_AtomicStore( &nt.stop, 0 );
	nt.salt = (uint32_t)NetThread_Now( );
	nt.config_published = false;
	nt.config = NULL;
	nt.seen_version = nt.pending_version;
	nt.running = true; // the first configuration is published before the thread runs
	NetThread_Publish( true, NULL );

	if( !Sys_ThreadStart( &nt.thread, NetThread_Run, NULL, "xash-net-recv" ))
	{
		Con_Printf( S_ERROR "-netthread: can't start the network thread, reading the socket on the main thread\n" );
		nt.running = false;
		return;
	}

	Con_Printf( "Network thread started, queue of %d packets\n", nt.queue_limit );
}

void NetThread_Stop( void )
{
	if( !nt.running )
		return;

	Sys_AtomicStore( &nt.stop, 1 );
#if !XASH_WIN32
	if( write( nt.wake_pipe[1], "", 1 ) < 0 )
	{
		// poll times out within a second anyway
	}
#endif
	Sys_ThreadJoin( nt.thread );
	nt.running = false;

#if !XASH_WIN32
	close( nt.wake_pipe[0] );
	close( nt.wake_pipe[1] );
#endif

	if( nt.config )
	{
		free( nt.config->bans );
		free( nt.config );
		nt.config = NULL;
	}
	if( nt.pending )
	{
		free( nt.pending->bans );
		free( nt.pending );
		nt.pending = NULL;
	}

	Sys_MutexFree( &nt.lock );
	free( nt.buffer );
	nt.buffer = NULL;
}

qboolean NetThread_Pop( netadr_t *from, byte *data, size_t *length )
{
	packet_header_t header;

	Sys_MutexLock( &nt.lock );

	if( !nt.queued )
	{
		Sys_MutexUnlock( &nt.lock );
		return false;
	}

	NetThread_CopyOut( nt.tail, &header, sizeof( header ));
	NetThread_CopyOut( nt.tail + sizeof( header ), data, header.size );
	nt.tail += sizeof( header ) + header.size;
	nt.queued--;

	Sys_MutexUnlock( &nt.lock );

	*from = header.from;
	*length = header.size;
	SV_ProfileCount( SVC_NET_QUEUE_WAIT_US, (int64_t)(( NetThread_Now( ) - header.arrival_ns ) / 1000 ));
	return true;
}
