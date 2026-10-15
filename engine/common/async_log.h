/*
async_log.h - log files written on their own thread (story 043)
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

#ifndef ASYNC_LOG_H
#define ASYNC_LOG_H

#include "xash3d_types.h"

void AsyncLog_Init( void );

// writes out what is queued and stops the writer thread
void AsyncLog_Shutdown( void );

// Server log file, already created by the caller. Returns false when it must
// be written on the main thread (sv_log_async 0, path not found, no thread).
// sv_log_async is read here: it applies to the next log file.
qboolean AsyncLog_BeginFile( const char *relative_path );
qboolean AsyncLog_IsFileAsync( void );
void AsyncLog_WriteFile( const char *text );
// writes out what is queued and closes the writer's handle
void AsyncLog_EndFile( void );

// Console log line (-log): queued for the file descriptor when sv_log_async is
// on, returns false when the caller writes it itself
qboolean AsyncLog_WriteConsole( int fd, const char *logtime, size_t logtime_len, const char *text );

// waits until everything queued is written and flushed, at most timeout_ms
// milliseconds (the disk may be stuck); returns whether it was
qboolean AsyncLog_Flush( int timeout_ms );

#endif // ASYNC_LOG_H
