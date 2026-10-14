/*
vk_rtfx.c - ray traced shadows, occlusion and reflections
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
With r_vk_quality rt, on a GPU with ray queries, the HDR scene of the main
view goes through these passes before the post-processing of vk_post.c,
tracing rays through the scene vk_rt.c built for the frame:

  visibility  online only: studio models the eye doesn't see past the
              world are marked hidden, they cast and reflect nothing
              (rt_visibility.comp)
  effects     HDR scene -> HDR scene with shadows and reflections, and the
              occlusion of the frame accumulated with the previous ones
              in a history image (rt_effects.comp)
  occlusion   history -> occlusion the tonemap multiplies the scene with,
              in place of SSAO (rt_ao_blur.comp)

GoldSrc's light is baked into the lightmaps, shadows of the world
included: the rays don't light the scene again, they take out of the
lightmaps the light of the sun and of the dynamic lights an entity stands
in front of, the shadows the bake couldn't know. Everything else stays
the classic lighting, which online play must keep: see Documentation/ref-vk.md.

The history images are two, one written and one read each frame. A server
can keep its players from all this with the physinfo key maxlook (vk_post.c).
*/

#include "vk_local.h"

static const uint32_t rt_effects_spv[] =
#include "shaders/rt_effects.comp.inc"
;
static const uint32_t rt_ao_blur_spv[] =
#include "shaders/rt_ao_blur.comp.inc"
;
static const uint32_t rt_visibility_spv[] =
#include "shaders/rt_visibility.comp.inc"
;

CVAR_DEFINE_AUTO( r_vk_rt_shadows, "1", FCVAR_GLCONFIG, "r_vk_quality rt: shadows of entities in the light of the sun and of dynamic lights" );
CVAR_DEFINE_AUTO( r_vk_rt_ao, "1", FCVAR_GLCONFIG, "r_vk_quality rt: ray traced ambient occlusion strength, 0 disables it" );
CVAR_DEFINE_AUTO( r_vk_rt_ao_radius, "32", FCVAR_GLCONFIG, "r_vk_quality rt: ambient occlusion radius in world units" );
CVAR_DEFINE_AUTO( r_vk_rt_reflections, "1", FCVAR_GLCONFIG, "r_vk_quality rt: reflections on water, glass and reflective materials, 0 disables them" );
CVAR_DEFINE_AUTO( r_vk_rt_sun, "1", FCVAR_GLCONFIG, "r_vk_quality rt: part of movevars skycolor the sun puts in the lightmaps" );
CVAR_DEFINE_AUTO( r_vk_rt_shadow_keep, "0.3", FCVAR_GLCONFIG, "r_vk_quality rt: least part of its light a shadowed surface keeps" );

#define FX_SHADOWS     1
#define FX_AO          2
#define FX_REFLECTIONS 4
#define FX_HISTORY     8

#define VK_RTFX_HISTORY_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define VK_RTFX_AO_FORMAT      VK_FORMAT_R32_SFLOAT

typedef enum
{
	VK_RTFX_VISIBILITY = 0,
	VK_RTFX_EFFECTS,
	VK_RTFX_AO_BLUR,
	VK_RTFX_PASSES
} vk_rtfx_pass_t;

// mirrors rtfx_common.glsl
typedef struct
{
	float inv_view_proj[16];
	float prev_view_proj[16];
	float eye[4];
	float forward[4];
	float prev_eye[4];
	float ao[4];
	float refl[4];
	float shadow[4];
	float light[4];
	int misc[4];
	int rect[4];
} vk_rtfx_params_t;

typedef struct
{
	VkDeviceAddress scene;
	VkDeviceAddress params;
	int iparams[4];
} vk_rtfx_push_t;

static struct
{
	qboolean ready;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool pool;
	VkPipelineLayout layout;
	VkPipeline pipes[VK_RTFX_PASSES];
	VkDescriptorSet sets[VK_FRAMES_IN_FLIGHT][VK_RTFX_PASSES];
	VkSampler nearest;

	// the previous frame, for the history
	qboolean have_prev;
	matrix4x4 prev_view_proj;
	vec3_t prev_eye;
	int prev_generation;
	int prev_rect[4];
	uint64_t frame;
} rtfx;

void VK_RegisterRTEffectsCvars( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt_shadows );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt_ao );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt_ao_radius );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt_reflections );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt_sun );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt_shadow_keep );
}

// r_vk_quality rt on a device that can, unless the server says otherwise
qboolean VK_RTEffectsWanted( void )
{
	return rtfx.ready && VK_RTAvailable() && VK_PostLookRT();
}

/*
=================
Lifetime
=================
*/
static VkPipeline VK_RTFXPipeline( const uint32_t *code, size_t size, const char *name )
{
	VkShaderModule module = VK_CreateShader( code, size, name );
	VkPipeline pipeline = VK_NULL_HANDLE;

	if( !module )
		return VK_NULL_HANDLE;

	VkComputePipelineCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = module,
			.pName = "main",
		},
		.layout = rtfx.layout,
	};

	if( vkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline ) != VK_SUCCESS )
		pipeline = VK_NULL_HANDLE;
	else VK_SetObjectName( VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, "%s", name );

	vkDestroyShaderModule( vk.device, module, NULL );
	return pipeline;
}

qboolean VK_InitRTEffects( void )
{
	memset( &rtfx, 0, sizeof( rtfx ));

	if( !VK_RTAvailable( ))
		return true; // nothing to do on this device

	VkDescriptorBindingFlags flags[3] = {
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = 3,
		.pBindingFlags = flags,
	};

	// the scene, the sampled inputs, the written images
	VkDescriptorSetLayoutBinding bindings[3] = {
		{ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, VK_SHADER_STAGE_COMPUTE_BIT },
	};

	VkDescriptorSetLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &binding_flags,
		.bindingCount = 3,
		.pBindings = bindings,
	};

	const uint32_t num_sets = VK_FRAMES_IN_FLIGHT * VK_RTFX_PASSES;
	VkDescriptorPoolSize sizes[3] = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, num_sets },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, num_sets * 4 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, num_sets * 2 },
	};

	VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = num_sets,
		.poolSizeCount = 3,
		.pPoolSizes = sizes,
	};

	if( vkCreateDescriptorSetLayout( vk.device, &layout, NULL, &rtfx.set_layout ) != VK_SUCCESS
		|| vkCreateDescriptorPool( vk.device, &pool, NULL, &rtfx.pool ) != VK_SUCCESS )
		return false;

	VkDescriptorSetLayout set_layouts[2] = { vk_pipe.texture_layout, rtfx.set_layout };
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( vk_rtfx_push_t ) };
	VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push,
	};

	if( vkCreatePipelineLayout( vk.device, &pipeline_layout, NULL, &rtfx.layout ) != VK_SUCCESS )
		return false;

	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
	{
		VkDescriptorSetLayout layouts[VK_RTFX_PASSES];

		for( int j = 0; j < VK_RTFX_PASSES; j++ )
			layouts[j] = rtfx.set_layout;

		VkDescriptorSetAllocateInfo alloc = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = rtfx.pool,
			.descriptorSetCount = VK_RTFX_PASSES,
			.pSetLayouts = layouts,
		};

		if( vkAllocateDescriptorSets( vk.device, &alloc, rtfx.sets[i] ) != VK_SUCCESS )
			return false;
	}

	rtfx.pipes[VK_RTFX_VISIBILITY] = VK_RTFXPipeline( rt_visibility_spv, sizeof( rt_visibility_spv ), "rt_visibility.comp" );
	rtfx.pipes[VK_RTFX_EFFECTS] = VK_RTFXPipeline( rt_effects_spv, sizeof( rt_effects_spv ), "rt_effects.comp" );
	rtfx.pipes[VK_RTFX_AO_BLUR] = VK_RTFXPipeline( rt_ao_blur_spv, sizeof( rt_ao_blur_spv ), "rt_ao_blur.comp" );

	for( int i = 0; i < VK_RTFX_PASSES; i++ )
	{
		if( !rtfx.pipes[i] )
			return false;
	}

	VkSamplerCreateInfo sampler = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};

	if( vkCreateSampler( vk.device, &sampler, NULL, &rtfx.nearest ) != VK_SUCCESS )
		return false;

	rtfx.ready = true;
	return true;
}

void VK_ShutdownRTEffects( void )
{
	if( !vk.device )
		return;

	vkDeviceWaitIdle( vk.device );

	for( int i = 0; i < VK_RTFX_PASSES; i++ )
	{
		if( rtfx.pipes[i] )
			vkDestroyPipeline( vk.device, rtfx.pipes[i], NULL );
	}

	if( rtfx.layout )
		vkDestroyPipelineLayout( vk.device, rtfx.layout, NULL );
	if( rtfx.pool )
		vkDestroyDescriptorPool( vk.device, rtfx.pool, NULL );
	if( rtfx.set_layout )
		vkDestroyDescriptorSetLayout( vk.device, rtfx.set_layout, NULL );
	if( rtfx.nearest )
		vkDestroySampler( vk.device, rtfx.nearest, NULL );

	memset( &rtfx, 0, sizeof( rtfx ));
}

/*
=================
Passes
=================
*/
typedef struct
{
	vk_image_t *img;
	VkImageView view; // img->view unless set
} vk_rtfx_image_t;

// this frame slot's set of a pass: the GPU is done with its previous frame
static VkDescriptorSet VK_RTFXSet( VkCommandBuffer cmd, vk_rtfx_pass_t pass, VkAccelerationStructureKHR tlas,
	const vk_rtfx_image_t *inputs, int num_inputs, vk_image_t *const *outputs, int num_outputs )
{
	VkDescriptorSet set = rtfx.sets[VK_FrameIndex()][pass];
	VkWriteDescriptorSetAccelerationStructureKHR as_info = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &tlas,
	};
	VkDescriptorImageInfo sampled[4], storage[2];
	VkWriteDescriptorSet writes[3];
	int num_writes = 0;

	writes[num_writes++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = &as_info,
		.dstSet = set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR };

	for( int i = 0; i < num_inputs; i++ )
	{
		const qboolean depth = FBitSet( inputs[i].img->aspect, VK_IMAGE_ASPECT_DEPTH_BIT );

		VK_GraphUse( cmd, inputs[i].img, VK_USE_SAMPLED );
		sampled[i] = (VkDescriptorImageInfo){ rtfx.nearest, inputs[i].view ? inputs[i].view : inputs[i].img->view,
			depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	}

	if( num_inputs )
	{
		writes[num_writes++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
			.dstBinding = 1, .descriptorCount = num_inputs, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = sampled };
	}

	for( int i = 0; i < num_outputs; i++ )
	{
		VK_GraphUse( cmd, outputs[i], VK_USE_STORAGE );
		storage[i] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, outputs[i]->view, VK_IMAGE_LAYOUT_GENERAL };
	}

	if( num_outputs )
	{
		writes[num_writes++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
			.dstBinding = 2, .descriptorCount = num_outputs, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = storage };
	}

	vkUpdateDescriptorSets( vk.device, num_writes, writes, 0, NULL );
	return set;
}

static void VK_RTFXDispatch( VkCommandBuffer cmd, vk_rtfx_pass_t pass, VkDescriptorSet set, const vk_rtfx_push_t *push,
	uint32_t groups_x, uint32_t groups_y )
{
	VkDescriptorSet sets[2] = { vk_pipe.texture_set, set };

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rtfx.pipes[pass] );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rtfx.layout, 0, 2, sets, 0, NULL );
	vkCmdPushConstants( cmd, rtfx.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( *push ), push );
	vkCmdDispatch( cmd, groups_x, groups_y, 1 );
}

// the pass marking hidden models wrote the instance table the next ones read
static void VK_RTFXBufferBarrier( VkCommandBuffer cmd )
{
	VkMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
	};

	VkDependencyInfo dep = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2( cmd, &dep );
}

/*
=================
VK_RTEffects

the main view's HDR scene (x, y, w, h top down inside the targets) through
the ray traced effects: out_scene is the scene to post-process, out_ao the
occlusion to use in place of SSAO (NULL when off). False when there is
nothing to do: the scene stays as drawn.
=================
*/
qboolean VK_RTEffects( VkCommandBuffer cmd, vk_image_t *scene, int x, int y, int w, int h, vk_image_t **out_scene, vk_image_t **out_ao )
{
	const VkImageUsageFlags storage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VkAccelerationStructureKHR tlas;
	VkDeviceAddress scene_addr, params_addr;
	vk_image_t *result, *history[2], *ao, *depth = VK_MainDepthImage();
	vk_rtfx_params_t *params;
	vk_rtfx_push_t push = { 0 };
	matrix4x4 mvp, inv;
	VkBuffer buffer;
	VkDeviceSize offset;
	int num_instances, flags = 0, cur;

	*out_scene = scene;
	*out_ao = NULL;

	if( !VK_RTEffectsWanted( ) || !VK_RTFrame( &tlas, &scene_addr, &num_instances ))
	{
		rtfx.have_prev = false;
		return false;
	}

	if( r_vk_rt_shadows.value )
		SetBits( flags, FX_SHADOWS );
	if( r_vk_rt_ao.value > 0.0f )
		SetBits( flags, FX_AO );
	if( r_vk_rt_reflections.value > 0.0f )
		SetBits( flags, FX_REFLECTIONS );

	if( !flags )
	{
		rtfx.have_prev = false;
		return false;
	}

	cur = rtfx.frame & 1;
	result = VK_GraphImage( "rt scene", VK_HDR_COLOR_FORMAT, storage, 0 );
	history[0] = VK_GraphImage( "rt history 0", VK_RTFX_HISTORY_FORMAT, storage, 0 );
	history[1] = VK_GraphImage( "rt history 1", VK_RTFX_HISTORY_FORMAT, storage, 0 );
	ao = VK_GraphImage( "rt occlusion", VK_RTFX_AO_FORMAT, storage, 0 );
	params = VK_DynamicAlloc( sizeof( *params ), 16, &buffer, &offset );

	if( !result || !history[0] || !history[1] || !ao || !params )
		return false;

	params_addr = VK_BufferAddress( buffer ) + offset;

	// the previous frame's history, when it is of this view
	if( rtfx.have_prev && rtfx.prev_generation == VK_GraphGeneration() && history[cur ^ 1]->layout != VK_IMAGE_LAYOUT_UNDEFINED
		&& rtfx.prev_rect[0] == x && rtfx.prev_rect[1] == y && rtfx.prev_rect[2] == w && rtfx.prev_rect[3] == h )
		SetBits( flags, FX_HISTORY );

	memset( params, 0, sizeof( *params ));
	Matrix4x4_Concat( mvp, RI.projectionMatrix, RI.worldviewMatrix );
	Matrix4x4_Invert_Full( inv, mvp );
	Matrix4x4_ToArrayFloatGL( inv, params->inv_view_proj );
	Matrix4x4_ToArrayFloatGL( FBitSet( flags, FX_HISTORY ) ? rtfx.prev_view_proj : mvp, params->prev_view_proj );
	VectorCopy( RI.rvp.vieworigin, params->eye );
	params->eye[3] = 4.0f; // R_SetupProjectionMatrix
	VectorCopy( RI.vforward, params->forward );
	params->forward[3] = Q_max( 256.0f, RI.farClip );
	VectorCopy( FBitSet( flags, FX_HISTORY ) ? rtfx.prev_eye : RI.rvp.vieworigin, params->prev_eye );
	params->ao[0] = bound( 4.0f, r_vk_rt_ao_radius.value, 256.0f );
	params->ao[1] = bound( 0.0f, r_vk_rt_ao.value, 4.0f );
	params->refl[0] = bound( 0.0f, r_vk_rt_reflections.value, 4.0f );
	params->refl[1] = (float)gp_cl->time;
	params->shadow[0] = Q_max( 0.0f, r_vk_rt_sun.value );
	params->shadow[1] = bound( 0.0f, r_vk_rt_shadow_keep.value, 1.0f );
	params->light[0] = gl_overbright.value ? 2.0f : 1.0f; // the lightmap pass's blending
	params->light[1] = gl_overbright.value ? 128.0f / 192.0f : 1.0f;
	params->misc[0] = flags;
	params->misc[1] = (int)( rtfx.frame & 0x7fffffff );
	params->misc[2] = gp_cl->maxclients > 1;
	params->rect[0] = x;
	params->rect[1] = y;
	params->rect[2] = w;
	params->rect[3] = h;

	push.scene = scene_addr;
	push.params = params_addr;

	VK_BeginLabel( cmd, "rt effects" );

	// online: models out of sight are left out of every ray
	if( gp_cl->maxclients > 1 && num_instances > 0 )
	{
		VkDescriptorSet set = VK_RTFXSet( cmd, VK_RTFX_VISIBILITY, tlas, NULL, 0, NULL, 0 );

		VK_RTFXDispatch( cmd, VK_RTFX_VISIBILITY, set, &push, ( num_instances + 63 ) / 64, 1 );
		VK_RTFXBufferBarrier( cmd );
	}

	{
		// nothing to read yet: the scene stands in for the history, unread
		vk_rtfx_image_t inputs[3] = {
			{ scene, VK_NULL_HANDLE },
			{ depth, VK_MainDepthSampleView() },
			{ FBitSet( flags, FX_HISTORY ) ? history[cur ^ 1] : scene, VK_NULL_HANDLE },
		};
		vk_image_t *outputs[2] = { result, history[cur] };
		VkDescriptorSet set = VK_RTFXSet( cmd, VK_RTFX_EFFECTS, tlas, inputs, 3, outputs, 2 );

		VK_RTFXDispatch( cmd, VK_RTFX_EFFECTS, set, &push, ( w + 7 ) / 8, ( h + 7 ) / 8 );
	}

	if( FBitSet( flags, FX_AO ))
	{
		vk_rtfx_image_t inputs[1] = { { history[cur], VK_NULL_HANDLE } };
		vk_image_t *outputs[1] = { ao };
		VkDescriptorSet set = VK_RTFXSet( cmd, VK_RTFX_AO_BLUR, tlas, inputs, 1, outputs, 1 );

		VK_RTFXDispatch( cmd, VK_RTFX_AO_BLUR, set, &push, ( w + 7 ) / 8, ( h + 7 ) / 8 );
		*out_ao = ao;
	}

	VK_EndLabel( cmd );

	Matrix4x4_Copy( rtfx.prev_view_proj, mvp );
	VectorCopy( RI.rvp.vieworigin, rtfx.prev_eye );
	rtfx.prev_generation = VK_GraphGeneration();
	rtfx.prev_rect[0] = x;
	rtfx.prev_rect[1] = y;
	rtfx.prev_rect[2] = w;
	rtfx.prev_rect[3] = h;
	rtfx.have_prev = FBitSet( flags, FX_AO ) != 0;
	rtfx.frame++;

	*out_scene = result;
	return true;
}
