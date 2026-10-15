/*
sv_profile.c - per-phase server frame profiler (story 035)
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

// Port of the ReHLDS fork's frame profiler (MT-002), same report and CSV.
//
// Every frame's phase times and counters go into a ring buffer allocated
// when profiling is switched on, so nothing is allocated while frames run.
// A frame left by Host_Error never reaches SV_ProfileEndFrame and is dropped.

#include "common.h"
#include "sv_profile.h"
#include <math.h>
#if XASH_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#define MIN_CAPACITY 1000
#define MAX_CAPACITY 600000

// Columns of one stored frame row
#define COL_TICK_INTERVAL 0
#define COL_PHASES        1
#define COL_COUNTERS      ( COL_PHASES + SVP_COUNT )
#define COL_COUNT         ( COL_COUNTERS + SVC_COUNT )

static CVAR_DEFINE_AUTO( sv_profile_enable, "0", 0, "record the per-phase timings of every frame continuously" );
static CVAR_DEFINE_AUTO( sv_profile_frames, "60000", 0, "number of frames the profiler keeps, applied after sv_profile_reset" );

sv_profile_state_t sv_profile_state;

static struct
{
	qboolean in_frame;
	uint64_t frame_start;
	uint64_t prev_frame_start;
	uint64_t tick_interval;
	uint64_t capture_end; // timed capture started by "sv_profile <seconds>"

	// ring buffer of frame rows, oldest first starting at ( head - count )
	float *rows;
	int capacity;
	int head;
	int count;
} prof;

static const struct
{
	const char *name;
	int depth;
} phase_info[SVP_COUNT] =
{
	{ "frame",              0 },
	{ "cbuf",               1 },
	{ "sv_frame",           1 },
	{ "read_packets",       2 },
	{ "connectionless",     3 },
	{ "netchan_process",    3 },
	{ "execute_client_msg", 3 },
	{ "physics",            2 },
	{ "check_timeouts",     2 },
	{ "send_messages",      2 },
	{ "datagram",           3 },
	{ "gather",             4 },
	{ "encode",             4 },
	{ "transmit",           3 },
	{ "heartbeat",          2 },
	{ "log_write",          1 },
};

static const char *const counter_names[SVC_COUNT] =
{
	"filter_rejects",
	"packets_in",
	"bytes_in",
	"packets_out",
	"bytes_out",
	"clients_sent",
	"entities_sent",
	"net_thread_drops",
	"net_queue_wait_us",
	"send_worker_items",
	"early_read_us",
};

typedef struct
{
	double avg;
	float  p50, p95, p99, max;
	double stddev;
} column_stats_t;

uint64_t SV_ProfileNow( void )
{
#if XASH_WIN32
	static LARGE_INTEGER freq;
	LARGE_INTEGER counter;

	if( !freq.QuadPart )
		QueryPerformanceFrequency( &freq );
	QueryPerformanceCounter( &counter );

	return (uint64_t)( counter.QuadPart / freq.QuadPart ) * 1000000000ULL
		+ (uint64_t)( counter.QuadPart % freq.QuadPart ) * 1000000000ULL / freq.QuadPart;
#else
	struct timespec ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static void SV_ProfileClearRows( void )
{
	prof.head = 0;
	prof.count = 0;
}

static qboolean SV_ProfileAllocate( void )
{
	int capacity;

	if( prof.rows )
		return true;

	capacity = bound( MIN_CAPACITY, (int)sv_profile_frames.value, MAX_CAPACITY );
	prof.rows = Mem_Malloc( host.mempool, sizeof( float ) * COL_COUNT * capacity );
	prof.capacity = capacity;
	SV_ProfileClearRows();
	return true;
}

static void SV_ProfileFree( void )
{
	if( prof.rows )
		Mem_Free( prof.rows );

	prof.rows = NULL;
	prof.capacity = 0;
	SV_ProfileClearRows();
}

static float *SV_ProfileRow( int index )
{
	// index 0 is the oldest stored frame
	int pos = ( prof.head - prof.count + index + prof.capacity ) % prof.capacity;
	return &prof.rows[pos * COL_COUNT];
}

static void SV_ProfileResetFrame( void )
{
	memset( sv_profile_state.phase_ns, 0, sizeof( sv_profile_state.phase_ns ));
	memset( sv_profile_state.counters, 0, sizeof( sv_profile_state.counters ));
}

void SV_ProfileBeginFrame( void )
{
	qboolean want_active = sv_profile_enable.value != 0.0f || prof.capture_end != 0;

	if( want_active && !prof.rows && !SV_ProfileAllocate( ))
	{
		want_active = false;
		prof.capture_end = 0;
	}

	if( want_active != sv_profile_state.active )
	{
		sv_profile_state.active = want_active;
		prof.prev_frame_start = 0;
		prof.in_frame = false;
		SV_ProfileResetFrame();
	}

	if( !sv_profile_state.active )
		return;

	// the previous frame was left by Host_Error: drop what it accumulated
	if( prof.in_frame )
		memset( sv_profile_state.phase_ns, 0, sizeof( sv_profile_state.phase_ns ));

	memset( sv_profile_state.phase_start, 0, sizeof( sv_profile_state.phase_start ));

	prof.frame_start = SV_ProfileNow();
	prof.tick_interval = prof.prev_frame_start ? prof.frame_start - prof.prev_frame_start : 0;
	prof.prev_frame_start = prof.frame_start;
	prof.in_frame = true;

	sv_profile_state.phase_start[SVP_FRAME] = prof.frame_start;
}

static int SV_ProfileCompareFloats( const void *a, const void *b )
{
	float fa = *(const float *)a, fb = *(const float *)b;
	return ( fa > fb ) - ( fa < fb );
}

static column_stats_t SV_ProfileColumnStats( float *values, int column )
{
	column_stats_t stats = { 0 };
	double sum = 0.0, variance = 0.0;
	int i, n = prof.count;

	for( i = 0; i < n; i++ )
	{
		values[i] = SV_ProfileRow( i )[column];
		sum += values[i];
	}

	stats.avg = sum / n;

	for( i = 0; i < n; i++ )
		variance += ( values[i] - stats.avg ) * ( values[i] - stats.avg );

	stats.stddev = sqrt( variance / n );

	qsort( values, n, sizeof( *values ), SV_ProfileCompareFloats );
	stats.p50 = values[(int)( 0.50 * ( n - 1 ) + 0.5 )];
	stats.p95 = values[(int)( 0.95 * ( n - 1 ) + 0.5 )];
	stats.p99 = values[(int)( 0.99 * ( n - 1 ) + 0.5 )];
	stats.max = values[n - 1];
	return stats;
}

static void SV_ProfilePrintReport( void )
{
	column_stats_t tick, frame, stats;
	double seconds, fps;
	float *values;
	int i;

	if( !prof.rows || prof.count == 0 )
	{
		Con_Printf( "sv_profile: no frames recorded\n" );
		return;
	}

	values = Mem_Malloc( host.mempool, sizeof( float ) * prof.count );

	tick = SV_ProfileColumnStats( values, COL_TICK_INTERVAL );
	frame = SV_ProfileColumnStats( values, COL_PHASES + SVP_FRAME );

	seconds = tick.avg * prof.count / 1000000.0;
	fps = tick.avg > 0.0 ? 1000000.0 / tick.avg : 0.0;

	Con_Printf( "sv_profile: %d frames over %.2f s, %.1f fps (sys_ticrate %.0f), busy %.1f%%\n",
		prof.count, seconds, fps, Cvar_VariableValue( "sys_ticrate" ),
		tick.avg > 0.0 ? 100.0 * frame.avg / tick.avg : 0.0 );

	Con_Printf( "%-24s %9s %9s %9s %9s %9s %9s\n", "time (us)", "avg", "p50", "p95", "p99", "max", "stddev" );
	Con_Printf( "%-24s %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f\n", "tick_interval",
		tick.avg, tick.p50, tick.p95, tick.p99, tick.max, tick.stddev );

	for( i = 0; i < SVP_COUNT; i++ )
	{
		char label[64];

		stats = SV_ProfileColumnStats( values, COL_PHASES + i );
		Q_snprintf( label, sizeof( label ), "%*s%s", phase_info[i].depth * 2, "", phase_info[i].name );
		Con_Printf( "%-24s %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f\n", label,
			stats.avg, stats.p50, stats.p95, stats.p99, stats.max, stats.stddev );
	}

	Con_Printf( "%-24s %9s %9s %9s %9s %9s\n", "per frame", "avg", "p50", "p95", "p99", "max" );
	for( i = 0; i < SVC_COUNT; i++ )
	{
		stats = SV_ProfileColumnStats( values, COL_COUNTERS + i );
		Con_Printf( "%-24s %9.1f %9.0f %9.0f %9.0f %9.0f\n", counter_names[i],
			stats.avg, stats.p50, stats.p95, stats.p99, stats.max );
	}

	Mem_Free( values );
}

void SV_ProfileEndFrame( void )
{
	if( !sv_profile_state.active || !prof.in_frame )
		return;

	SV_ProfileEnd( SVP_FRAME );
	prof.in_frame = false;

	// encode time is everything spent building datagrams outside game DLL calls
	sv_profile_state.phase_ns[SVP_SEND_ENCODE] =
		sv_profile_state.phase_ns[SVP_SEND_DATAGRAM] > sv_profile_state.phase_ns[SVP_SEND_GATHER] ?
		sv_profile_state.phase_ns[SVP_SEND_DATAGRAM] - sv_profile_state.phase_ns[SVP_SEND_GATHER] : 0;

	// the first frame after activation has no previous frame to measure the tick from
	if( prof.tick_interval )
	{
		float *row = &prof.rows[prof.head * COL_COUNT];
		int i;

		row[COL_TICK_INTERVAL] = prof.tick_interval * 0.001;

		for( i = 0; i < SVP_COUNT; i++ )
			row[COL_PHASES + i] = sv_profile_state.phase_ns[i] * 0.001;

		for( i = 0; i < SVC_COUNT; i++ )
			row[COL_COUNTERS + i] = sv_profile_state.counters[i];

		prof.head = ( prof.head + 1 ) % prof.capacity;
		if( prof.count < prof.capacity )
			prof.count++;
	}

	SV_ProfileResetFrame();

	if( prof.capture_end && prof.frame_start >= prof.capture_end )
	{
		prof.capture_end = 0;
		SV_ProfilePrintReport();
	}
}

void SV_ProfileFrameRejected( void )
{
	SV_ProfileCount( SVC_FILTER_REJECTS, 1 );
}

static void SV_Profile_f( void )
{
	double seconds;

	if( Cmd_Argc() < 2 )
	{
		if( prof.count == 0 )
		{
			Con_Printf( "Usage: sv_profile <seconds>  - record frames for <seconds>, then print a report\n" );
			Con_Printf( "       sv_profile            - print a report of the recorded frames\n" );
			Con_Printf( "Set sv_profile_enable 1 to record continuously (last sv_profile_frames frames).\n" );
			return;
		}

		SV_ProfilePrintReport();
		return;
	}

	seconds = Q_atof( Cmd_Argv( 1 ));
	if( seconds <= 0.0 )
	{
		Con_Printf( "sv_profile: duration must be positive\n" );
		return;
	}

	if( !SV_ProfileAllocate( ))
		return;

	SV_ProfileClearRows();
	prof.capture_end = SV_ProfileNow() + (uint64_t)( seconds * 1000000000.0 );
	Con_Printf( "sv_profile: recording %.1f s\n", seconds );
}

static void SV_ProfileCsv_f( void )
{
	const char *filename;
	file_t *f;
	int r, i;

	if( Cmd_Argc() < 2 )
	{
		Con_Printf( "Usage: sv_profile_csv <file>  - write recorded frames to <file> in the game directory\n" );
		return;
	}

	filename = Cmd_Argv( 1 );
	if( filename[0] == '/' || filename[0] == '\\' || Q_strstr( filename, ".." ) || Q_strchr( filename, ':' ))
	{
		Con_Printf( "sv_profile_csv: invalid file name\n" );
		return;
	}

	if( !prof.rows || prof.count == 0 )
	{
		Con_Printf( "sv_profile_csv: no frames recorded\n" );
		return;
	}

	f = FS_Open( filename, "w", true );
	if( !f )
	{
		Con_Printf( "sv_profile_csv: unable to open %s\n", filename );
		return;
	}

	FS_Printf( f, "tick_interval_us" );
	for( i = 0; i < SVP_COUNT; i++ )
		FS_Printf( f, ",%s_us", phase_info[i].name );
	for( i = 0; i < SVC_COUNT; i++ )
		FS_Printf( f, ",%s", counter_names[i] );
	FS_Printf( f, "\n" );

	for( r = 0; r < prof.count; r++ )
	{
		const float *row = SV_ProfileRow( r );

		FS_Printf( f, "%.1f", row[COL_TICK_INTERVAL] );
		for( i = 0; i < SVP_COUNT; i++ )
			FS_Printf( f, ",%.1f", row[COL_PHASES + i] );
		for( i = 0; i < SVC_COUNT; i++ )
			FS_Printf( f, ",%.0f", row[COL_COUNTERS + i] );
		FS_Printf( f, "\n" );
	}

	FS_Close( f );
	Con_Printf( "sv_profile_csv: wrote %d frames to %s\n", prof.count, filename );
}

static void SV_ProfileReset_f( void )
{
	prof.capture_end = 0;

	// release the buffer so that a new sv_profile_frames value is applied
	SV_ProfileFree();
	sv_profile_state.active = false;
	Con_Printf( "sv_profile: reset\n" );
}

void SV_ProfileInit( void )
{
	Cvar_RegisterVariable( &sv_profile_enable );
	Cvar_RegisterVariable( &sv_profile_frames );

	Cmd_AddCommand( "sv_profile", SV_Profile_f, "record frames for <seconds> and print a per-phase report" );
	Cmd_AddCommand( "sv_profile_csv", SV_ProfileCsv_f, "write the recorded frames to a CSV file" );
	Cmd_AddCommand( "sv_profile_reset", SV_ProfileReset_f, "drop the recorded frames" );
}
