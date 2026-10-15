/*
net_replay.c - byte-exact record and replay of a server session (story 037)
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

// Port of the ReHLDS fork's replay (MT-004). Inputs and sends are binary
// files written with stdio, from the main thread only.

#include "common.h"
#include "net_replay.h"
#include "server.h"
#include <stdio.h>

#define INPUTS_MAGIC "XSREPLY1"
#define SENDS_MAGIC  "RHSENDS1" // same format as ReHLDS: its compare tool reads both

typedef enum
{
	EV_FRAME = 1,   // double realtime, uint32 fingerprint
	EV_PACKET,      // netadr_t from, uint32 length, data
	EV_NO_PACKET,
	EV_CONSOLE,     // uint32 length, text
	EV_VALUE,       // int64
	EV_VALUE_FLOAT, // double
} replay_event_t;

typedef enum
{
	MODE_OFF,
	MODE_RECORD,
	MODE_REPLAY,
} replay_mode_t;

static struct
{
	replay_mode_t mode;
	FILE *inputs;
	FILE *sends;
	int next_event;   // replay: type of the event read ahead, 0 when none
	qboolean finished;
	uint32_t frame;   // accepted frames so far
	uint32_t events;
	uint32_t sent;
	qboolean state_diverged;
	char console[MAX_PRINT_MSG];
} replay;

static const char *NetReplay_EventName( int type )
{
	switch( type )
	{
	case EV_FRAME: return "frame";
	case EV_PACKET: return "packet";
	case EV_NO_PACKET: return "no packet";
	case EV_CONSOLE: return "console";
	case EV_VALUE: return "value";
	case EV_VALUE_FLOAT: return "float value";
	case -1: return "end of recording";
	}
	return "unknown";
}

// Fingerprint of the simulation at the start of a frame: the entities'
// positions. A replay reports the first frame where it differs from the
// recording, which locates a missing input.
static uint32_t NetReplay_Fingerprint( void )
{
	uint32_t hash = 2166136261u;
	int i;

	if( !SV_Active( ) || !svgame.edicts )
		return 0;

	for( i = 0; i < svgame.numEntities; i++ )
	{
		const edict_t *e = &svgame.edicts[i];
		const byte *origin = (const byte *)e->v.origin;
		size_t j;

		if( e->free )
			continue;

		for( j = 0; j < sizeof( e->v.origin ); j++ )
			hash = ( hash ^ origin[j] ) * 16777619u;
	}

	return hash;
}

static void NetReplay_Write( const void *data, size_t size )
{
	if( fwrite( data, 1, size, replay.inputs ) != size )
		Sys_Error( "-netrecord: write failed\n" );
}

static void NetReplay_WriteEvent( int type )
{
	byte b = (byte)type;

	NetReplay_Write( &b, 1 );
	replay.events++;
}

static qboolean NetReplay_Read( void *data, size_t size )
{
	return fread( data, 1, size, replay.inputs ) == size;
}

static void NetReplay_Finish( const char *reason )
{
	if( replay.finished )
		return;

	replay.finished = true;
	Con_Printf( "-netreplay: %s after %u frames, %u events, %u packets sent\n", reason, replay.frame, replay.events, replay.sent );

	if( replay.sends )
		fflush( replay.sends );

	Sys_Quit( "replay finished" );
}

static int NetReplay_PeekEvent( void )
{
	byte b;

	if( replay.next_event )
		return replay.next_event;

	if( !NetReplay_Read( &b, 1 ))
		return -1;

	replay.next_event = b;
	return replay.next_event;
}

static void NetReplay_Diverged( int expected, int found )
{
	Con_Printf( S_ERROR "-netreplay: the engine asked for a %s, the recording has a %s (frame %u, event %u)\n",
		NetReplay_EventName( expected ), NetReplay_EventName( found ), replay.frame, replay.events );
	NetReplay_Finish( "diverged" );
}

static qboolean NetReplay_ExpectEvent( int type )
{
	int found = NetReplay_PeekEvent( );

	if( found != type )
	{
		if( found == -1 )
			NetReplay_Finish( "end of recording" );
		else NetReplay_Diverged( type, found );
		return false;
	}

	replay.next_event = 0;
	replay.events++;
	return true;
}

void NetReplay_Init( void )
{
	string path;

	if( Sys_GetParmFromCmdLine( "-netrecord", path ))
	{
		replay.inputs = fopen( path, "wb" );
		if( !replay.inputs )
			Sys_Error( "-netrecord: can't create %s\n", path );

		NetReplay_Write( INPUTS_MAGIC, 8 );
		replay.mode = MODE_RECORD;
	}
	else if( Sys_GetParmFromCmdLine( "-netreplay", path ))
	{
		char magic[8];

		replay.inputs = fopen( path, "rb" );
		if( !replay.inputs )
			Sys_Error( "-netreplay: can't open %s\n", path );

		if( !NetReplay_Read( magic, 8 ) || memcmp( magic, INPUTS_MAGIC, 8 ))
			Sys_Error( "-netreplay: %s is not a recording\n", path );

		replay.mode = MODE_REPLAY;
	}

	if( replay.mode != MODE_OFF && Sys_GetParmFromCmdLine( "-netsends", path ))
	{
		replay.sends = fopen( path, "wb" );
		if( !replay.sends )
			Sys_Error( "-netsends: can't create %s\n", path );
		fwrite( SENDS_MAGIC, 1, 8, replay.sends );
	}
}

void NetReplay_Shutdown( void )
{
	if( replay.inputs )
		fclose( replay.inputs );
	if( replay.sends )
		fclose( replay.sends );

	replay.inputs = replay.sends = NULL;
	replay.mode = MODE_OFF;
}

qboolean NetReplay_IsRecording( void )
{
	return replay.mode == MODE_RECORD;
}

qboolean NetReplay_IsReplaying( void )
{
	return replay.mode == MODE_REPLAY && !replay.finished;
}

void NetReplay_RecordFrame( double realtime )
{
	uint32_t fingerprint;

	if( replay.mode != MODE_RECORD )
		return;

	fingerprint = NetReplay_Fingerprint( );
	NetReplay_WriteEvent( EV_FRAME );
	NetReplay_Write( &realtime, sizeof( realtime ));
	NetReplay_Write( &fingerprint, sizeof( fingerprint ));
	replay.frame++;
}

double NetReplay_ReplayFrame( void )
{
	double realtime = 0.0;
	uint32_t fingerprint = 0;

	if( !NetReplay_ExpectEvent( EV_FRAME ) || !NetReplay_Read( &realtime, sizeof( realtime )) || !NetReplay_Read( &fingerprint, sizeof( fingerprint )))
	{
		NetReplay_Finish( "end of recording" );
		return realtime;
	}

	if( !replay.state_diverged && fingerprint != NetReplay_Fingerprint( ))
	{
		replay.state_diverged = true;
		Con_Printf( S_WARN "-netreplay: the entities differ from the recording from frame %u\n", replay.frame );
	}

	replay.frame++;
	return realtime;
}

void NetReplay_RecordPacket( qboolean received, const netadr_t *from, const byte *data, size_t length )
{
	uint32_t len = (uint32_t)length;

	if( replay.mode != MODE_RECORD )
		return;

	if( !received )
	{
		NetReplay_WriteEvent( EV_NO_PACKET );
		return;
	}

	NetReplay_WriteEvent( EV_PACKET );
	NetReplay_Write( from, sizeof( *from ));
	NetReplay_Write( &len, sizeof( len ));
	NetReplay_Write( data, len );
}

qboolean NetReplay_ReplayPacket( netadr_t *from, byte *data, size_t *length )
{
	uint32_t len;
	int type = NetReplay_PeekEvent( );

	*length = 0;

	if( type == EV_NO_PACKET )
	{
		NetReplay_ExpectEvent( EV_NO_PACKET );
		return false;
	}

	if( !NetReplay_ExpectEvent( EV_PACKET ))
		return false;

	if( !NetReplay_Read( from, sizeof( *from )) || !NetReplay_Read( &len, sizeof( len )) || len > NET_MAX_FRAGMENT || !NetReplay_Read( data, len ))
	{
		NetReplay_Finish( "truncated recording" );
		return false;
	}

	*length = len;
	return true;
}

void NetReplay_RecordConsole( const char *text )
{
	uint32_t len;

	if( replay.mode != MODE_RECORD || !text )
		return;

	len = Q_strlen( text );
	NetReplay_WriteEvent( EV_CONSOLE );
	NetReplay_Write( &len, sizeof( len ));
	NetReplay_Write( text, len );
}

const char *NetReplay_ReplayConsole( void )
{
	uint32_t len;

	if( NetReplay_PeekEvent( ) != EV_CONSOLE )
		return NULL;

	NetReplay_ExpectEvent( EV_CONSOLE );
	if( !NetReplay_Read( &len, sizeof( len )) || len >= sizeof( replay.console ) || !NetReplay_Read( replay.console, len ))
	{
		NetReplay_Finish( "truncated recording" );
		return NULL;
	}

	replay.console[len] = '\0';
	return replay.console;
}

int64_t NetReplay_Value( int64_t live )
{
	if( replay.mode == MODE_RECORD )
	{
		NetReplay_WriteEvent( EV_VALUE );
		NetReplay_Write( &live, sizeof( live ));
	}
	else if( NetReplay_IsReplaying( ))
	{
		int64_t value;

		if( NetReplay_ExpectEvent( EV_VALUE ) && NetReplay_Read( &value, sizeof( value )))
			return value;
	}

	return live;
}

double NetReplay_ValueF( double live )
{
	if( replay.mode == MODE_RECORD )
	{
		NetReplay_WriteEvent( EV_VALUE_FLOAT );
		NetReplay_Write( &live, sizeof( live ));
	}
	else if( NetReplay_IsReplaying( ))
	{
		double value;

		if( NetReplay_ExpectEvent( EV_VALUE_FLOAT ) && NetReplay_Read( &value, sizeof( value )))
			return value;
	}

	return live;
}

qboolean NetReplay_RecordSend( const netadr_t *to, const void *data, size_t length )
{
	if( replay.sends )
	{
		// frame, address (IPv4 and port, network order), size, payload
		uint32_t header[4];

		header[0] = replay.frame;
		header[1] = to->ip4;
		header[2] = to->port;
		header[3] = (uint32_t)length;
		fwrite( header, 1, sizeof( header ), replay.sends );
		fwrite( data, 1, length, replay.sends );
		replay.sent++;
	}

	return replay.mode == MODE_REPLAY;
}
