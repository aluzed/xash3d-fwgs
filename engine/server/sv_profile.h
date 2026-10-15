/*
sv_profile.h - per-phase server frame profiler (story 035)
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

#ifndef SV_PROFILE_H
#define SV_PROFILE_H

#include <stdint.h>
#include "xash3d_types.h"

// Timed sections of a host frame. A phase may be entered several times per
// frame (e.g. once per client): its durations are summed for the frame.
// Names and order follow the ReHLDS fork's profiler, so that its report
// tools read both.
typedef enum
{
	SVP_FRAME,              // accepted host frame, from Host_FilterTime to the end of Host_Frame
	SVP_CBUF,               // Host_GetCommands (console input)
	SVP_SV_FRAME,           // Host_ServerFrame
	SVP_READ_PACKETS,       // SV_ReadPackets
	SVP_CONNECTIONLESS,     // connectionless packets (queries, challenges, connects)
	SVP_NETCHAN_PROCESS,    // Netchan_Process and fragment reassembly
	SVP_EXECUTE_CLIENT_MSG, // SV_ExecuteClientMessage (game DLL: commands, movement)
	SVP_PHYSICS,            // SV_RunGameFrame (game DLL: StartFrame, Think, Touch)
	SVP_CHECK_TIMEOUTS,     // SV_CheckTimeouts
	SVP_SEND_MESSAGES,      // SV_SendClientMessages
	SVP_SEND_DATAGRAM,      // datagram build for all clients (gather + encode)
	SVP_SEND_GATHER,        // game DLL calls: SetupVisibility, AddToFullPack, UpdateClientData, GetWeaponData
	SVP_SEND_ENCODE,        // derived: SVP_SEND_DATAGRAM - SVP_SEND_GATHER
	SVP_SEND_TRANSMIT,      // Netchan_TransmitBits for spawned clients
	SVP_HEARTBEAT,          // NET_MasterHeartbeat (the "steam" column of ReHLDS)
	SVP_LOG_WRITE,          // log file writes in Log_Printf

	SVP_COUNT
} sv_profile_phase_t;

// Per-frame counters, summed over the frame (including rejected wake-ups before it)
typedef enum
{
	SVC_FILTER_REJECTS,     // host frames rejected by Host_FilterTime since the previous frame
	SVC_PACKETS_IN,         // server socket packets read
	SVC_BYTES_IN,
	SVC_PACKETS_OUT,        // server socket packets sent (before split)
	SVC_BYTES_OUT,
	SVC_CLIENTS_SENT,       // clients that received a snapshot
	SVC_ENTITIES_SENT,      // packet entities, summed over clients
	SVC_NET_THREAD_DROPS,   // packets dropped by the network thread (story 040)
	SVC_NET_QUEUE_WAIT_US,  // time spent in the network thread's queue, summed over packets
	SVC_SEND_WORKER_ITEMS,  // snapshots encoded by the workers (story 047)
	SVC_EARLY_READ_US,      // packets read between the previous frame and this one

	SVC_COUNT
} sv_profile_counter_t;

typedef struct
{
	qboolean active;
	uint64_t phase_start[SVP_COUNT];
	uint64_t phase_ns[SVP_COUNT];
	int64_t  counters[SVC_COUNT];
} sv_profile_state_t;

extern sv_profile_state_t sv_profile_state;

uint64_t SV_ProfileNow( void );

void SV_ProfileInit( void );
void SV_ProfileBeginFrame( void );
void SV_ProfileEndFrame( void );
void SV_ProfileFrameRejected( void );

static inline void SV_ProfileBegin( sv_profile_phase_t phase )
{
	if( sv_profile_state.active )
		sv_profile_state.phase_start[phase] = SV_ProfileNow();
}

static inline void SV_ProfileEnd( sv_profile_phase_t phase )
{
	if( sv_profile_state.active && sv_profile_state.phase_start[phase] )
	{
		sv_profile_state.phase_ns[phase] += SV_ProfileNow() - sv_profile_state.phase_start[phase];
		sv_profile_state.phase_start[phase] = 0;
	}
}

static inline void SV_ProfileCount( sv_profile_counter_t counter, int64_t value )
{
	if( sv_profile_state.active )
		sv_profile_state.counters[counter] += value;
}

#endif // SV_PROFILE_H
