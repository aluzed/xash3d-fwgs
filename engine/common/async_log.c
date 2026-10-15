/*
async_log.c - log files written on their own thread (story 043)
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

// Port of the ReHLDS fork's asynchronous log writer (MT-301).
//
// With sv_log_async 1 on a dedicated server, the server log file (Log_Printf)
// and the console log (-log, engine.log) are written by a writer thread: the
// main thread formats the line and queues it, so that a slow disk, a network
// file system or a full page cache never stalls a frame.
//
// The queue is bounded (4 MB). When it is full, sv_log_async_overflow "block"
// makes the main thread wait for room, "drop" drops the line and counts it.
// Lines are written in the order they were queued, across files. The files
// are flushed whenever the queue empties, when a log file is closed and on
// Sys_Error.
//
// One producer (the main thread), one consumer (the writer). Positions are byte
// counts since the start, the buffer index is the position modulo QUEUE_SIZE.
// One lock and one condition for both directions: the writer waits for data,
// the main thread for room or for a flush; every change wakes all of them.

#include "common.h"
#include "async_log.h"
#include "sys_thread.h"
#include <stdio.h>
#if XASH_WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define QUEUE_SIZE ( 4 * 1024 * 1024 )

enum
{
	OP_OPEN_LOG,  // payload: full path
	OP_WRITE_LOG, // payload: text
	OP_CLOSE_LOG,
	OP_WRITE_FD,  // payload: int file descriptor, then text
};

typedef struct
{
	uint8_t  op;
	uint8_t  pad[3];
	uint32_t size; // payload bytes
} record_header_t;

static CVAR_DEFINE_AUTO( sv_log_async, "0", 0, "dedicated server: write the log files on a dedicated thread (applies to the next log file)" );
static CVAR_DEFINE_AUTO( sv_log_async_overflow, "block", 0, "with sv_log_async 1, when 4 MB of log lines wait: \"block\" waits for room, \"drop\" drops the line" );

static struct
{
	qboolean started;
	qboolean stop;
	sys_thread_t thread;
	sys_mutex_t lock;
	sys_cond_t cond;

	byte *buffer;
	uint64_t head;          // queued
	uint64_t tail;          // written
	uint64_t flushed;       // written and flushed
	uint64_t flush_request; // a flush is waited for up to this position

	// writer thread only
	FILE *log_file;
	char *scratch;
	uint32_t scratch_size;

	// statistics, under the lock
	uint64_t records;
	uint64_t dropped;
	uint64_t blocked_us;
	uint32_t max_queued;
	uint64_t write_errors;
} writer;

// the current server log file is written by the writer thread
static qboolean log_file_async;

// console log lines go through the queue
static qboolean console_async;

static uint64_t AsyncLog_Now( void )
{
	return (uint64_t)( Platform_DoubleTime( ) * 1000000000.0 );
}

static void AsyncLog_CopyIn( uint64_t pos, const void *data, uint32_t size )
{
	uint32_t index = (uint32_t)( pos % QUEUE_SIZE );
	uint32_t first = Q_min( size, QUEUE_SIZE - index );

	memcpy( writer.buffer + index, data, first );
	memcpy( writer.buffer, (const byte *)data + first, size - first );
}

static void AsyncLog_CopyOut( uint64_t pos, void *data, uint32_t size )
{
	uint32_t index = (uint32_t)( pos % QUEUE_SIZE );
	uint32_t first = Q_min( size, QUEUE_SIZE - index );

	memcpy( data, writer.buffer + index, first );
	memcpy( (byte *)data + first, writer.buffer, size - first );
}

static void AsyncLog_Process( uint8_t op, char *payload, uint32_t size )
{
	switch( op )
	{
	case OP_OPEN_LOG:
		if( writer.log_file )
			fclose( writer.log_file );
		writer.log_file = fopen( payload, "ab" );
		if( !writer.log_file )
			writer.write_errors++;
		break;
	case OP_WRITE_LOG:
		if( !writer.log_file || fwrite( payload, 1, size, writer.log_file ) != size )
			writer.write_errors++;
		break;
	case OP_CLOSE_LOG:
		if( writer.log_file )
		{
			fclose( writer.log_file );
			writer.log_file = NULL;
		}
		break;
	case OP_WRITE_FD:
	{
		int fd;

		memcpy( &fd, payload, sizeof( fd ));
#if XASH_WIN32
		if( _write( fd, payload + sizeof( fd ), size - sizeof( fd )) < 0 )
#else
		if( write( fd, payload + sizeof( fd ), size - sizeof( fd )) < 0 )
#endif
			writer.write_errors++;
		break;
	}
	}
}

static void AsyncLog_Run( void *unused )
{
	Sys_MutexLock( &writer.lock );

	for( ;; )
	{
		uint64_t head, tail;

		while( writer.tail == writer.head && writer.flushed >= writer.flush_request && !writer.stop )
			Sys_CondWait( &writer.cond, &writer.lock );

		if( writer.tail == writer.head && writer.stop )
			break;

		head = writer.head;
		tail = writer.tail;
		Sys_MutexUnlock( &writer.lock );

		// the records between tail and head are the writer's until it moves writer.tail
		while( tail < head )
		{
			record_header_t header;

			AsyncLog_CopyOut( tail, &header, sizeof( header ));

			if( header.size + 1 > writer.scratch_size )
			{
				writer.scratch_size = header.size + 1;
				writer.scratch = realloc( writer.scratch, writer.scratch_size );
			}

			AsyncLog_CopyOut( tail + sizeof( header ), writer.scratch, header.size );
			writer.scratch[header.size] = '\0';

			AsyncLog_Process( header.op, writer.scratch, header.size );
			tail += sizeof( header ) + header.size;

			Sys_MutexLock( &writer.lock );
			writer.tail = tail;
			Sys_CondBroadcast( &writer.cond );
			Sys_MutexUnlock( &writer.lock );
		}

		// written out when the queue empties or a flush is waited for, so that
		// the file is up to date whenever the server is quiet
		Sys_MutexLock( &writer.lock );
		if( writer.tail == writer.head || writer.flush_request > writer.flushed )
		{
			Sys_MutexUnlock( &writer.lock );
			if( writer.log_file )
				fflush( writer.log_file );
			Sys_MutexLock( &writer.lock );

			writer.flushed = tail;
			Sys_CondBroadcast( &writer.cond );
		}
	}

	Sys_MutexUnlock( &writer.lock );

	if( writer.log_file )
	{
		fclose( writer.log_file );
		writer.log_file = NULL;
	}
}

static qboolean AsyncLog_Start( void )
{
	writer.buffer = malloc( QUEUE_SIZE );
	if( !writer.buffer )
		return false;

	Sys_MutexInit( &writer.lock );
	Sys_CondInit( &writer.cond );

	if( !Sys_ThreadStart( &writer.thread, AsyncLog_Run, NULL, "xash-log" ))
	{
		Sys_CondFree( &writer.cond );
		Sys_MutexFree( &writer.lock );
		free( writer.buffer );
		writer.buffer = NULL;
		return false;
	}

	writer.started = true;
	return true;
}

static qboolean AsyncLog_Push( uint8_t op, const void *a, uint32_t a_size, const void *b, uint32_t b_size, qboolean allow_drop )
{
	record_header_t header = { 0 };
	uint32_t total;

	header.op = op;
	header.size = a_size + b_size;
	total = sizeof( header ) + header.size;

	if( total > QUEUE_SIZE )
		return false;

	if( !writer.started && !AsyncLog_Start( ))
		return false;

	Sys_MutexLock( &writer.lock );

	if( QUEUE_SIZE - (uint32_t)( writer.head - writer.tail ) < total )
	{
		uint64_t start;

		if( allow_drop )
		{
			writer.dropped++;
			Sys_MutexUnlock( &writer.lock );
			return false;
		}

		start = AsyncLog_Now( );
		while( QUEUE_SIZE - (uint32_t)( writer.head - writer.tail ) < total )
			Sys_CondWait( &writer.cond, &writer.lock );
		writer.blocked_us += ( AsyncLog_Now( ) - start ) / 1000;
	}

	AsyncLog_CopyIn( writer.head, &header, sizeof( header ));
	if( a_size )
		AsyncLog_CopyIn( writer.head + sizeof( header ), a, a_size );
	if( b_size )
		AsyncLog_CopyIn( writer.head + sizeof( header ) + a_size, b, b_size );

	writer.head += total;
	writer.records++;
	if( (uint32_t)( writer.head - writer.tail ) > writer.max_queued )
		writer.max_queued = (uint32_t)( writer.head - writer.tail );

	Sys_CondBroadcast( &writer.cond );
	Sys_MutexUnlock( &writer.lock );
	return true;
}

static qboolean AsyncLog_AllowDrop( void )
{
	return !Q_stricmp( sv_log_async_overflow.string, "drop" );
}

qboolean AsyncLog_Flush( int timeout_ms )
{
	uint64_t deadline, target;
	qboolean flushed;

	if( !writer.started )
		return true;

	deadline = AsyncLog_Now( ) + (uint64_t)timeout_ms * 1000000;

	Sys_MutexLock( &writer.lock );
	target = writer.head;
	if( writer.flush_request < target )
		writer.flush_request = target;
	Sys_CondBroadcast( &writer.cond );

	while( writer.flushed < target )
	{
		uint64_t now = AsyncLog_Now( );

		if( now >= deadline )
			break;

		Sys_CondWaitMs( &writer.cond, &writer.lock, (int)(( deadline - now ) / 1000000 ) + 1 );
	}

	flushed = writer.flushed >= target;
	Sys_MutexUnlock( &writer.lock );
	return flushed;
}

static void AsyncLog_Stats_f( void )
{
	uint64_t records, dropped, blocked_us, errors;
	uint32_t queued = 0, max_queued;

	if( writer.started )
		Sys_MutexLock( &writer.lock );

	queued = (uint32_t)( writer.head - writer.tail );
	records = writer.records;
	dropped = writer.dropped;
	blocked_us = writer.blocked_us;
	errors = writer.write_errors;
	max_queued = writer.max_queued;

	if( writer.started )
		Sys_MutexUnlock( &writer.lock );

	Con_Printf( "sv_log_async %s, writer thread %s, log file %s, console log %s\n", sv_log_async.string,
		writer.started ? "running" : "not started",
		log_file_async ? "written asynchronously" : "written synchronously",
		console_async ? "written asynchronously" : "written synchronously" );
	Con_Printf( "  records %llu, queued now %u bytes, max %u of %u bytes\n",
		(unsigned long long)records, queued, max_queued, QUEUE_SIZE );
	Con_Printf( "  dropped %llu, main thread blocked %.1f ms in all, write errors %llu\n",
		(unsigned long long)dropped, blocked_us / 1000.0, (unsigned long long)errors );
}

void AsyncLog_Init( void )
{
	Cvar_RegisterVariable( &sv_log_async );
	Cvar_RegisterVariable( &sv_log_async_overflow );
	Cmd_AddCommand( "sv_log_async_stats", AsyncLog_Stats_f, "log writer thread counters" );
}

void AsyncLog_Shutdown( void )
{
	if( !writer.started )
		return;

	AsyncLog_Flush( 10000 );
	console_async = false;
	log_file_async = false;

	Sys_MutexLock( &writer.lock );
	writer.stop = true;
	Sys_CondBroadcast( &writer.cond );
	Sys_MutexUnlock( &writer.lock );

	Sys_ThreadJoin( writer.thread );
	Sys_CondFree( &writer.cond );
	Sys_MutexFree( &writer.lock );

	free( writer.buffer );
	free( writer.scratch );
	writer.buffer = NULL;
	writer.scratch = NULL;
	writer.scratch_size = 0;
	writer.started = writer.stop = false;
}

qboolean AsyncLog_BeginFile( const char *relative_path )
{
	char full_path[MAX_SYSPATH];

	log_file_async = false;

	if( !Host_IsDedicated( ) || !sv_log_async.value )
		return false;

	if( !g_fsapi.GetFullDiskPath( full_path, sizeof( full_path ), relative_path, true ))
	{
		Con_Printf( "sv_log_async: cannot find the path of %s, writing it on the main thread\n", relative_path );
		return false;
	}

	log_file_async = AsyncLog_Push( OP_OPEN_LOG, full_path, Q_strlen( full_path ) + 1, NULL, 0, false );
	return log_file_async;
}

qboolean AsyncLog_IsFileAsync( void )
{
	return log_file_async;
}

void AsyncLog_WriteFile( const char *text )
{
	AsyncLog_Push( OP_WRITE_LOG, text, Q_strlen( text ), NULL, 0, AsyncLog_AllowDrop( ));
}

void AsyncLog_EndFile( void )
{
	if( !log_file_async )
		return;

	AsyncLog_Push( OP_CLOSE_LOG, NULL, 0, NULL, 0, false );
	AsyncLog_Flush( 10000 );
	log_file_async = false;
}

qboolean AsyncLog_WriteConsole( int fd, const char *logtime, size_t logtime_len, const char *text )
{
	qboolean want = Host_IsDedicated( ) && sv_log_async.value != 0.0f;
	char line[MAX_PRINT_MSG + 64];
	size_t len;

	if( want != console_async )
	{
		// switching back to direct writes: what is queued goes first
		if( !want )
			AsyncLog_Flush( 10000 );
		console_async = want;
	}

	if( !console_async )
		return false;

	memcpy( line, &fd, sizeof( fd ));
	len = sizeof( fd );
	len += Q_snprintf( line + len, sizeof( line ) - len, "%.*s%s", (int)logtime_len, logtime, text );
	len = Q_min( len, sizeof( line ) - 1 );

	if( !AsyncLog_Push( OP_WRITE_FD, line, (uint32_t)len, NULL, 0, AsyncLog_AllowDrop( )) && !writer.started )
	{
		console_async = false; // no writer thread: write it directly
		return false;
	}

	return true;
}
