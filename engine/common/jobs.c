/*
jobs.c - worker threads for side effect free work
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

/*
A fixed pool of worker threads runs jobs: a function that only touches
its own data (file reads, decoding, building lookup tables). Its "done"
function then runs on the main thread, at the start of the next host
frame or in Job_WaitIdle, and is the only place where the result may meet
the rest of the engine.

Nothing that changes the simulation, the network traffic or calls into the
game DLLs runs on a worker: the server, prediction, CL_Move, delta
decoding and the game DLL callbacks stay on the main thread, as GoldSrc
and its game DLLs expect (docs/stories/029-engine-job-system.md).

-jobthreads N sets the number of workers (default: the CPU count minus
one, up to 4); -jobthreads 0 runs every job on the spot, done included.
*/

#include "common.h"
#include "jobs.h"

#if XASH_SDL == 2
#include <SDL_thread.h>
#include <SDL_cpuinfo.h>
typedef SDL_Thread *job_thread_t;
typedef SDL_mutex *job_mutex_t;
typedef SDL_cond *job_cond_t;
#elif !XASH_WIN32
#include <pthread.h>
#include <unistd.h>
typedef pthread_t job_thread_t;
typedef pthread_mutex_t job_mutex_t;
typedef pthread_cond_t job_cond_t;
#else
typedef HANDLE job_thread_t;
typedef CRITICAL_SECTION job_mutex_t;
typedef CONDITION_VARIABLE job_cond_t;
#endif

#define JOB_MAX_THREADS 8
#define JOB_MAX_QUEUED  1024

typedef struct
{
	job_func_t run;
	job_func_t done;
	void *data;
} job_t;

static struct
{
	qboolean initialized;
	int num_threads;
	job_thread_t threads[JOB_MAX_THREADS];

	job_mutex_t lock;
	job_cond_t wake;    // a job was queued, or shutdown
	job_cond_t idle;    // a job finished

	job_t queue[JOB_MAX_QUEUED]; // ring of jobs waiting for a worker
	int queue_head, queue_count;
	job_t finished[JOB_MAX_QUEUED]; // waiting for their done on the main thread
	int finished_count;
	int running;
	qboolean quit;
} jobs;

/*
=================
Threads, mutexes and conditions of each platform
=================
*/
#if XASH_SDL == 2
static void Job_MutexInit( job_mutex_t *m ) { *m = SDL_CreateMutex(); }
static void Job_MutexFree( job_mutex_t *m ) { SDL_DestroyMutex( *m ); }
static void Job_Lock( job_mutex_t *m ) { SDL_LockMutex( *m ); }
static void Job_Unlock( job_mutex_t *m ) { SDL_UnlockMutex( *m ); }
static void Job_CondInit( job_cond_t *c ) { *c = SDL_CreateCond(); }
static void Job_CondFree( job_cond_t *c ) { SDL_DestroyCond( *c ); }
static void Job_Wait( job_cond_t *c, job_mutex_t *m ) { SDL_CondWait( *c, *m ); }
static void Job_Signal( job_cond_t *c ) { SDL_CondSignal( *c ); }
static void Job_Broadcast( job_cond_t *c ) { SDL_CondBroadcast( *c ); }
static int Job_CPUCount( void ) { return SDL_GetCPUCount(); }
#elif !XASH_WIN32
static void Job_MutexInit( job_mutex_t *m ) { pthread_mutex_init( m, NULL ); }
static void Job_MutexFree( job_mutex_t *m ) { pthread_mutex_destroy( m ); }
static void Job_Lock( job_mutex_t *m ) { pthread_mutex_lock( m ); }
static void Job_Unlock( job_mutex_t *m ) { pthread_mutex_unlock( m ); }
static void Job_CondInit( job_cond_t *c ) { pthread_cond_init( c, NULL ); }
static void Job_CondFree( job_cond_t *c ) { pthread_cond_destroy( c ); }
static void Job_Wait( job_cond_t *c, job_mutex_t *m ) { pthread_cond_wait( c, m ); }
static void Job_Signal( job_cond_t *c ) { pthread_cond_signal( c ); }
static void Job_Broadcast( job_cond_t *c ) { pthread_cond_broadcast( c ); }
static int Job_CPUCount( void ) { return (int)sysconf( _SC_NPROCESSORS_ONLN ); }
#else
static void Job_MutexInit( job_mutex_t *m ) { InitializeCriticalSection( m ); }
static void Job_MutexFree( job_mutex_t *m ) { DeleteCriticalSection( m ); }
static void Job_Lock( job_mutex_t *m ) { EnterCriticalSection( m ); }
static void Job_Unlock( job_mutex_t *m ) { LeaveCriticalSection( m ); }
static void Job_CondInit( job_cond_t *c ) { InitializeConditionVariable( c ); }
static void Job_CondFree( job_cond_t *c ) { }
static void Job_Wait( job_cond_t *c, job_mutex_t *m ) { SleepConditionVariableCS( c, m, INFINITE ); }
static void Job_Signal( job_cond_t *c ) { WakeConditionVariable( c ); }
static void Job_Broadcast( job_cond_t *c ) { WakeAllConditionVariable( c ); }
static int Job_CPUCount( void )
{
	SYSTEM_INFO info;
	GetSystemInfo( &info );
	return info.dwNumberOfProcessors;
}
#endif

static void Job_WorkerLoop( void )
{
	Job_Lock( &jobs.lock );

	for( ;; )
	{
		job_t job;

		while( !jobs.queue_count && !jobs.quit )
			Job_Wait( &jobs.wake, &jobs.lock );

		if( jobs.quit )
			break;

		job = jobs.queue[jobs.queue_head];
		jobs.queue_head = ( jobs.queue_head + 1 ) % JOB_MAX_QUEUED;
		jobs.queue_count--;
		jobs.running++;
		Job_Unlock( &jobs.lock );

		job.run( job.data );

		Job_Lock( &jobs.lock );
		jobs.running--;

		// room is guaranteed: a job is counted queued until it's done
		jobs.finished[jobs.finished_count++] = job;
		Job_Broadcast( &jobs.idle );
	}

	Job_Unlock( &jobs.lock );
}

#if XASH_SDL == 2
static int Job_ThreadStart( void *unused )
{
	Job_WorkerLoop();
	return 0;
}
#elif !XASH_WIN32
static void *Job_ThreadStart( void *unused )
{
	Job_WorkerLoop();
	return NULL;
}
#else
static DWORD WINAPI Job_ThreadStart( LPVOID unused )
{
	Job_WorkerLoop();
	return 0;
}
#endif

static qboolean Job_StartThread( job_thread_t *thread )
{
#if XASH_SDL == 2
	return ( *thread = SDL_CreateThread( Job_ThreadStart, "job worker", NULL )) != NULL;
#elif !XASH_WIN32
	return pthread_create( thread, NULL, Job_ThreadStart, NULL ) == 0;
#else
	return ( *thread = CreateThread( NULL, 0, Job_ThreadStart, NULL, 0, NULL )) != NULL;
#endif
}

static void Job_JoinThread( job_thread_t thread )
{
#if XASH_SDL == 2
	SDL_WaitThread( thread, NULL );
#elif !XASH_WIN32
	pthread_join( thread, NULL );
#else
	WaitForSingleObject( thread, INFINITE );
	CloseHandle( thread );
#endif
}

/*
=================
Lifetime
=================
*/
void Job_Init( void )
{
	int count = bound( 1, Job_CPUCount() - 1, 4 );
	string value;

	if( Sys_GetParmFromCmdLine( "-jobthreads", value ))
		count = bound( 0, Q_atoi( value ), JOB_MAX_THREADS );

	memset( &jobs, 0, sizeof( jobs ));
	Job_MutexInit( &jobs.lock );
	Job_CondInit( &jobs.wake );
	Job_CondInit( &jobs.idle );
	jobs.initialized = true;

	for( int i = 0; i < count; i++ )
	{
		if( !Job_StartThread( &jobs.threads[jobs.num_threads] ))
			break;
		jobs.num_threads++;
	}

	Con_Reportf( "Job system: %d worker threads\n", jobs.num_threads );
}

void Job_Shutdown( void )
{
	if( !jobs.initialized )
		return;

	Job_WaitIdle();

	Job_Lock( &jobs.lock );
	jobs.quit = true;
	Job_Broadcast( &jobs.wake );
	Job_Unlock( &jobs.lock );

	for( int i = 0; i < jobs.num_threads; i++ )
		Job_JoinThread( jobs.threads[i] );

	Job_CondFree( &jobs.idle );
	Job_CondFree( &jobs.wake );
	Job_MutexFree( &jobs.lock );
	memset( &jobs, 0, sizeof( jobs ));
}

/*
=================
Job_Submit

runs run( data ) on a worker, then done( data ) on the main thread. Without
workers, both run at once. False when the queue is full: the caller does
the work itself or tries later
=================
*/
qboolean Job_Submit( job_func_t run, job_func_t done, void *data )
{
	if( !jobs.num_threads )
	{
		run( data );
		if( done )
			done( data );
		return true;
	}

	Job_Lock( &jobs.lock );

	// queued, running and finished jobs share the room of the finished list
	if( jobs.queue_count + jobs.running + jobs.finished_count >= JOB_MAX_QUEUED )
	{
		Job_Unlock( &jobs.lock );
		return false;
	}

	job_t *job = &jobs.queue[( jobs.queue_head + jobs.queue_count ) % JOB_MAX_QUEUED];
	job->run = run;
	job->done = done;
	job->data = data;
	jobs.queue_count++;

	Job_Signal( &jobs.wake );
	Job_Unlock( &jobs.lock );
	return true;
}

// the done functions of the finished jobs, on the main thread
void Job_RunCompletions( void )
{
	job_t finished[JOB_MAX_QUEUED];
	int count;

	if( !jobs.num_threads )
		return;

	Job_Lock( &jobs.lock );
	count = jobs.finished_count;
	memcpy( finished, jobs.finished, count * sizeof( job_t ));
	jobs.finished_count = 0;
	Job_Unlock( &jobs.lock );

	// a done function may submit more jobs
	for( int i = 0; i < count; i++ )
	{
		if( finished[i].done )
			finished[i].done( finished[i].data );
	}
}

// every job submitted so far is finished and done: map changes, shutdowns
void Job_WaitIdle( void )
{
	if( !jobs.num_threads )
		return;

	for( ;; )
	{
		Job_Lock( &jobs.lock );
		while( jobs.queue_count || jobs.running )
			Job_Wait( &jobs.idle, &jobs.lock );
		const qboolean finished = jobs.finished_count != 0;
		Job_Unlock( &jobs.lock );

		if( !finished )
			break;

		Job_RunCompletions(); // may queue more
	}
}

#if XASH_ENGINE_TESTS
#include "tests.h"

/*
every job runs once, every done once and on the main thread, a done may
submit more, a full queue refuses instead of losing jobs; run under
ThreadSanitizer to check the locking
*/
#define TEST_JOBS 2000

static struct
{
	int ran[TEST_JOBS * 2];  // written by the workers, each job its own slot
	int done[TEST_JOBS * 2]; // main thread only
	int done_count;
	int resubmitted;
	qboolean wrong_thread;
	unsigned long main_thread;
} job_test;

static unsigned long Job_TestThreadId( void )
{
#if XASH_SDL == 2
	return SDL_ThreadID();
#elif !XASH_WIN32
	return (unsigned long)pthread_self();
#else
	return GetCurrentThreadId();
#endif
}

static void Job_TestRun( void *data )
{
	const int i = (int)(intptr_t)data;
	volatile int sum = 0;

	for( int k = 0; k < 1000; k++ )
		sum += k * i;

	job_test.ran[i]++;
}

static void Job_TestDone( void *data )
{
	const int i = (int)(intptr_t)data;

	if( Job_TestThreadId() != job_test.main_thread )
		job_test.wrong_thread = true;

	job_test.done[i]++;
	job_test.done_count++;

	// some done functions queue more work
	if( i < TEST_JOBS && ( i % 10 ) == 0 )
	{
		while( !Job_Submit( Job_TestRun, Job_TestDone, (void *)(intptr_t)( i + TEST_JOBS )))
			Job_RunCompletions();
		job_test.resubmitted++;
	}
}

void Test_RunJobs( void )
{
	const qboolean was_initialized = jobs.initialized;
	int refused = 0;

	if( !was_initialized )
		Job_Init();

	memset( &job_test, 0, sizeof( job_test ));
	job_test.main_thread = Job_TestThreadId();

	for( int i = 0; i < TEST_JOBS; i++ )
	{
		// more jobs than the queue holds: refused, then room is made
		while( !Job_Submit( Job_TestRun, Job_TestDone, (void *)(intptr_t)i ))
		{
			refused++;
			Job_RunCompletions();
		}
	}

	Job_WaitIdle();

	TASSERT( job_test.done_count == TEST_JOBS + job_test.resubmitted );
	TASSERT( job_test.resubmitted == TEST_JOBS / 10 );
	TASSERT( !job_test.wrong_thread );

	for( int i = 0; i < TEST_JOBS * 2; i++ )
	{
		const int expected = i < TEST_JOBS || ( i - TEST_JOBS ) % 10 == 0 ? 1 : 0;

		TASSERT( job_test.ran[i] == expected );
		TASSERT( job_test.done[i] == expected );
	}

	TASSERT( jobs.num_threads == 0 || refused > 0 ); // the queue filled up at least once

	if( !was_initialized )
		Job_Shutdown();
}
#endif // XASH_ENGINE_TESTS
