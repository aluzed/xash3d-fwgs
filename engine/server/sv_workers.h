/*
sv_workers.h - worker threads that encode the snapshots (story 047)
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

#ifndef SV_WORKERS_H
#define SV_WORKERS_H

#include <stdint.h>

typedef void (*sv_parallel_func_t)( int index, void *ctx );

// reads -sys_threads N|auto, registers sys_threads_spin_us and sv_parallel_send_stats
void SV_WorkersInit( void );
void SV_WorkersShutdown( void );

int SV_WorkersCount( void );

// main thread: func( 0 .. count - 1 ) on the workers and the calling thread;
// returns when every item is done
void SV_ParallelFor( int count, sv_parallel_func_t func, void *ctx );

// items done by the workers so far
uint32_t SV_WorkersItems( void );

#endif // SV_WORKERS_H
