/*
vk_profile.c - GPU time of each part of the frame
Copyright (C) 2026 Xash3D FWGS contributors

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
With r_vk_gpu_profile 1, the frame writes a timestamp each time it enters
a new part (world, solid entities, ...): a part lasts until the next mark,
so the parts add up to the whole frame. The timestamps of a frame are read
when its fence is waited for again, VK_FRAMES_IN_FLIGHT frames later.

The statistics separate the frames whose GPU time is over
r_vk_gpu_profile_slow milliseconds from the others, to tell which part
grows in the slow ones. r_vk_gpu_profile_print shows them (and so does
the renderer shutdown while the profile is on), r_vk_gpu_profile_reset
starts over.
*/

#include "vk_local.h"

CVAR_DEFINE_AUTO( r_vk_gpu_profile, "0", 0, "measure the GPU time of each part of the frame" );
CVAR_DEFINE_AUTO( r_vk_gpu_profile_slow, "0.35", 0, "GPU frame time in milliseconds above which a frame counts as slow in the profile" );

#define VK_PROFILE_MAX_MARKS 64 // per frame, a part can come back (cubemap views)

static const char *vk_profile_names[VK_PROF_COUNT] =
{
	"frame start",
	"world",
	"solid entities",
	"effects",
	"translucent",
	"view model",
	"water",
	"2D",
	"present copy",
};

typedef struct
{
	byte marks[VK_PROFILE_MAX_MARKS];
	int num_marks;
	qboolean recorded; // marks were written by this slot's last commands
} vk_profile_frame_t;

typedef struct
{
	double sum[VK_PROF_COUNT];
	double max[VK_PROF_COUNT];
	double total;
	int frames;
} vk_profile_stats_t;

static struct
{
	VkQueryPool pool;
	vk_profile_frame_t frames[VK_FRAMES_IN_FLIGHT];
	int current; // slot being recorded, -1 outside a profiled frame
	vk_profile_stats_t fast, slow;
	double max_total;
} vk_prof = { .current = -1 };

static void VK_ProfileReset_f( void )
{
	memset( &vk_prof.fast, 0, sizeof( vk_prof.fast ));
	memset( &vk_prof.slow, 0, sizeof( vk_prof.slow ));
	vk_prof.max_total = 0.0;
}

static void VK_ProfilePrintStats( const char *title, const vk_profile_stats_t *st )
{
	if( !st->frames )
	{
		gEngfuncs.Con_Printf( "%s: no frame\n", title );
		return;
	}

	gEngfuncs.Con_Printf( "%s: %d frames, %.3f ms on average\n", title, st->frames, st->total / st->frames );

	for( int i = 0; i < VK_PROF_COUNT; i++ )
		gEngfuncs.Con_Printf( "  %-15s %7.3f ms  (max %.3f)\n", vk_profile_names[i], st->sum[i] / st->frames, st->max[i] );
}

static void VK_ProfilePrint_f( void )
{
	gEngfuncs.Con_Printf( "GPU profile, slow frames over %.2f ms, slowest %.3f ms\n", r_vk_gpu_profile_slow.value, vk_prof.max_total );
	VK_ProfilePrintStats( "other frames", &vk_prof.fast );
	VK_ProfilePrintStats( "slow frames", &vk_prof.slow );
}

// before R_Init_Video, which executes vulkan.cfg
void VK_RegisterProfileCvars( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_vk_gpu_profile );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_gpu_profile_slow );
	gEngfuncs.Cmd_AddCommand( "r_vk_gpu_profile_print", VK_ProfilePrint_f, "print the GPU time of each part of the frame" );
	gEngfuncs.Cmd_AddCommand( "r_vk_gpu_profile_reset", VK_ProfileReset_f, "start the GPU profile over" );
}

void VK_InitProfile( void )
{
	if( !vk.props.limits.timestampComputeAndGraphics )
		return;

	VkQueryPoolCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = VK_PROFILE_MAX_MARKS * VK_FRAMES_IN_FLIGHT,
	};

	if( vkCreateQueryPool( vk.device, &info, NULL, &vk_prof.pool ) != VK_SUCCESS )
		vk_prof.pool = VK_NULL_HANDLE;
}

void VK_ShutdownProfile( void )
{
	if( r_vk_gpu_profile.value && ( vk_prof.fast.frames || vk_prof.slow.frames ))
		VK_ProfilePrint_f();

	if( vk_prof.pool )
	{
		vkDeviceWaitIdle( vk.device );
		vkDestroyQueryPool( vk.device, vk_prof.pool, NULL );
	}

	vk_prof.pool = VK_NULL_HANDLE;
	gEngfuncs.Cmd_RemoveCommand( "r_vk_gpu_profile_print" );
	gEngfuncs.Cmd_RemoveCommand( "r_vk_gpu_profile_reset" );
}

static void VK_ProfileAccumulate( vk_profile_stats_t *st, const double *parts, double total )
{
	for( int i = 0; i < VK_PROF_COUNT; i++ )
	{
		st->sum[i] += parts[i];
		st->max[i] = Q_max( st->max[i], parts[i] );
	}

	st->total += total;
	st->frames++;
}

/*
=================
VK_ProfileBeginFrame

the slot's previous frame is complete (its fence was waited for): reads its
timestamps, then starts the new frame's marks in cmd
=================
*/
void VK_ProfileBeginFrame( VkCommandBuffer cmd, int slot )
{
	vk_profile_frame_t *frame = &vk_prof.frames[slot];
	const uint32_t first = slot * VK_PROFILE_MAX_MARKS;
	uint64_t ts[VK_PROFILE_MAX_MARKS];

	if( frame->recorded && frame->num_marks >= 2 && vkGetQueryPoolResults( vk.device, vk_prof.pool, first, frame->num_marks,
		sizeof( ts ), ts, sizeof( ts[0] ), VK_QUERY_RESULT_64_BIT ) == VK_SUCCESS )
	{
		const double to_ms = vk.props.limits.timestampPeriod * 1e-6;
		double parts[VK_PROF_COUNT] = { 0 };
		const double total = ( ts[frame->num_marks - 1] - ts[0] ) * to_ms;

		for( int i = 0; i < frame->num_marks - 1; i++ )
			parts[frame->marks[i]] += ( ts[i + 1] - ts[i] ) * to_ms;

		VK_ProfileAccumulate( total > r_vk_gpu_profile_slow.value ? &vk_prof.slow : &vk_prof.fast, parts, total );
		vk_prof.max_total = Q_max( vk_prof.max_total, total );
	}

	frame->recorded = false;
	frame->num_marks = 0;
	vk_prof.current = -1;

	if( !vk_prof.pool || !r_vk_gpu_profile.value )
		return;

	vkCmdResetQueryPool( cmd, vk_prof.pool, first, VK_PROFILE_MAX_MARKS );
	vk_prof.current = slot;
	frame->recorded = true;
	VK_ProfileMark( cmd, VK_PROF_FRAME_START );
}

/*
=================
VK_ProfileMark

the GPU enters the given part once everything recorded before is done
=================
*/
void VK_ProfileMark( VkCommandBuffer cmd, vk_profile_part_t part )
{
	vk_profile_frame_t *frame;

	if( vk_prof.current < 0 || !cmd )
		return;

	frame = &vk_prof.frames[vk_prof.current];
	if( frame->num_marks >= VK_PROFILE_MAX_MARKS )
		return;

	VK_ImmFlush(); // the batched draws belong to the part being left

	vkCmdWriteTimestamp( cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vk_prof.pool,
		vk_prof.current * VK_PROFILE_MAX_MARKS + frame->num_marks );
	frame->marks[frame->num_marks++] = part;
}

void VK_ProfileEndFrame( VkCommandBuffer cmd )
{
	// closes the present copy; the part of the last mark never counts
	VK_ProfileMark( cmd, VK_PROF_PRESENT_COPY );
	vk_prof.current = -1;
}
