/*
jobs.h - worker threads for side effect free work
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

#ifndef JOBS_H
#define JOBS_H

typedef void (*job_func_t)( void *data );

void Job_Init( void );
void Job_Shutdown( void );
qboolean Job_Submit( job_func_t run, job_func_t done, void *data );
void Job_RunCompletions( void );
void Job_WaitIdle( void );

#endif // JOBS_H
