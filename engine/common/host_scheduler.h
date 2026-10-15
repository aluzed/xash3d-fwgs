/*
host_scheduler.h - dedicated server frames on a grid of deadlines (story 038)
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

#ifndef HOST_SCHEDULER_H
#define HOST_SCHEDULER_H

#include "xash3d_types.h"

void Host_SchedulerInit( void );

// sys_scheduler is on
qboolean Host_SchedulerEnabled( void );

// the last Host_SchedulerWait paced a frame: Host_FilterTime accepts it as is
qboolean Host_SchedulerActive( void );

// Waits for the next deadline of a grid with fps points per second.
// fps <= 0 stops pacing frames until the next call with a positive rate.
void Host_SchedulerWait( double fps );

#endif // HOST_SCHEDULER_H
