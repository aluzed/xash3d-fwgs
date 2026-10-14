/*
vk_post.c - HDR scene and post-processing
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
With r_vk_quality classic (the default) nothing here runs: the scene is
drawn into the main RGBA8 target exactly as ref_gl draws it.

Otherwise the 3D view of each frame is drawn into an HDR scene target
(VK_HDR_COLOR_FORMAT) sharing the main depth, with pipelines that write
linear colours (output_common.glsl). When the view is done, compute passes
turn it into the main target's pixels, inside the view rectangle only:

  SSAO       depth -> half size linear depth -> ao_raw -> (depth aware
             blur) -> ao, at half size, upsampled by the tonemap
  bloom      scene -> bloom[0] -> ... -> bloom[n-1] (13 tap downsamples),
             then back up, each level adding the tent filtered one below
  exposure   scene -> per group log luminance -> adapted exposure (buffer)
  tonemap    scene * ao + bloom, exposed, tonemapped, gamma encoded -> ldr
  FXAA       ldr -> ldr, or SMAA: ldr -> edges -> weights -> ldr
  sharpen    ldr -> ldr (contrast adaptive)
  copy       ldr -> main target

The 2D (HUD, menus, console) is then drawn over the main target as usual.

The passes run through a minimal frame graph: each one names the images it
samples and the one it writes, and VK_PostRun puts the barriers and layout
transitions between them, and caches one descriptor set per combination
of images (they only change with the target size). Sampled images are in
read only layouts, written ones in GENERAL: the depth and the scene stay
compressed when read.
*/

#include "vk_local.h"
#include "AreaTex.h"   // 3rdparty/smaa/Textures
#include "SearchTex.h"

// SPIR-V compiled by glslc at build time (see wscript)
static const uint32_t post_ssao_depth_spv[] =
#include "shaders/post_ssao_depth.comp.inc"
;
static const uint32_t post_ssao_spv[] =
#include "shaders/post_ssao.comp.inc"
;
static const uint32_t post_ssao_blur_spv[] =
#include "shaders/post_ssao_blur.comp.inc"
;
static const uint32_t post_bloom_down_spv[] =
#include "shaders/post_bloom_down.comp.inc"
;
static const uint32_t post_bloom_up_spv[] =
#include "shaders/post_bloom_up.comp.inc"
;
static const uint32_t post_luminance_spv[] =
#include "shaders/post_luminance.comp.inc"
;
static const uint32_t post_exposure_spv[] =
#include "shaders/post_exposure.comp.inc"
;
static const uint32_t post_tonemap_spv[] =
#include "shaders/post_tonemap.comp.inc"
;
static const uint32_t post_fxaa_spv[] =
#include "shaders/post_fxaa.comp.inc"
;
static const uint32_t post_smaa_edges_spv[] =
#include "shaders/post_smaa_edges.comp.inc"
;
static const uint32_t post_smaa_weights_spv[] =
#include "shaders/post_smaa_weights.comp.inc"
;
static const uint32_t post_smaa_blend_spv[] =
#include "shaders/post_smaa_blend.comp.inc"
;
static const uint32_t post_sharpen_spv[] =
#include "shaders/post_sharpen.comp.inc"
;

CVAR_DEFINE_AUTO( r_vk_quality, "classic", FCVAR_GLCONFIG, "rendering look: classic (as ref_gl), enhanced (HDR and post-processing), rt (ray tracing, enhanced until it exists)" );
CVAR_DEFINE_AUTO( r_vk_tonemap, "3", FCVAR_GLCONFIG, "HDR tonemapper: 0 clamp, 1 ACES, 2 AgX, 3 neutral (classic colours, soft highlights)" );
CVAR_DEFINE_AUTO( r_vk_exposure, "0", FCVAR_GLCONFIG, "exposure compensation in EV (stops), added to the automatic exposure" );
CVAR_DEFINE_AUTO( r_vk_autoexposure, "1", FCVAR_GLCONFIG, "adapt the exposure to the scene brightness like an eye" );
CVAR_DEFINE_AUTO( r_vk_autoexposure_range, "1", FCVAR_GLCONFIG, "how far the automatic exposure may go, in EV either way" );
CVAR_DEFINE_AUTO( r_vk_autoexposure_speed, "1.5", FCVAR_GLCONFIG, "how fast the automatic exposure adapts" );
CVAR_DEFINE_AUTO( r_vk_bloom, "0.5", FCVAR_GLCONFIG, "bloom intensity, 0 disables it" );
CVAR_DEFINE_AUTO( r_vk_bloom_threshold, "1", FCVAR_GLCONFIG, "linear brightness above which pixels bloom" );
CVAR_DEFINE_AUTO( r_vk_ssao, "1", FCVAR_GLCONFIG, "screen space ambient occlusion strength, 0 disables it" );
CVAR_DEFINE_AUTO( r_vk_ssao_radius, "24", FCVAR_GLCONFIG, "screen space ambient occlusion radius in world units" );
CVAR_DEFINE_AUTO( r_vk_aa, "2", FCVAR_GLCONFIG, "antialiasing of the 3D view: 0 none, 1 FXAA, 2 SMAA" );
CVAR_DEFINE_AUTO( r_vk_sharpen, "0.3", FCVAR_GLCONFIG, "contrast adaptive sharpening of the 3D view, 0 disables it" );

#define VK_POST_GROUP         8   // local size of every pass but the exposure one
#define VK_POST_LUM_TILE      32  // pixels of a luminance group side
#define VK_POST_MAX_BLOOM     6
#define VK_POST_MAX_INPUTS    4
#define VK_POST_MAX_SETS      64
#define VK_POST_AO_FORMAT     VK_FORMAT_R32_SFLOAT
#define VK_POST_LDR_FORMAT    VK_FORMAT_R8G8B8A8_UNORM
#define VK_POST_EXPOSURE_HEAD 16  // bytes before the per group sums

// the luminance the automatic exposure brings the view to: the log average
// of HL and CS views in the classic look goes from 0.005 (dark corridors)
// to 0.17 (dust2 in the sun), 0.05 is what most of them have
#define VK_POST_EXPOSURE_KEY  0.05f

// mirrors post_common.glsl
typedef struct
{
	int rect[4];        // pixels written: x, y, width, height
	int iparams[4];
	float params[4];
	float params2[4];
	float src_rect[4];  // texture coordinates the sources are clamped to
	float texel[4];     // xy: 1 / size of input 0, zw: 1 / size of the output
} vk_post_push_t;

typedef enum
{
	VK_POST_SSAO_DEPTH = 0,
	VK_POST_SSAO,
	VK_POST_SSAO_BLUR,
	VK_POST_BLOOM_DOWN,
	VK_POST_BLOOM_UP,
	VK_POST_LUMINANCE,
	VK_POST_EXPOSURE,
	VK_POST_TONEMAP,
	VK_POST_FXAA,
	VK_POST_SMAA_EDGES,
	VK_POST_SMAA_WEIGHTS,
	VK_POST_SMAA_BLEND,
	VK_POST_SHARPEN,
	VK_POST_PIPELINES
} vk_post_pipe_t;

// r_vk_aa
#define VK_POST_AA_NONE 0
#define VK_POST_AA_FXAA 1
#define VK_POST_AA_SMAA 2

// what the tonemap pass mixes in (post_tonemap.comp)
#define VK_POST_USE_BLOOM    1
#define VK_POST_USE_AO       2
#define VK_POST_USE_EXPOSURE 4

typedef struct
{
	vk_image_t *img;
	VkImageView view; // img->view unless set
	VkSampler sampler;
} vk_post_input_t;

typedef struct
{
	const char *name;
	vk_post_pipe_t pipe;
	vk_post_input_t inputs[VK_POST_MAX_INPUTS];
	vk_image_t *output;
	qboolean buffer;   // reads or writes the exposure buffer
	uint32_t groups[2];
	vk_post_push_t push;
} vk_post_pass_t;

typedef struct
{
	VkImageView views[VK_POST_MAX_INPUTS];
	VkSampler samplers[VK_POST_MAX_INPUTS];
	VkImageView output;
	qboolean buffer;
	VkDescriptorSet set;
} vk_post_set_t;

// the settings a frame runs with, read from the cvars once per frame
typedef struct
{
	int tonemap;
	float exposure;
	qboolean autoexposure;
	float ev_min, ev_max;
	float speed;
	float bloom, bloom_threshold;
	float ssao, ssao_radius;
	int aa; // VK_POST_AA_*
	float sharpen;
} vk_post_settings_t;

static struct
{
	qboolean enabled; // r_vk_quality isn't classic, and everything was created
	qboolean failed;  // creation failed: classic until the quality changes
	qboolean rt_warned;
	char quality[32]; // r_vk_quality the state was decided for
	vk_post_settings_t s;

	uint32_t width, height; // of the targets
	vk_image_t scene;
	vk_image_t lindepth, ao_raw, ao; // half size
	vk_image_t bloom[VK_POST_MAX_BLOOM];
	int num_bloom;
	vk_image_t ldr[2];
	vk_image_t smaa_edges, smaa_weights;
	vk_image_t smaa_area, smaa_search; // precomputed, created with the renderer
	vk_buffer_t exposure;
	uint32_t max_lum_groups;
	qboolean exposure_reset; // no adapted exposure to start from

	VkSampler linear, nearest;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout layout;
	VkDescriptorPool pool;
	vk_post_set_t sets[VK_POST_MAX_SETS];
	int num_sets;
	VkPipeline pipes[VK_POST_PIPELINES];

	// frame graph state of the pass chain being recorded
	vk_image_t *touched[32]; // images used since the last barrier on them
	qboolean written[32];
	int num_touched;
	qboolean buffer_first; // the buffer wasn't used by the chain yet

	qboolean in_scene; // between VK_PostBeginScene and VK_PostEndScene
	uint64_t exposure_frame; // last frame that adapted the exposure
} post;

void VK_RegisterPostCvars( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_vk_quality );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_tonemap );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_exposure );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_autoexposure );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_autoexposure_range );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_autoexposure_speed );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_bloom );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_bloom_threshold );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_ssao );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_ssao_radius );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_aa );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_sharpen );
}

/*
=================
Pipelines and descriptors, created with the renderer
=================
*/
static VkSampler VK_PostCreateSampler( VkFilter filter, const char *name )
{
	VkSampler sampler;

	VkSamplerCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = filter,
		.minFilter = filter,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.maxLod = 0.0f,
	};

	if( vkCreateSampler( vk.device, &info, NULL, &sampler ) != VK_SUCCESS )
		return VK_NULL_HANDLE;

	VK_SetObjectName( VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "%s", name );
	return sampler;
}

static VkPipeline VK_PostCreatePipeline( const uint32_t *code, size_t size, const char *name )
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
		.layout = post.layout,
	};

	if( vkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline ) != VK_SUCCESS )
		pipeline = VK_NULL_HANDLE;
	else VK_SetObjectName( VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, "%s", name );

	vkDestroyShaderModule( vk.device, module, NULL );
	return pipeline;
}

// an image with its pixels, through the texture upload commands
static qboolean VK_PostCreateTexture( vk_image_t *img, const char *name, uint32_t width, uint32_t height,
	VkFormat format, const byte *data, size_t size )
{
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;
	byte *dst;

	if( !VK_CreateImage( img, name, width, height, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT ))
		return false;

	dst = VK_UploadStaging( size, &staging, &offset, &cmd );
	if( !dst )
		return false;

	memcpy( dst, data, size );

	VkBufferImageCopy copy = {
		.bufferOffset = offset,
		.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.imageExtent = { width, height, 1 },
	};

	VK_ImageBarrier( cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	vkCmdCopyBufferToImage( cmd, staging, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
	VK_ImageBarrier( cmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	return true;
}

qboolean VK_InitPost( void )
{
	memset( &post, 0, sizeof( post ));

	post.linear = VK_PostCreateSampler( VK_FILTER_LINEAR, "post linear" );
	post.nearest = VK_PostCreateSampler( VK_FILTER_NEAREST, "post nearest" );

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

	// the sampled inputs, the output image, the exposure buffer
	VkDescriptorSetLayoutBinding bindings[3] = {
		{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_POST_MAX_INPUTS, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
	};

	VkDescriptorSetLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &binding_flags,
		.bindingCount = 3,
		.pBindings = bindings,
	};

	VkDescriptorPoolSize sizes[3] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_POST_MAX_SETS * VK_POST_MAX_INPUTS },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_POST_MAX_SETS },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_POST_MAX_SETS },
	};

	VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = VK_POST_MAX_SETS,
		.poolSizeCount = 3,
		.pPoolSizes = sizes,
	};

	VkPushConstantRange push = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.size = sizeof( vk_post_push_t ),
	};

	if( !post.linear || !post.nearest
		|| vkCreateDescriptorSetLayout( vk.device, &layout, NULL, &post.set_layout ) != VK_SUCCESS
		|| vkCreateDescriptorPool( vk.device, &pool, NULL, &post.pool ) != VK_SUCCESS )
		return false;

	VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &post.set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push,
	};

	if( vkCreatePipelineLayout( vk.device, &pipeline_layout, NULL, &post.layout ) != VK_SUCCESS )
		return false;

#define POST_PIPE( id, spv ) post.pipes[id] = VK_PostCreatePipeline( spv, sizeof( spv ), #spv )
	POST_PIPE( VK_POST_SSAO_DEPTH, post_ssao_depth_spv );
	POST_PIPE( VK_POST_SSAO, post_ssao_spv );
	POST_PIPE( VK_POST_SSAO_BLUR, post_ssao_blur_spv );
	POST_PIPE( VK_POST_BLOOM_DOWN, post_bloom_down_spv );
	POST_PIPE( VK_POST_BLOOM_UP, post_bloom_up_spv );
	POST_PIPE( VK_POST_LUMINANCE, post_luminance_spv );
	POST_PIPE( VK_POST_EXPOSURE, post_exposure_spv );
	POST_PIPE( VK_POST_TONEMAP, post_tonemap_spv );
	POST_PIPE( VK_POST_FXAA, post_fxaa_spv );
	POST_PIPE( VK_POST_SMAA_EDGES, post_smaa_edges_spv );
	POST_PIPE( VK_POST_SMAA_WEIGHTS, post_smaa_weights_spv );
	POST_PIPE( VK_POST_SMAA_BLEND, post_smaa_blend_spv );
	POST_PIPE( VK_POST_SHARPEN, post_sharpen_spv );
#undef POST_PIPE

	for( int i = 0; i < VK_POST_PIPELINES; i++ )
	{
		if( !post.pipes[i] )
		{
			gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the post-processing pipelines\n" );
			return false;
		}
	}

	// rows top down, as SMAA's Direct3D 10 version loads them
	if( !VK_PostCreateTexture( &post.smaa_area, "SMAA area", AREATEX_WIDTH, AREATEX_HEIGHT, VK_FORMAT_R8G8_UNORM, areaTexBytes, AREATEX_SIZE )
		|| !VK_PostCreateTexture( &post.smaa_search, "SMAA search", SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_FORMAT_R8_UNORM, searchTexBytes, SEARCHTEX_SIZE ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the SMAA textures\n" );
		return false;
	}

	return true;
}

/*
=================
Targets, created when the look needs them and at each size change
=================
*/
static void VK_PostDestroyTargets( void )
{
	VK_DestroyImage( &post.scene );
	VK_DestroyImage( &post.lindepth );
	VK_DestroyImage( &post.ao_raw );
	VK_DestroyImage( &post.ao );
	VK_DestroyImage( &post.ldr[0] );
	VK_DestroyImage( &post.ldr[1] );
	VK_DestroyImage( &post.smaa_edges );
	VK_DestroyImage( &post.smaa_weights );
	for( int i = 0; i < VK_POST_MAX_BLOOM; i++ )
		VK_DestroyImage( &post.bloom[i] );
	VK_DestroyBuffer( &post.exposure );

	// every cached set pointed at the images
	if( post.pool )
		vkResetDescriptorPool( vk.device, post.pool, 0 );
	post.num_sets = 0;
	post.num_bloom = 0;
	post.width = post.height = 0;
}

static qboolean VK_PostCreateTargets( uint32_t width, uint32_t height )
{
	const VkImageUsageFlags storage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	const uint32_t half_width = ( width + 1 ) / 2, half_height = ( height + 1 ) / 2;
	qboolean ok;

	ok = VK_CreateImage( &post.scene, "HDR scene", width, height, VK_HDR_COLOR_FORMAT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT )
		&& VK_CreateImage( &post.lindepth, "SSAO depth", half_width, half_height, VK_POST_AO_FORMAT, storage, VK_IMAGE_ASPECT_COLOR_BIT )
		&& VK_CreateImage( &post.ao_raw, "SSAO raw", half_width, half_height, VK_POST_AO_FORMAT, storage, VK_IMAGE_ASPECT_COLOR_BIT )
		&& VK_CreateImage( &post.ao, "SSAO", half_width, half_height, VK_POST_AO_FORMAT, storage, VK_IMAGE_ASPECT_COLOR_BIT )
		&& VK_CreateImage( &post.ldr[0], "post LDR 0", width, height, VK_POST_LDR_FORMAT,
			storage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT )
		&& VK_CreateImage( &post.ldr[1], "post LDR 1", width, height, VK_POST_LDR_FORMAT,
			storage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT )
		// cleared on first use: outside the view nothing writes them
		&& VK_CreateImage( &post.smaa_edges, "SMAA edges", width, height, VK_POST_LDR_FORMAT,
			storage | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT )
		&& VK_CreateImage( &post.smaa_weights, "SMAA weights", width, height, VK_POST_LDR_FORMAT,
			storage | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT );

	// half size first, down to a few pixels
	uint32_t w = width, h = height;
	for( post.num_bloom = 0; ok && post.num_bloom < VK_POST_MAX_BLOOM; post.num_bloom++ )
	{
		char name[32];

		w = Q_max( w / 2, 1 );
		h = Q_max( h / 2, 1 );

		if( post.num_bloom >= 2 && ( w < 8 || h < 8 ))
			break;

		Q_snprintf( name, sizeof( name ), "bloom %d", post.num_bloom );
		ok = VK_CreateImage( &post.bloom[post.num_bloom], name, w, h, VK_HDR_COLOR_FORMAT, storage, VK_IMAGE_ASPECT_COLOR_BIT );
	}

	post.max_lum_groups = (( width + VK_POST_LUM_TILE - 1 ) / VK_POST_LUM_TILE ) * (( height + VK_POST_LUM_TILE - 1 ) / VK_POST_LUM_TILE );

	ok = ok && VK_CreateBuffer( &post.exposure, "exposure", VK_POST_EXPOSURE_HEAD + post.max_lum_groups * 2 * sizeof( float ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	if( !ok )
	{
		VK_PostDestroyTargets();
		return false;
	}

	post.width = width;
	post.height = height;
	post.exposure_reset = true;
	return true;
}

static void VK_PostDestroy( void )
{
	if( !post.width )
		return;

	vkDeviceWaitIdle( vk.device );
	VK_PostDestroyTargets();
}

void VK_ShutdownPost( void )
{
	if( !vk.device )
		return;

	vkDeviceWaitIdle( vk.device );
	VK_PostDestroyTargets();
	VK_DestroyImage( &post.smaa_area );
	VK_DestroyImage( &post.smaa_search );

	for( int i = 0; i < VK_POST_PIPELINES; i++ )
	{
		if( post.pipes[i] )
			vkDestroyPipeline( vk.device, post.pipes[i], NULL );
	}

	if( post.layout )
		vkDestroyPipelineLayout( vk.device, post.layout, NULL );
	if( post.pool )
		vkDestroyDescriptorPool( vk.device, post.pool, NULL );
	if( post.set_layout )
		vkDestroyDescriptorSetLayout( vk.device, post.set_layout, NULL );
	if( post.linear )
		vkDestroySampler( vk.device, post.linear, NULL );
	if( post.nearest )
		vkDestroySampler( vk.device, post.nearest, NULL );

	memset( &post, 0, sizeof( post ));
}

/*
=================
VK_PostBeginFrame

before the frame records anything: reads the settings, and creates or
frees what the look needs, waiting for the GPU when it does
=================
*/
static void VK_PostReadSettings( vk_post_settings_t *s )
{
	s->tonemap = bound( 0, (int)r_vk_tonemap.value, 3 );
	s->exposure = bound( -8.0f, r_vk_exposure.value, 8.0f );
	s->autoexposure = r_vk_autoexposure.value != 0.0f;
	s->ev_max = bound( 0.0f, r_vk_autoexposure_range.value, 4.0f );
	s->ev_min = -s->ev_max;
	s->speed = Q_max( r_vk_autoexposure_speed.value, 0.01f );
	s->bloom = Q_max( r_vk_bloom.value, 0.0f );
	s->bloom_threshold = Q_max( r_vk_bloom_threshold.value, 0.0f );
	s->ssao = bound( 0.0f, r_vk_ssao.value, 4.0f );
	s->ssao_radius = bound( 1.0f, r_vk_ssao_radius.value, 256.0f );
	s->aa = bound( VK_POST_AA_NONE, (int)r_vk_aa.value, VK_POST_AA_SMAA );
	s->sharpen = bound( 0.0f, r_vk_sharpen.value, 1.0f );

	// no seeing better in the dark than the others: online, the
	// exposure never goes above the classic one
	if( gp_cl->maxclients > 1 )
	{
		s->exposure = Q_min( s->exposure, 0.0f );
		s->ev_max = 0.0f;
	}
}

static qboolean VK_PostWanted( void )
{
	const char *quality = r_vk_quality.string;

	if( !Q_stricmp( quality, "classic" ))
		return false;

	if( !Q_stricmp( quality, "rt" ))
	{
		if( !post.rt_warned )
			gEngfuncs.Con_Printf( "ref_vk: r_vk_quality rt: no ray tracing yet, rendering as enhanced\n" );
		post.rt_warned = true;
		return true;
	}

	if( Q_stricmp( quality, "enhanced" ))
	{
		gEngfuncs.Con_Printf( "ref_vk: unknown r_vk_quality \"%s\", use classic, enhanced or rt\n", quality );
		gEngfuncs.Cvar_Set( r_vk_quality.name, "classic" );
		return false;
	}

	return true;
}

void VK_PostBeginFrame( void )
{
	int width, height;

	// a failure gives up until the quality setting changes
	if( Q_strcmp( post.quality, r_vk_quality.string ))
	{
		Q_strncpy( post.quality, r_vk_quality.string, sizeof( post.quality ));
		post.failed = false;
	}

	post.enabled = false;

	if( post.failed || !VK_PostWanted( ))
	{
		VK_PostDestroy(); // gives the memory back
		return;
	}

	VK_MainExtent( &width, &height );
	if( width <= 0 || height <= 0 )
		return;

	if( post.width != (uint32_t)width || post.height != (uint32_t)height )
	{
		VK_PostDestroy();

		if( !VK_CreateHDRPipelines( ) || !VK_PostCreateTargets( width, height ))
		{
			gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the HDR targets, back to the classic look\n" );
			post.failed = true;
			return;
		}
	}

	VK_PostReadSettings( &post.s );
	post.enabled = true;
}

/*
=================
VK_PostBeginScene

the view about to be drawn goes into the HDR scene when the look wants
it: the main 3D view, not cubemap shots or overviews
=================
*/
qboolean VK_PostBeginScene( void )
{
	VkCommandBuffer cmd = VK_FrameCommands();
	vec3_t clear;

	if( !post.enabled || !cmd || !FBitSet( RI.rvp.flags, RF_DRAW_WORLD )
		|| FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP|RF_DRAW_OVERVIEW|RF_ONLY_CLIENTDRAW )
		|| ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
		return false;

	VK_SetColorTarget( &post.scene, VK_TARGET_HDR );

	// what ref_gl's frame clear gave, linear; otherwise the last scene stays
	if( post.scene.layout == VK_IMAGE_LAYOUT_UNDEFINED || gl_clear->value )
	{
		VK_GetClearColor( clear );

		VkClearColorValue color = { .float32 = { powf( clear[0], 2.2f ), powf( clear[1], 2.2f ), powf( clear[2], 2.2f ), 1.0f } };
		VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VK_ImageBarrier( cmd, &post.scene, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
		vkCmdClearColorImage( cmd, post.scene.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range );
	}

	post.in_scene = true;
	return true;
}

/*
=================
Frame graph
=================
*/
// the layout a pass samples an image in
static VkImageLayout VK_PostReadLayout( const vk_image_t *img )
{
	return FBitSet( img->aspect, VK_IMAGE_ASPECT_DEPTH_BIT ) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static VkDescriptorSet VK_PostDescriptorSet( const vk_post_pass_t *pass )
{
	vk_post_set_t key = { 0 };

	VkImageLayout layouts[VK_POST_MAX_INPUTS] = { 0 };

	for( int i = 0; i < VK_POST_MAX_INPUTS; i++ )
	{
		if( !pass->inputs[i].img )
			continue;

		key.views[i] = pass->inputs[i].view ? pass->inputs[i].view : pass->inputs[i].img->view;
		key.samplers[i] = pass->inputs[i].sampler;
		layouts[i] = VK_PostReadLayout( pass->inputs[i].img );
	}

	key.output = pass->output ? pass->output->view : VK_NULL_HANDLE;
	key.buffer = pass->buffer;

	for( int i = 0; i < post.num_sets; i++ )
	{
		vk_post_set_t *cached = &post.sets[i];

		if( !memcmp( cached->views, key.views, sizeof( key.views )) && !memcmp( cached->samplers, key.samplers, sizeof( key.samplers ))
			&& cached->output == key.output && cached->buffer == key.buffer )
			return cached->set;
	}

	// the combinations only depend on the settings: a few dozen at most
	if( post.num_sets >= VK_POST_MAX_SETS )
		return VK_NULL_HANDLE;

	VkDescriptorSetAllocateInfo alloc = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = post.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &post.set_layout,
	};

	if( vkAllocateDescriptorSets( vk.device, &alloc, &key.set ) != VK_SUCCESS )
		return VK_NULL_HANDLE;

	VkDescriptorImageInfo images[VK_POST_MAX_INPUTS + 1];
	VkDescriptorBufferInfo buffer = { post.exposure.buffer, 0, VK_WHOLE_SIZE };
	VkWriteDescriptorSet writes[VK_POST_MAX_INPUTS + 2];
	int num_writes = 0;

	for( int i = 0; i < VK_POST_MAX_INPUTS; i++ )
	{
		if( !key.views[i] )
			continue;

		images[i] = (VkDescriptorImageInfo){ key.samplers[i], key.views[i], layouts[i] };
		writes[num_writes++] = (VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = key.set,
			.dstBinding = 0,
			.dstArrayElement = i,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &images[i],
		};
	}

	if( key.output )
	{
		images[VK_POST_MAX_INPUTS] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, key.output, VK_IMAGE_LAYOUT_GENERAL };
		writes[num_writes++] = (VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = key.set,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &images[VK_POST_MAX_INPUTS],
		};
	}

	if( key.buffer )
	{
		writes[num_writes++] = (VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = key.set,
			.dstBinding = 2,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &buffer,
		};
	}

	vkUpdateDescriptorSets( vk.device, num_writes, writes, 0, NULL );
	post.sets[post.num_sets++] = key;
	return key.set;
}

// a new chain: whatever the images and the buffer did before (previous
// frames included) needs a barrier before their first use
static void VK_PostBeginGraph( void )
{
	post.num_touched = 0;
	post.buffer_first = true;
}

static int VK_PostTouchedIndex( vk_image_t *img )
{
	for( int i = 0; i < post.num_touched; i++ )
	{
		if( post.touched[i] == img )
			return i;
	}

	return -1;
}

/*
=================
VK_PostUse

the barrier an image needs before a pass reads it (after a write) or
writes it (after any use), with its move to the layout of that use
=================
*/
/*
=================
VK_PostComputeBarrier

between two passes of the chain: a full barrier (VK_ImageBarrier) costs
some 15 us each on the RTX 2080 Ti, a third of the bloom
=================
*/
static void VK_PostComputeBarrier( VkCommandBuffer cmd, vk_image_t *img, VkImageLayout layout )
{
	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		.oldLayout = img->layout,
		.newLayout = layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img->image,
		.subresourceRange = { img->aspect, 0, 1, 0, 1 },
	};

	VkDependencyInfo dep = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2( cmd, &dep );
	img->layout = layout;
}

static void VK_PostUse( VkCommandBuffer cmd, vk_image_t *img, qboolean write )
{
	const VkImageLayout layout = write ? VK_IMAGE_LAYOUT_GENERAL : VK_PostReadLayout( img );
	int index = VK_PostTouchedIndex( img );

	if( index < 0 )
	{
		// first use in this chain: unknown past (drawn into, copied from,
		// used by the previous frame), always a full barrier
		VK_ImageBarrier( cmd, img, layout );

		if( post.num_touched < (int)ARRAYSIZE( post.touched ))
		{
			index = post.num_touched++;
			post.touched[index] = img;
		}
	}
	else if( img->layout != layout || post.written[index] || write )
		VK_PostComputeBarrier( cmd, img, layout );

	if( index >= 0 )
		post.written[index] = write;
}

// the previous frame's passes for the first use, then the chain's
static void VK_PostBufferBarrier( VkCommandBuffer cmd, qboolean first )
{
	VkMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = first ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.srcAccessMask = first ? VK_ACCESS_2_MEMORY_WRITE_BIT : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	};

	VkDependencyInfo dep = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2( cmd, &dep );
}

static void VK_PostRun( VkCommandBuffer cmd, const vk_post_pass_t *pass )
{
	VkDescriptorSet set;

	VK_BeginLabel( cmd, pass->name );

	for( int i = 0; i < VK_POST_MAX_INPUTS; i++ )
	{
		if( pass->inputs[i].img )
			VK_PostUse( cmd, pass->inputs[i].img, false );
	}

	if( pass->output )
		VK_PostUse( cmd, pass->output, true );

	// every pass using the buffer writes it but the tonemap: keep it simple
	if( pass->buffer )
	{
		VK_PostBufferBarrier( cmd, post.buffer_first );
		post.buffer_first = false;
	}

	set = VK_PostDescriptorSet( pass );

	if( set )
	{
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, post.pipes[pass->pipe] );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, post.layout, 0, 1, &set, 0, NULL );
		vkCmdPushConstants( cmd, post.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pass->push ), &pass->push );
		vkCmdDispatch( cmd, pass->groups[0], pass->groups[1], 1 );
	}

	VK_EndLabel( cmd );
}

static void VK_PostSetRect( vk_post_pass_t *pass, int x, int y, int w, int h )
{
	pass->push.rect[0] = x;
	pass->push.rect[1] = y;
	pass->push.rect[2] = w;
	pass->push.rect[3] = h;
	pass->groups[0] = ( w + VK_POST_GROUP - 1 ) / VK_POST_GROUP;
	pass->groups[1] = ( h + VK_POST_GROUP - 1 ) / VK_POST_GROUP;
}

static void VK_PostSetTexel( vk_post_pass_t *pass, const vk_image_t *input, const vk_image_t *output )
{
	pass->push.texel[0] = 1.0f / input->width;
	pass->push.texel[1] = 1.0f / input->height;
	pass->push.texel[2] = 1.0f / output->width;
	pass->push.texel[3] = 1.0f / output->height;
}

// the view rectangle in texture coordinates of the full size images
static void VK_PostSetSourceRect( vk_post_pass_t *pass, int x, int y, int w, int h )
{
	pass->push.src_rect[0] = ( x + 0.5f ) / post.width;
	pass->push.src_rect[1] = ( y + 0.5f ) / post.height;
	pass->push.src_rect[2] = ( x + w - 0.5f ) / post.width;
	pass->push.src_rect[3] = ( y + h - 0.5f ) / post.height;
}

/*
=================
Passes
=================
*/
/*
=================
VK_PostSSAO

at half size: the depth samples are the cost, and the occlusion is soft
=================
*/
static void VK_PostSSAO( VkCommandBuffer cmd, int x, int y, int w, int h )
{
	vk_post_input_t depth = { VK_MainDepthImage(), VK_MainDepthSampleView(), post.nearest };
	vk_post_input_t lindepth = { &post.lindepth, VK_NULL_HANDLE, post.nearest };
	const float zfar = Q_max( 256.0f, RI.farClip );
	const int hx = x / 2, hy = y / 2;
	const int hw = ( x + w + 1 ) / 2 - hx, hh = ( y + h + 1 ) / 2 - hy;
	vk_post_pass_t pass;

	// eye distance of the nearest of each 2x2 pixels, 0 for the sky
	memset( &pass, 0, sizeof( pass ));
	pass.name = "SSAO depth";
	pass.pipe = VK_POST_SSAO_DEPTH;
	pass.inputs[0] = depth;
	pass.output = &post.lindepth;
	VK_PostSetRect( &pass, hx, hy, hw, hh );
	VK_PostSetTexel( &pass, VK_MainDepthImage(), &post.lindepth );
	pass.push.params[0] = 4.0f; // R_SetupProjectionMatrix
	pass.push.params[1] = zfar;
	VK_PostRun( cmd, &pass );

	memset( &pass, 0, sizeof( pass ));
	pass.name = "SSAO";
	pass.pipe = VK_POST_SSAO;
	pass.inputs[0] = lindepth;
	pass.output = &post.ao_raw;
	VK_PostSetRect( &pass, hx, hy, hw, hh );
	VK_PostSetTexel( &pass, &post.lindepth, &post.ao_raw );
	pass.push.params[0] = 4.0f;
	pass.push.params[1] = zfar;
	pass.push.params[2] = tanf( RI.rvp.fov_x * M_PI_F / 360.0f );
	pass.push.params[3] = tanf( RI.rvp.fov_y * M_PI_F / 360.0f );
	pass.push.params2[0] = post.s.ssao_radius;
	pass.push.params2[1] = post.s.ssao;
	VK_PostRun( cmd, &pass );

	memset( &pass, 0, sizeof( pass ));
	pass.name = "SSAO blur";
	pass.pipe = VK_POST_SSAO_BLUR;
	pass.inputs[0] = (vk_post_input_t){ &post.ao_raw, VK_NULL_HANDLE, post.nearest };
	pass.inputs[1] = lindepth;
	pass.output = &post.ao;
	VK_PostSetRect( &pass, hx, hy, hw, hh );
	VK_PostSetTexel( &pass, &post.ao_raw, &post.ao );
	VK_PostRun( cmd, &pass );
}

static void VK_PostBloom( VkCommandBuffer cmd, int x, int y, int w, int h )
{
	vk_post_pass_t pass;

	for( int i = 0; i < post.num_bloom; i++ )
	{
		vk_image_t *src = i ? &post.bloom[i - 1] : &post.scene;
		vk_image_t *dst = &post.bloom[i];

		memset( &pass, 0, sizeof( pass ));
		pass.name = i ? "bloom down" : "bloom prefilter";
		pass.pipe = VK_POST_BLOOM_DOWN;
		pass.inputs[0] = (vk_post_input_t){ src, VK_NULL_HANDLE, post.linear };
		pass.output = dst;
		VK_PostSetRect( &pass, 0, 0, dst->width, dst->height );
		VK_PostSetTexel( &pass, src, dst );
		pass.push.iparams[0] = i == 0; // threshold, from the view only
		pass.push.params[0] = post.s.bloom_threshold;

		if( i == 0 )
			VK_PostSetSourceRect( &pass, x, y, w, h );
		else Vector4Set( pass.push.src_rect, 0.0f, 0.0f, 1.0f, 1.0f );

		VK_PostRun( cmd, &pass );
	}

	for( int i = post.num_bloom - 2; i >= 0; i-- )
	{
		vk_image_t *src = &post.bloom[i + 1];
		vk_image_t *dst = &post.bloom[i];

		memset( &pass, 0, sizeof( pass ));
		pass.name = "bloom up";
		pass.pipe = VK_POST_BLOOM_UP;
		pass.inputs[0] = (vk_post_input_t){ src, VK_NULL_HANDLE, post.linear };
		pass.output = dst;
		VK_PostSetRect( &pass, 0, 0, dst->width, dst->height );
		VK_PostSetTexel( &pass, src, dst );
		VK_PostRun( cmd, &pass );
	}
}

static void VK_PostExposure( VkCommandBuffer cmd, int x, int y, int w, int h )
{
	const uint32_t groups_x = ( w + VK_POST_LUM_TILE - 1 ) / VK_POST_LUM_TILE;
	const uint32_t groups_y = ( h + VK_POST_LUM_TILE - 1 ) / VK_POST_LUM_TILE;
	vk_post_pass_t pass;

	memset( &pass, 0, sizeof( pass ));
	pass.name = "luminance";
	pass.pipe = VK_POST_LUMINANCE;
	pass.inputs[0] = (vk_post_input_t){ &post.scene, VK_NULL_HANDLE, post.linear };
	pass.buffer = true;
	VK_PostSetRect( &pass, x, y, w, h );
	VK_PostSetTexel( &pass, &post.scene, &post.scene );
	pass.groups[0] = groups_x;
	pass.groups[1] = groups_y;
	pass.push.iparams[0] = groups_x;
	VK_PostRun( cmd, &pass );

	memset( &pass, 0, sizeof( pass ));
	pass.name = "exposure";
	pass.pipe = VK_POST_EXPOSURE;
	pass.buffer = true;
	pass.groups[0] = pass.groups[1] = 1;
	pass.push.iparams[0] = groups_x * groups_y;
	pass.push.iparams[1] = post.exposure_reset;
	pass.push.params[0] = VK_POST_EXPOSURE_KEY;
	pass.push.params[1] = post.s.ev_min;
	pass.push.params[2] = post.s.ev_max;
	pass.push.params[3] = bound( 0.0f, (float)( gp_cl->time - gp_cl->oldtime ), 0.1f );
	pass.push.params2[0] = post.s.speed;
	VK_PostRun( cmd, &pass );

	post.exposure_reset = false;
}

/*
=================
VK_PostSMAA

the three passes of SMAA 1x, from src into dst
=================
*/
static void VK_PostClear( VkCommandBuffer cmd, vk_image_t *img )
{
	VkClearColorValue zero = { 0 };
	VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	VK_ImageBarrier( cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	vkCmdClearColorImage( cmd, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range );
}

static void VK_PostSMAA( VkCommandBuffer cmd, vk_image_t *src, vk_image_t *dst, int x, int y, int w, int h )
{
	vk_post_pass_t pass;

	// the searches read past the view: no edge there
	if( post.smaa_edges.layout == VK_IMAGE_LAYOUT_UNDEFINED )
		VK_PostClear( cmd, &post.smaa_edges );
	if( post.smaa_weights.layout == VK_IMAGE_LAYOUT_UNDEFINED )
		VK_PostClear( cmd, &post.smaa_weights );

	memset( &pass, 0, sizeof( pass ));
	pass.name = "SMAA edges";
	pass.pipe = VK_POST_SMAA_EDGES;
	pass.inputs[0] = (vk_post_input_t){ src, VK_NULL_HANDLE, post.linear };
	pass.output = &post.smaa_edges;
	VK_PostSetRect( &pass, x, y, w, h );
	VK_PostSetTexel( &pass, src, &post.smaa_edges );
	VK_PostRun( cmd, &pass );

	memset( &pass, 0, sizeof( pass ));
	pass.name = "SMAA weights";
	pass.pipe = VK_POST_SMAA_WEIGHTS;
	pass.inputs[0] = (vk_post_input_t){ &post.smaa_edges, VK_NULL_HANDLE, post.linear };
	pass.inputs[1] = (vk_post_input_t){ &post.smaa_area, VK_NULL_HANDLE, post.linear };
	pass.inputs[2] = (vk_post_input_t){ &post.smaa_search, VK_NULL_HANDLE, post.nearest };
	pass.output = &post.smaa_weights;
	VK_PostSetRect( &pass, x, y, w, h );
	VK_PostSetTexel( &pass, &post.smaa_edges, &post.smaa_weights );
	VK_PostRun( cmd, &pass );

	memset( &pass, 0, sizeof( pass ));
	pass.name = "SMAA blend";
	pass.pipe = VK_POST_SMAA_BLEND;
	pass.inputs[0] = (vk_post_input_t){ src, VK_NULL_HANDLE, post.linear };
	pass.inputs[1] = (vk_post_input_t){ &post.smaa_weights, VK_NULL_HANDLE, post.linear };
	pass.output = dst;
	VK_PostSetRect( &pass, x, y, w, h );
	VK_PostSetTexel( &pass, src, dst );
	VK_PostRun( cmd, &pass );
}

/*
=================
VK_PostEndScene

x, y, w, h: the view as VK_SetViewport took it, from the bottom left
=================
*/
void VK_PostEndScene( int x, int y, int w, int h )
{
	VkCommandBuffer cmd = VK_FrameCommands();
	vk_image_t *ldr, *main_color = VK_MainColorImage();
	vk_post_pass_t pass;
	int flags = 0, next = 0;

	if( !post.in_scene )
		return;

	post.in_scene = false;
	VK_SetColorTarget( NULL, VK_TARGET_LDR ); // ends the scene pass

	// top down from here, inside the targets
	y = post.height - y - h;
	x = bound( 0, x, (int)post.width );
	y = bound( 0, y, (int)post.height );
	w = Q_min( w, (int)post.width - x );
	h = Q_min( h, (int)post.height - y );

	if( !cmd || w <= 0 || h <= 0 )
		return;

	VK_BeginLabel( cmd, "post-process" );
	VK_PostBeginGraph();

	VK_ProfileMark( cmd, VK_PROF_POST_SSAO );
	if( post.s.ssao > 0.0f )
	{
		VK_PostSSAO( cmd, x, y, w, h );
		SetBits( flags, VK_POST_USE_AO );
	}

	VK_ProfileMark( cmd, VK_PROF_POST_BLOOM );
	if( post.s.bloom > 0.0f && post.num_bloom )
	{
		VK_PostBloom( cmd, x, y, w, h );
		SetBits( flags, VK_POST_USE_BLOOM );
	}

	// once a frame: other views of the same frame use its result
	VK_ProfileMark( cmd, VK_PROF_POST_EXPOSURE );
	if( post.s.autoexposure )
	{
		if( post.exposure_frame != VK_CurrentFrame( ) || post.exposure_reset )
			VK_PostExposure( cmd, x, y, w, h );
		post.exposure_frame = VK_CurrentFrame();
		SetBits( flags, VK_POST_USE_EXPOSURE );
	}
	else post.exposure_reset = true; // start over from the target when it comes back

	VK_ProfileMark( cmd, VK_PROF_POST_TONEMAP );
	memset( &pass, 0, sizeof( pass ));
	pass.name = "tonemap";
	pass.pipe = VK_POST_TONEMAP;
	pass.inputs[0] = (vk_post_input_t){ &post.scene, VK_NULL_HANDLE, post.nearest };
	if( FBitSet( flags, VK_POST_USE_BLOOM ))
		pass.inputs[1] = (vk_post_input_t){ &post.bloom[0], VK_NULL_HANDLE, post.linear };
	if( FBitSet( flags, VK_POST_USE_AO ))
		pass.inputs[2] = (vk_post_input_t){ &post.ao, VK_NULL_HANDLE, post.linear };
	pass.buffer = FBitSet( flags, VK_POST_USE_EXPOSURE ) != 0;
	pass.output = ldr = &post.ldr[next];
	VK_PostSetRect( &pass, x, y, w, h );
	VK_PostSetTexel( &pass, &post.scene, ldr );
	pass.push.iparams[0] = post.s.tonemap;
	pass.push.iparams[1] = flags;
	pass.push.iparams[2] = post.num_bloom;
	pass.push.params[0] = post.s.exposure;
	pass.push.params[1] = post.s.bloom;
	pass.push.params[2] = gp_cl->maxclients > 1 ? 0.0f : 64.0f; // highest total EV, see VK_PostReadSettings
	VK_PostRun( cmd, &pass );

	if( post.s.aa == VK_POST_AA_FXAA )
	{
		next ^= 1;
		memset( &pass, 0, sizeof( pass ));
		pass.name = "FXAA";
		pass.pipe = VK_POST_FXAA;
		pass.inputs[0] = (vk_post_input_t){ ldr, VK_NULL_HANDLE, post.linear };
		pass.output = &post.ldr[next];
		VK_PostSetRect( &pass, x, y, w, h );
		VK_PostSetTexel( &pass, ldr, &post.ldr[next] );
		VK_PostSetSourceRect( &pass, x, y, w, h );
		VK_PostRun( cmd, &pass );
		ldr = &post.ldr[next];
	}
	else if( post.s.aa == VK_POST_AA_SMAA )
	{
		VK_PostSMAA( cmd, ldr, &post.ldr[next ^ 1], x, y, w, h );
		next ^= 1;
		ldr = &post.ldr[next];
	}

	if( post.s.sharpen > 0.0f )
	{
		next ^= 1;
		memset( &pass, 0, sizeof( pass ));
		pass.name = "sharpen";
		pass.pipe = VK_POST_SHARPEN;
		pass.inputs[0] = (vk_post_input_t){ ldr, VK_NULL_HANDLE, post.nearest };
		pass.output = &post.ldr[next];
		VK_PostSetRect( &pass, x, y, w, h );
		VK_PostSetTexel( &pass, ldr, &post.ldr[next] );
		pass.push.params[0] = post.s.sharpen;
		VK_PostRun( cmd, &pass );
		ldr = &post.ldr[next];
	}

	// into the main target, under the 2D still to come
	VK_ImageBarrier( cmd, ldr, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	VK_ImageBarrier( cmd, main_color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

	VkImageCopy copy = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.srcOffset = { x, y, 0 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstOffset = { x, y, 0 },
		.extent = { w, h, 1 },
	};

	vkCmdCopyImage( cmd, ldr->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, main_color->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
	VK_EndLabel( cmd );
}
