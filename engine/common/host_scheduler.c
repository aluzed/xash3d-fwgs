/*
host_scheduler.c - dedicated server frames on a grid of deadlines (story 038)
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

// Port of the ReHLDS fork's frame scheduler (MT-101).
//
// Without it, the dedicated server loop spins: Host_Autosleep rejects the
// frames that come too early and sleeps 100 us slices in between, so the tick
// drifts with the sleep granularity and the cost of the previous frame. With
// sys_scheduler 1, the loop sleeps until the next absolute deadline of a grid
// with one point every 1 / sys_ticrate seconds and runs exactly one frame
// there.

#include "common.h"
#include "host_scheduler.h"
#include "sv_profile.h"
#if XASH_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#else
#include <errno.h>
#include <time.h>
#if XASH_LINUX
#include <sys/prctl.h>
#endif
#endif

// When the server falls behind the grid by more than this many ticks (map
// load, stalled process), the backlog is dropped and the grid restarts from
// now, instead of running the missed frames back to back.
#define MAX_LATE_TICKS 2

#define MAX_SPIN_US 2000.0f

static CVAR_DEFINE_AUTO( sys_scheduler, "0", 0, "dedicated server: run frames on a fixed grid of deadlines, one every 1/sys_ticrate seconds" );
static CVAR_DEFINE_AUTO( sys_spin_us, "0", 0, "with sys_scheduler 1, busy-wait this many microseconds before each tick instead of sleeping (burns CPU)" );

static struct
{
	qboolean active;
	qboolean process_settings_applied;
	uint64_t next_tick; // next deadline on the grid, 0 when not started
	uint64_t period;
#if XASH_WIN32
	HANDLE timer;
#endif
} sched;

void Host_SchedulerInit( void )
{
	Cvar_RegisterVariable( &sys_scheduler );
	Cvar_RegisterVariable( &sys_spin_us );
}

qboolean Host_SchedulerEnabled( void )
{
	return sys_scheduler.value != 0.0f;
}

qboolean Host_SchedulerActive( void )
{
	return sched.active;
}

static void Host_SchedulerApplyProcessSettings( void )
{
	if( sched.process_settings_applied )
		return;

	sched.process_settings_applied = true;

#if XASH_WIN32
	// Windows 10 1803 and later; before, SleepUntil spins the last 2 ms
	sched.timer = CreateWaitableTimerExW( NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
	if( !sched.timer )
		Con_DPrintf( "%s: high resolution timer unavailable\n", __func__ );
#elif XASH_LINUX
	// the default 50 us timer slack would add that much to every sleep
	prctl( PR_SET_TIMERSLACK, 1 );
#endif
}

static void Host_SchedulerSleepUntil( uint64_t deadline )
{
#if XASH_WIN32
	uint64_t now = SV_ProfileNow();

	if( deadline <= now )
		return;

	if( sched.timer )
	{
		// relative due time in 100 ns units
		LARGE_INTEGER due;

		due.QuadPart = -(LONGLONG)(( deadline - now ) / 100 );
		if( due.QuadPart < 0 && SetWaitableTimer( sched.timer, &due, 0, NULL, NULL, FALSE ))
			WaitForSingleObject( sched.timer, INFINITE );
		return;
	}

	// coarse fallback: 1 ms sleeps while far from the deadline, the caller spins the rest
	while( SV_ProfileNow() + 2000000 < deadline )
		Sleep( 1 );
#else
	struct timespec ts;

	ts.tv_sec = (time_t)( deadline / 1000000000ULL );
	ts.tv_nsec = (long)( deadline % 1000000000ULL );

	// clock_nanosleep returns the error number instead of setting errno
	while( clock_nanosleep( CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL ) == EINTR )
		;
#endif
}

static void Host_SchedulerSpinUntil( uint64_t deadline )
{
	while( SV_ProfileNow() < deadline )
	{
#if XASH_WIN32
		YieldProcessor();
#elif defined( __i386__ ) || defined( __x86_64__ )
		__builtin_ia32_pause();
#endif
	}
}

void Host_SchedulerWait( double fps )
{
	uint64_t period, spin, now;

	if( fps <= 0.0 )
	{
		// back to Host_Autosleep, the grid restarts when the scheduler resumes
		sched.active = false;
		sched.next_tick = 0;
		return;
	}

	Host_SchedulerApplyProcessSettings();

	period = (uint64_t)( 1000000000.0 / fps );
	if( period != sched.period )
	{
		// new tick rate: restart the grid
		sched.period = period;
		sched.next_tick = 0;
	}

	now = SV_ProfileNow();
	if( sched.next_tick == 0 || now > sched.next_tick + MAX_LATE_TICKS * period )
		sched.next_tick = now;

	// optionally sleep until a bit before the deadline and spin for the rest,
	// for operators who dedicate a core to the server
	spin = (uint64_t)( bound( 0.0f, sys_spin_us.value, MAX_SPIN_US ) * 1000.0f );
	if( sched.next_tick > spin )
		Host_SchedulerSleepUntil( sched.next_tick - spin );

	Host_SchedulerSpinUntil( sched.next_tick );

	sched.next_tick += period;
	sched.active = true;
}
