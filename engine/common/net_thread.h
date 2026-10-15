/*
net_thread.h - server socket read on a dedicated thread, early filtering (story 040)
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

#ifndef NET_THREAD_H
#define NET_THREAD_H

#include "xash3d_types.h"
#include "netadr.h"

// registers the cvars and commands, reads -netthread and -netthread_queue
void NetThread_Init( void );

// NET_Config: right after the server sockets are opened, right before they
// are closed; the thread never uses a closed socket
void NetThread_Start( int sock4, int sock6 );
void NetThread_Stop( void );

qboolean NetThread_IsRunning( void );

// main thread, once per server frame before reading packets: publishes the
// filter configuration when it changed, reports the drops to the profiler
void NetThread_BeginFrame( void );

// main thread: a client is given a slot, publish its address before it is told
void NetThread_ClientConnecting( const netadr_t *adr );

// main thread: the oldest queued packet
qboolean NetThread_Pop( netadr_t *from, byte *data, size_t *length );

#endif // NET_THREAD_H
