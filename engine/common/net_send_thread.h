/*
net_send_thread.h - server packets sent from a dedicated thread (story 041)
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

#ifndef NET_SEND_THREAD_H
#define NET_SEND_THREAD_H

#include "xash3d_types.h"

// registers the command, reads -netsend and -netsend_queue
void NetSend_Init( void );

// NET_Config: after the server sockets are opened; before they are closed,
// once everything queued was sent
void NetSend_Start( void );
void NetSend_Stop( void );

qboolean NetSend_IsRunning( void );

// main thread: queues a server packet, split in parts of split_size bytes
// numbered split_sequence when split_size is not 0
struct sockaddr_storage;
void NetSend_Push( int net_socket, const struct sockaddr_storage *to, size_t to_len, const void *data, size_t length, size_t split_size, int split_sequence );

#endif // NET_SEND_THREAD_H
