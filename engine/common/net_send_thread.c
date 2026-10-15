/*
net_send_thread.c - server packets sent from a dedicated thread (story 041)
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

// Port of the ReHLDS fork's send thread (MT-304, MT-105).
//
// With -netsend on a dedicated server, NET_SendPacketEx( NS_SERVER ) queues
// the packet instead of waiting for the kernel; a thread sends the queue in
// order, a frame's packets in one sendmmsg call on Linux. Long packets are
// split by the thread (NET_SendSplit) with the sequence number the main thread
// gave them, and the 100 µs pause between their parts no longer stalls the
// frame. Errors are counted, not printed.
//
// When the queue is full the main thread waits for room: packets are never
// dropped or reordered.

#include "common.h"
#include "net_ws_private.h"
#include "netchan.h"
#include "net_send_thread.h"
#include "sys_thread.h"
#include "sv_profile.h"

#define QUEUE_BYTES    ( 8 * 1024 * 1024 )
#define MIN_QUEUE      64
#define MAX_QUEUE      16384
#define DEFAULT_QUEUE  1024
#define MAX_BATCH      64

typedef struct
{
	struct sockaddr_storage to;
	uint32_t to_len;
	int      socket;
	uint32_t size;
	uint32_t split_size; // 0: not split
	int      split_sequence;
} send_header_t;

static struct
{
	qboolean enabled;
	qboolean running;
	int queue_limit;

	sys_thread_t thread;
	sys_mutex_t lock;
	sys_cond_t cond;
	qboolean stop;

	byte *buffer;
	uint64_t head, tail;
	int queued;

	// statistics, under the lock
	uint32_t pushed;
	uint32_t sent;
	uint32_t syscalls;
	uint32_t largest_batch;
	uint32_t wakeups;
	uint32_t full_waits;
	uint64_t full_wait_us;
	uint32_t errors;
} ns;

static void NetSend_CopyIn( uint64_t pos, const void *data, uint32_t size )
{
	uint32_t index = (uint32_t)( pos % QUEUE_BYTES );
	uint32_t first = Q_min( size, QUEUE_BYTES - index );

	memcpy( ns.buffer + index, data, first );
	memcpy( ns.buffer, (const byte *)data + first, size - first );
}

static void NetSend_CopyOut( uint64_t pos, void *data, uint32_t size )
{
	uint32_t index = (uint32_t)( pos % QUEUE_BYTES );
	uint32_t first = Q_min( size, QUEUE_BYTES - index );

	memcpy( data, ns.buffer + index, first );
	memcpy( (byte *)data + first, ns.buffer, size - first );
}

/*
==================
Send thread
==================
*/
typedef struct
{
	send_header_t header;
	byte data[NET_MAX_FRAGMENT];
} send_item_t;

static send_item_t *batch; // MAX_BATCH items, allocated when the thread starts

static void NetSend_SendBatch( int count, uint32_t *errors, uint32_t *syscalls )
{
	int i = 0;

	while( i < count )
	{
		const send_header_t *h = &batch[i].header;

		if( h->split_size )
		{
			if( NET_SendSplit( h->socket, (const char *)batch[i].data, h->size, 0, &h->to, h->to_len, h->split_size, h->split_sequence, false ) < 0 )
				( *errors )++;
			( *syscalls )++;
			i++;
			continue;
		}

#if XASH_LINUX
		{
			// consecutive unsplit packets on the same socket in one call
			struct mmsghdr msgs[MAX_BATCH];
			struct iovec iov[MAX_BATCH];
			int n = 0, sent;

			while( i + n < count && !batch[i + n].header.split_size && batch[i + n].header.socket == h->socket )
			{
				send_item_t *item = &batch[i + n];

				iov[n].iov_base = item->data;
				iov[n].iov_len = item->header.size;
				memset( &msgs[n], 0, sizeof( msgs[n] ));
				msgs[n].msg_hdr.msg_name = &item->header.to;
				msgs[n].msg_hdr.msg_namelen = item->header.to_len;
				msgs[n].msg_hdr.msg_iov = &iov[n];
				msgs[n].msg_hdr.msg_iovlen = 1;
				n++;
			}

			for( int done = 0; done < n; )
			{
				sent = sendmmsg( h->socket, msgs + done, n - done, 0 );
				( *syscalls )++;
				if( sent <= 0 )
				{
					// skip the packet the kernel refused, keep the order of the rest
					( *errors )++;
					done++;
					continue;
				}
				done += sent;
			}

			i += n;
		}
#else
		if( sendto( h->socket, (const char *)batch[i].data, h->size, 0, (const struct sockaddr *)&h->to, h->to_len ) < 0 )
			( *errors )++;
		( *syscalls )++;
		i++;
#endif
	}
}

static void NetSend_Run( void *unused )
{
	Sys_MutexLock( &ns.lock );

	for( ;; )
	{
		uint32_t errors = 0, syscalls = 0;
		int count = 0;

		while( !ns.queued && !ns.stop )
			Sys_CondWait( &ns.cond, &ns.lock );

		if( !ns.queued && ns.stop )
			break;

		ns.wakeups++;

		// take up to MAX_BATCH packets; the main thread may queue more meanwhile
		while( ns.queued && count < MAX_BATCH )
		{
			send_item_t *item = &batch[count++];

			NetSend_CopyOut( ns.tail, &item->header, sizeof( item->header ));
			NetSend_CopyOut( ns.tail + sizeof( item->header ), item->data, item->header.size );
			ns.tail += sizeof( item->header ) + item->header.size;
			ns.queued--;
		}

		Sys_CondBroadcast( &ns.cond ); // room for a waiting main thread
		Sys_MutexUnlock( &ns.lock );

		NetSend_SendBatch( count, &errors, &syscalls );

		Sys_MutexLock( &ns.lock );
		ns.sent += count;
		ns.syscalls += syscalls;
		ns.errors += errors;
		if( (uint32_t)count > ns.largest_batch )
			ns.largest_batch = count;
	}

	Sys_MutexUnlock( &ns.lock );
}

/*
==================
Main thread
==================
*/
static void NetSend_Stats_f( void )
{
	if( !ns.running )
	{
		Con_Printf( "Network send thread: not running (%s)\n", ns.enabled ? "no server socket" : "start the server with -netsend" );
		return;
	}

	Sys_MutexLock( &ns.lock );
	Con_Printf( "Network send thread: running, queue %d/%d packets\n", ns.queued, ns.queue_limit );
	Con_Printf( "  queued        %u\n", ns.pushed );
	Con_Printf( "  sent          %u in %u system calls (%.1f per call), largest batch %u\n",
		ns.sent, ns.syscalls, ns.syscalls ? (double)ns.sent / ns.syscalls : 0.0, ns.largest_batch );
	Con_Printf( "  wake-ups      %u\n", ns.wakeups );
	Con_Printf( "  queue full    %u times, main thread waited %.1f ms in all\n", ns.full_waits, ns.full_wait_us / 1000.0 );
	Con_Printf( "  send errors   %u\n", ns.errors );
	Sys_MutexUnlock( &ns.lock );
}

void NetSend_Init( void )
{
	int size;

	Cmd_AddCommand( "net_send_stats", NetSend_Stats_f, "network send thread counters" );

	// -nonetsend wins, so that a command line can turn it off again
	ns.enabled = Sys_CheckParm( "-netsend" ) && !Sys_CheckParm( "-nonetsend" ) && Host_IsDedicated( );
	ns.queue_limit = DEFAULT_QUEUE;
	if( Sys_GetIntFromCmdLine( "-netsend_queue", &size ))
		ns.queue_limit = bound( MIN_QUEUE, size, MAX_QUEUE );
}

qboolean NetSend_IsRunning( void )
{
	return ns.running;
}

void NetSend_Start( void )
{
	if( !ns.enabled || ns.running )
		return;

	ns.buffer = malloc( QUEUE_BYTES );
	batch = malloc( sizeof( *batch ) * MAX_BATCH );
	if( !ns.buffer || !batch )
	{
		free( ns.buffer );
		free( batch );
		ns.buffer = NULL;
		batch = NULL;
		return;
	}

	Sys_MutexInit( &ns.lock );
	Sys_CondInit( &ns.cond );
	ns.head = ns.tail = 0;
	ns.queued = 0;
	ns.stop = false;

	if( !Sys_ThreadStart( &ns.thread, NetSend_Run, NULL, "xash-net-send" ))
	{
		Con_Printf( S_ERROR "-netsend: can't start the send thread, sending on the main thread\n" );
		Sys_CondFree( &ns.cond );
		Sys_MutexFree( &ns.lock );
		free( ns.buffer );
		free( batch );
		ns.buffer = NULL;
		batch = NULL;
		return;
	}

	ns.running = true;
	Con_Printf( "Network send thread started, queue of %d packets\n", ns.queue_limit );
}

void NetSend_Stop( void )
{
	if( !ns.running )
		return;

	// what is queued still leaves, before the sockets are closed
	Sys_MutexLock( &ns.lock );
	ns.stop = true;
	Sys_CondBroadcast( &ns.cond );
	Sys_MutexUnlock( &ns.lock );

	Sys_ThreadJoin( ns.thread );
	ns.running = false;

	Sys_CondFree( &ns.cond );
	Sys_MutexFree( &ns.lock );
	free( ns.buffer );
	free( batch );
	ns.buffer = NULL;
	batch = NULL;
}

void NetSend_Push( int net_socket, const struct sockaddr_storage *to, size_t to_len, const void *data, size_t length, size_t split_size, int split_sequence )
{
	send_header_t header;
	uint32_t total = sizeof( header ) + (uint32_t)length;

	memset( &header, 0, sizeof( header ));
	memcpy( &header.to, to, to_len );
	header.to_len = (uint32_t)to_len;
	header.socket = net_socket;
	header.size = (uint32_t)length;
	header.split_size = (uint32_t)split_size;
	header.split_sequence = split_sequence;

	Sys_MutexLock( &ns.lock );

	if( ns.queued >= ns.queue_limit || QUEUE_BYTES - (uint32_t)( ns.head - ns.tail ) < total )
	{
		uint64_t start = SV_ProfileNow( );

		ns.full_waits++;
		while( ns.queued >= ns.queue_limit || QUEUE_BYTES - (uint32_t)( ns.head - ns.tail ) < total )
			Sys_CondWait( &ns.cond, &ns.lock );
		ns.full_wait_us += ( SV_ProfileNow( ) - start ) / 1000;
	}

	NetSend_CopyIn( ns.head, &header, sizeof( header ));
	NetSend_CopyIn( ns.head + sizeof( header ), data, (uint32_t)length );
	ns.head += total;
	ns.queued++;
	ns.pushed++;

	// one wake-up per batch: the thread takes everything queued when it runs
	if( ns.queued == 1 )
		Sys_CondSignal( &ns.cond );

	Sys_MutexUnlock( &ns.lock );
}
