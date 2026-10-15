/*
sv_workers.c - worker threads that encode the snapshots (story 047)
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

// Port of the ReHLDS fork's encoding workers (MT-403).
//
// -sys_threads N starts N workers (at most 8, "auto": one per CPU but one).
// SV_ParallelFor hands out the items of a job, under the pool's lock, to the
// workers and to the calling thread, and returns once every item is done; a
// worker takes an item only while the job it woke for is the current one. Workers wait on a condition, or spin sys_threads_spin_us
// microseconds first for the next job (cores dedicated to the server only).

#include "common.h"
#include "server.h"
#include "sys_thread.h"
#include "sv_workers.h"
#if !XASH_WIN32
#include <unistd.h>
#endif

#define MAX_WORKERS 8

static CVAR_DEFINE_AUTO( sys_threads_spin_us, "0", 0, "how long the encoding workers spin for the next frame's snapshots before sleeping" );

static struct
{
	int count;
	sys_thread_t threads[MAX_WORKERS];
	sys_mutex_t lock;
	sys_cond_t wake;

	// the job, under the lock: generation changes with every job
	uint32_t generation;
	sv_parallel_func_t func;
	void *ctx;
	int num_items;
	int next_item;
	qboolean stop;

	volatile uint32_t published;  // generation, for the spinning workers
	volatile uint32_t done_items;
	volatile uint32_t spin_us;

	// statistics
	volatile uint32_t worker_items;
	uint32_t main_items;
	uint32_t jobs;
} pool;

// takes and runs the items of job generation, lock held on entry and exit
static void SV_WorkerRunItems( uint32_t generation, qboolean worker )
{
	while( pool.generation == generation && pool.next_item < pool.num_items )
	{
		int i = pool.next_item++;
		sv_parallel_func_t func = pool.func;
		void *ctx = pool.ctx;

		Sys_MutexUnlock( &pool.lock );

		func( i, ctx );

		if( worker )
			Sys_AtomicAdd( &pool.worker_items, 1 );
		else pool.main_items++;

		Sys_AtomicFetchAdd( &pool.done_items, 1 );
		Sys_MutexLock( &pool.lock );
	}
}

static void SV_WorkerLoop( void *arg )
{
	uint32_t seen = 0;

	Sys_MutexLock( &pool.lock );

	for( ;; )
	{
		uint32_t spin_us = Sys_AtomicLoad( &pool.spin_us );

		// spin for the next job, then sleep
		if( spin_us && pool.generation == seen && !pool.stop )
		{
			double end = Platform_DoubleTime( ) + spin_us * 1e-6;

			Sys_MutexUnlock( &pool.lock );
			while( Sys_AtomicLoadAcquire( &pool.published ) == seen && Platform_DoubleTime( ) < end )
				Sys_CpuRelax( );
			Sys_MutexLock( &pool.lock );
		}

		while( pool.generation == seen && !pool.stop )
			Sys_CondWait( &pool.wake, &pool.lock );

		if( pool.stop )
			break;

		seen = pool.generation;
		SV_WorkerRunItems( seen, true );
	}

	Sys_MutexUnlock( &pool.lock );
}

static void SV_WorkersStats_f( void )
{
	Con_Printf( "%d worker threads, sv_parallel_send %s, sys_threads_spin_us %s\n", pool.count, sv_parallel_send.string, sys_threads_spin_us.string );
	Con_Printf( "  jobs %u, snapshots encoded by the workers %u, by the main thread %u\n",
		pool.jobs, Sys_AtomicLoad( &pool.worker_items ), pool.main_items );
}

void SV_WorkersInit( void )
{
	string value;
	int count = 0, i;

	Cvar_RegisterVariable( &sys_threads_spin_us );
	Cmd_AddCommand( "sv_parallel_send_stats", SV_WorkersStats_f, "snapshot encoding workers counters" );

	if( !Sys_GetParmFromCmdLine( "-sys_threads", value ))
		return;

	if( !Q_stricmp( value, "auto" ))
	{
#if XASH_WIN32
		SYSTEM_INFO info;
		GetSystemInfo( &info );
		count = info.dwNumberOfProcessors - 1;
#else
		count = (int)sysconf( _SC_NPROCESSORS_ONLN ) - 1;
#endif
	}
	else count = Q_atoi( value );

	count = bound( 0, count, MAX_WORKERS );
	if( !count )
		return;

	Sys_MutexInit( &pool.lock );
	Sys_CondInit( &pool.wake );

	for( i = 0; i < count; i++ )
	{
		if( !Sys_ThreadStart( &pool.threads[i], SV_WorkerLoop, NULL, "xash-encode" ))
			break;
	}

	pool.count = i;
	Con_Printf( "%d snapshot encoding workers started\n", pool.count );
}

void SV_WorkersShutdown( void )
{
	int i;

	if( !pool.count )
		return;

	Sys_MutexLock( &pool.lock );
	pool.stop = true;
	Sys_CondBroadcast( &pool.wake );
	Sys_MutexUnlock( &pool.lock );

	for( i = 0; i < pool.count; i++ )
		Sys_ThreadJoin( pool.threads[i] );

	Sys_CondFree( &pool.wake );
	Sys_MutexFree( &pool.lock );
	pool.count = 0;
}

int SV_WorkersCount( void )
{
	return pool.count;
}

void SV_ParallelFor( int count, sv_parallel_func_t func, void *ctx )
{
	uint32_t generation;
	int i;

	if( count <= 0 )
		return;

	// one item, or no worker: no hand-off
	if( !pool.count || count == 1 )
	{
		for( i = 0; i < count; i++ )
			func( i, ctx );
		pool.main_items += count;
		return;
	}

	Sys_AtomicStore( &pool.spin_us, (uint32_t)bound( 0.0f, sys_threads_spin_us.value, 2000.0f ));
	Sys_AtomicStore( &pool.done_items, 0 );

	// the job and the gathered data before it are published by the lock
	Sys_MutexLock( &pool.lock );
	generation = ++pool.generation;
	pool.func = func;
	pool.ctx = ctx;
	pool.num_items = count;
	pool.next_item = 0;
	Sys_AtomicStoreRelease( &pool.published, generation );
	Sys_CondBroadcast( &pool.wake );

	SV_WorkerRunItems( generation, false );
	Sys_MutexUnlock( &pool.lock );

	// the items the workers took; what they wrote is published by done_items
	while( Sys_AtomicLoadAcquire( &pool.done_items ) < (uint32_t)count )
		Sys_CpuRelax( );

	pool.jobs++;
}

uint32_t SV_WorkersItems( void )
{
	return Sys_AtomicLoad( &pool.worker_items );
}
