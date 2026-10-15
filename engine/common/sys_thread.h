/*
sys_thread.h - threads, locks and conditions for the server threads
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

// Native threads for the dedicated server stories (043, 040, 041): the
// dedicated build has no SDL, so these use pthreads or Win32 directly.

#ifndef SYS_THREAD_H
#define SYS_THREAD_H

#include "xash3d_types.h"
#include <stdint.h>

#if XASH_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE sys_thread_t;
typedef SRWLOCK sys_mutex_t;
typedef CONDITION_VARIABLE sys_cond_t;
#else
#include <pthread.h>
#include <time.h>
#include <errno.h>
typedef pthread_t sys_thread_t;
typedef pthread_mutex_t sys_mutex_t;
typedef pthread_cond_t sys_cond_t;
#endif

typedef void (*sys_thread_func_t)( void *arg );

typedef struct
{
	sys_thread_func_t func;
	void *arg;
	const char *name;
} sys_thread_start_t;

#if XASH_WIN32
static inline DWORD WINAPI Sys_ThreadEntry( LPVOID param )
{
	sys_thread_start_t start = *(sys_thread_start_t *)param;
	free( param );
	start.func( start.arg );
	return 0;
}
#else
static inline void *Sys_ThreadEntry( void *param )
{
	sys_thread_start_t start = *(sys_thread_start_t *)param;
	free( param );
#if XASH_LINUX
	pthread_setname_np( pthread_self( ), start.name );
#endif
	start.func( start.arg );
	return NULL;
}
#endif

// name: at most 15 characters (Linux thread names)
static inline qboolean Sys_ThreadStart( sys_thread_t *thread, sys_thread_func_t func, void *arg, const char *name )
{
	sys_thread_start_t *start = (sys_thread_start_t *)malloc( sizeof( *start ));

	if( !start )
		return false;

	start->func = func;
	start->arg = arg;
	start->name = name;

#if XASH_WIN32
	if(( *thread = CreateThread( NULL, 0, Sys_ThreadEntry, start, 0, NULL )) != NULL )
		return true;
#else
	if( pthread_create( thread, NULL, Sys_ThreadEntry, start ) == 0 )
		return true;
#endif

	free( start );
	return false;
}

static inline void Sys_ThreadJoin( sys_thread_t thread )
{
#if XASH_WIN32
	WaitForSingleObject( thread, INFINITE );
	CloseHandle( thread );
#else
	pthread_join( thread, NULL );
#endif
}

#if XASH_WIN32
static inline void Sys_MutexInit( sys_mutex_t *m ) { InitializeSRWLock( m ); }
static inline void Sys_MutexFree( sys_mutex_t *m ) { }
static inline void Sys_MutexLock( sys_mutex_t *m ) { AcquireSRWLockExclusive( m ); }
static inline void Sys_MutexUnlock( sys_mutex_t *m ) { ReleaseSRWLockExclusive( m ); }
static inline void Sys_CondInit( sys_cond_t *c ) { InitializeConditionVariable( c ); }
static inline void Sys_CondFree( sys_cond_t *c ) { }
static inline void Sys_CondWait( sys_cond_t *c, sys_mutex_t *m ) { SleepConditionVariableSRW( c, m, INFINITE, 0 ); }
static inline void Sys_CondWaitMs( sys_cond_t *c, sys_mutex_t *m, int ms ) { SleepConditionVariableSRW( c, m, ms, 0 ); }
static inline void Sys_CondSignal( sys_cond_t *c ) { WakeConditionVariable( c ); }
static inline void Sys_CondBroadcast( sys_cond_t *c ) { WakeAllConditionVariable( c ); }
#else
static inline void Sys_MutexInit( sys_mutex_t *m ) { pthread_mutex_init( m, NULL ); }
static inline void Sys_MutexFree( sys_mutex_t *m ) { pthread_mutex_destroy( m ); }
static inline void Sys_MutexLock( sys_mutex_t *m ) { pthread_mutex_lock( m ); }
static inline void Sys_MutexUnlock( sys_mutex_t *m ) { pthread_mutex_unlock( m ); }

#if XASH_APPLE
#define SYS_COND_CLOCK CLOCK_REALTIME // no pthread_condattr_setclock
#else
#define SYS_COND_CLOCK CLOCK_MONOTONIC
#endif

static inline void Sys_CondInit( sys_cond_t *c )
{
	pthread_condattr_t attr;

	pthread_condattr_init( &attr );
#if !XASH_APPLE
	pthread_condattr_setclock( &attr, CLOCK_MONOTONIC );
#endif
	pthread_cond_init( c, &attr );
	pthread_condattr_destroy( &attr );
}

static inline void Sys_CondFree( sys_cond_t *c ) { pthread_cond_destroy( c ); }
static inline void Sys_CondWait( sys_cond_t *c, sys_mutex_t *m ) { pthread_cond_wait( c, m ); }

static inline void Sys_CondWaitMs( sys_cond_t *c, sys_mutex_t *m, int ms )
{
	struct timespec deadline;

	clock_gettime( SYS_COND_CLOCK, &deadline );
	deadline.tv_sec += ms / 1000;
	deadline.tv_nsec += ( ms % 1000 ) * 1000000L;
	if( deadline.tv_nsec >= 1000000000L )
	{
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}

	pthread_cond_timedwait( c, m, &deadline );
}

static inline void Sys_CondSignal( sys_cond_t *c ) { pthread_cond_signal( c ); }
static inline void Sys_CondBroadcast( sys_cond_t *c ) { pthread_cond_broadcast( c ); }
#endif

#endif // SYS_THREAD_H
