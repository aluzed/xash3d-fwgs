/*
net_replay.h - byte-exact record and replay of a server session (story 037)
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

#ifndef NET_REPLAY_H
#define NET_REPLAY_H

#include "xash3d_types.h"
#include "netadr.h"
#include <stdint.h>

// Records the inputs of a dedicated server session and replays them, so that
// two engine builds can be checked for byte-identical network output.
//
//	-netrecord <file>   records the session's inputs to <file>
//	-netreplay <file>   replays them instead of reading the clock, the server
//	                    socket and the console, as fast as possible, then quits
//	-netsends <file>    in both modes, writes every packet sent by the server
//	                    socket to <file> (scripts/server/bench/replay_compare.py)
//
// Inputs are, in the order the engine consumed them: the realtime of each
// accepted frame, the packets NET_GetPacket( NS_SERVER ) returned (and its "no
// packet" answers), console text, and the values read from the clock or the
// system (random seed, the game's pfnTime). The rest of the server is
// deterministic, so a replay reproduces the recorded session.
//
// The command line of the replay must be the recorded one, plus -netreplay.

void NetReplay_Init( void );
void NetReplay_Shutdown( void );

qboolean NetReplay_IsRecording( void );
qboolean NetReplay_IsReplaying( void );

// Host_FilterTime: the realtime of an accepted frame
void NetReplay_RecordFrame( double realtime );
// replay: the next frame's realtime; quits at the end of the recording
double NetReplay_ReplayFrame( void );

// NET_GetPacket( NS_SERVER )
void NetReplay_RecordPacket( qboolean received, const netadr_t *from, const byte *data, size_t length );
qboolean NetReplay_ReplayPacket( netadr_t *from, byte *data, size_t *length );

// console input (Host_GetCommands): recorded, or the recorded text
void NetReplay_RecordConsole( const char *text );
const char *NetReplay_ReplayConsole( void );

// values from the clock or the system: recorded, or the recorded value
int64_t NetReplay_Value( int64_t live );
double NetReplay_ValueF( double live );

// NET_SendPacketEx( NS_SERVER ): written to the sends file; returns true
// when replaying, the packet must not be sent
qboolean NetReplay_RecordSend( const netadr_t *to, const void *data, size_t length );

#endif // NET_REPLAY_H
