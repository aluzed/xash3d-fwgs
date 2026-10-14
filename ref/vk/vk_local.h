/*
vk_local.h - Vulkan renderer shared definitions
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

#ifndef VK_LOCAL_H
#define VK_LOCAL_H

// the loader is opened by the engine, every entry point goes through
// vkGetInstanceProcAddr
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "ref_common.h"
#include "com_strings.h"
#include "crtlib.h"
#include "crclib.h"
#include "enginefeatures.h"
#include "ref_params.h"
#include "protocol.h"
#include "common/mod_local.h"
#include "pmove.h"
#include "atlas.h"
#include "studio.h"
#include "shaders/bindings.h"

// baseline: dynamic rendering, synchronization2, descriptor indexing and
// buffer device address are all core in 1.3
#define VK_MIN_API_VERSION VK_API_VERSION_1_3

#define VK_FRAMES_IN_FLIGHT 2
#define VK_MAX_SWAPCHAIN_IMAGES 8

// entry points loaded with a NULL instance
#define VK_GLOBAL_FUNCS( X ) \
	X( vkCreateInstance ) \
	X( vkEnumerateInstanceVersion ) \
	X( vkEnumerateInstanceExtensionProperties ) \
	X( vkEnumerateInstanceLayerProperties )

// entry points loaded from the instance
#define VK_INSTANCE_FUNCS( X ) \
	X( vkDestroyInstance ) \
	X( vkGetDeviceProcAddr ) \
	X( vkDestroySurfaceKHR ) \
	X( vkEnumeratePhysicalDevices ) \
	X( vkGetPhysicalDeviceProperties ) \
	X( vkGetPhysicalDeviceFeatures2 ) \
	X( vkGetPhysicalDeviceMemoryProperties ) \
	X( vkGetPhysicalDeviceFormatProperties ) \
	X( vkGetPhysicalDeviceQueueFamilyProperties ) \
	X( vkEnumerateDeviceExtensionProperties ) \
	X( vkCreateDevice ) \
	X( vkGetPhysicalDeviceSurfaceSupportKHR ) \
	X( vkGetPhysicalDeviceSurfaceCapabilitiesKHR ) \
	X( vkGetPhysicalDeviceSurfaceFormatsKHR ) \
	X( vkGetPhysicalDeviceSurfacePresentModesKHR ) \
	X( vkCreateDebugUtilsMessengerEXT ) \
	X( vkDestroyDebugUtilsMessengerEXT ) \
	X( vkSetDebugUtilsObjectNameEXT ) \
	X( vkCmdBeginDebugUtilsLabelEXT ) \
	X( vkCmdEndDebugUtilsLabelEXT ) \
	X( vkGetPhysicalDeviceMemoryProperties2 )

// entry points loaded from the device
#define VK_DEVICE_FUNCS( X ) \
	X( vkDestroyDevice ) \
	X( vkGetDeviceQueue ) \
	X( vkDeviceWaitIdle ) \
	X( vkQueueSubmit2 ) \
	X( vkQueuePresentKHR ) \
	X( vkCreateSwapchainKHR ) \
	X( vkDestroySwapchainKHR ) \
	X( vkGetSwapchainImagesKHR ) \
	X( vkAcquireNextImageKHR ) \
	X( vkCreateImage ) \
	X( vkDestroyImage ) \
	X( vkCreateImageView ) \
	X( vkDestroyImageView ) \
	X( vkGetImageMemoryRequirements ) \
	X( vkBindImageMemory ) \
	X( vkCreateBuffer ) \
	X( vkDestroyBuffer ) \
	X( vkGetBufferMemoryRequirements ) \
	X( vkBindBufferMemory ) \
	X( vkAllocateMemory ) \
	X( vkFreeMemory ) \
	X( vkMapMemory ) \
	X( vkUnmapMemory ) \
	X( vkCreateCommandPool ) \
	X( vkDestroyCommandPool ) \
	X( vkResetCommandPool ) \
	X( vkAllocateCommandBuffers ) \
	X( vkBeginCommandBuffer ) \
	X( vkEndCommandBuffer ) \
	X( vkCreateFence ) \
	X( vkDestroyFence ) \
	X( vkWaitForFences ) \
	X( vkResetFences ) \
	X( vkCreateSemaphore ) \
	X( vkDestroySemaphore ) \
	X( vkCmdPipelineBarrier2 ) \
	X( vkCmdClearColorImage ) \
	X( vkCmdClearDepthStencilImage ) \
	X( vkCmdBlitImage ) \
	X( vkCreateQueryPool ) \
	X( vkDestroyQueryPool ) \
	X( vkGetQueryPoolResults ) \
	X( vkCmdResetQueryPool ) \
	X( vkCmdWriteTimestamp ) \
	X( vkCmdCopyImageToBuffer ) \
	X( vkCmdCopyBufferToImage ) \
	X( vkCmdBeginRendering ) \
	X( vkCmdEndRendering ) \
	X( vkCmdSetViewport ) \
	X( vkCmdSetScissor ) \
	X( vkCmdBindPipeline ) \
	X( vkCmdBindDescriptorSets ) \
	X( vkCmdBindVertexBuffers ) \
	X( vkCmdPushConstants ) \
	X( vkCmdDraw ) \
	X( vkCmdDrawIndexed ) \
	X( vkCmdBindIndexBuffer ) \
	X( vkCmdCopyBuffer ) \
	X( vkCmdClearAttachments ) \
	X( vkCmdSetDepthBias ) \
	X( vkCmdSetCullMode ) \
	X( vkCmdSetDepthTestEnable ) \
	X( vkCmdSetDepthWriteEnable ) \
	X( vkCmdSetDepthCompareOp ) \
	X( vkCmdSetStencilTestEnable ) \
	X( vkCmdSetStencilOp ) \
	X( vkCmdSetStencilCompareMask ) \
	X( vkCmdSetStencilWriteMask ) \
	X( vkCmdSetStencilReference ) \
	X( vkCreateSampler ) \
	X( vkDestroySampler ) \
	X( vkCreateDescriptorSetLayout ) \
	X( vkDestroyDescriptorSetLayout ) \
	X( vkCreateDescriptorPool ) \
	X( vkDestroyDescriptorPool ) \
	X( vkAllocateDescriptorSets ) \
	X( vkUpdateDescriptorSets ) \
	X( vkCreatePipelineLayout ) \
	X( vkDestroyPipelineLayout ) \
	X( vkCreateShaderModule ) \
	X( vkDestroyShaderModule ) \
	X( vkCreateGraphicsPipelines ) \
	X( vkDestroyPipeline ) \
	X( vkCreatePipelineCache ) \
	X( vkDestroyPipelineCache ) \
	X( vkGetPipelineCacheData ) \
	X( vkCreateComputePipelines ) \
	X( vkCmdDispatch ) \
	X( vkResetDescriptorPool ) \
	X( vkCmdCopyImage ) \
	X( vkCmdFillBuffer )

#define VK_DECLARE_FUNC( name ) extern PFN_##name name;
VK_GLOBAL_FUNCS( VK_DECLARE_FUNC )
VK_INSTANCE_FUNCS( VK_DECLARE_FUNC )
VK_DEVICE_FUNCS( VK_DECLARE_FUNC )
#undef VK_DECLARE_FUNC

// optional device capabilities, detected once at startup
typedef struct vk_caps_s
{
	qboolean memory_budget;          // VK_EXT_memory_budget
	qboolean acceleration_structure; // VK_KHR_acceleration_structure
	qboolean ray_query;              // VK_KHR_ray_query
	qboolean ray_tracing_pipeline;   // VK_KHR_ray_tracing_pipeline
	qboolean texture_compression_bc;
	qboolean anisotropy;
} vk_caps_t;

typedef struct vk_state_s
{
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
	uint32_t api_version; // instance version actually created
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger; // only with validation
	VkSurfaceKHR surface;
	qboolean validation;
	qboolean debug_utils; // object names and GPU labels available

	VkPhysicalDevice physical_device;
	VkPhysicalDeviceProperties props;
	VkPhysicalDeviceMemoryProperties memory;
	vk_caps_t caps;

	VkDevice device;
	uint32_t graphics_family; // graphics, compute and present
	VkQueue graphics_queue;
	uint32_t transfer_family; // same as graphics_family without a dedicated one
	VkQueue transfer_queue;
} vk_state_t;

extern vk_state_t vk;

// a sub-allocation of a device memory block
typedef struct vk_alloc_s
{
	VkDeviceMemory memory;
	VkDeviceSize offset;
	VkDeviceSize size;
	byte *mapped; // host visible memory stays mapped
	int block;
} vk_alloc_t;

typedef struct vk_buffer_s
{
	VkBuffer buffer;
	VkDeviceSize size;
	vk_alloc_t alloc;
} vk_buffer_t;

// an image together with the layout it was last left in
typedef struct vk_image_s
{
	VkImage image;
	VkImageView view;
	vk_alloc_t alloc;
	VkFormat format;
	VkImageAspectFlags aspect;
	VkImageLayout layout;
	uint32_t width;
	uint32_t height;
} vk_image_t;

//
// vk_core.c
//
qboolean VK_InitCore( void );
void VK_ShutdownCore( void );
void VK_SetObjectName( VkObjectType type, uint64_t handle, const char *fmt, ... ) FORMAT_CHECK( 3 );
void VK_BeginLabel( VkCommandBuffer cmd, const char *name );
void VK_EndLabel( VkCommandBuffer cmd );
int VK_FindMemoryType( uint32_t type_bits, VkMemoryPropertyFlags flags );
qboolean VK_CreateImage( vk_image_t *img, const char *name, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect );
void VK_DestroyImage( vk_image_t *img );
void VK_ImageBarrier( VkCommandBuffer cmd, vk_image_t *img, VkImageLayout layout );
void VK_ImageBarrierRaw( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to );

//
// vk_memory.c
//
extern convar_t r_vk_dynamic_mb;
qboolean VK_InitMemory( void );
void VK_ShutdownMemory( void );
qboolean VK_Alloc( const VkMemoryRequirements *req, VkMemoryPropertyFlags flags, qboolean linear, vk_alloc_t *out );
void VK_Free( vk_alloc_t *alloc );
qboolean VK_CreateBuffer( vk_buffer_t *buf, const char *name, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags );
void VK_DestroyBuffer( vk_buffer_t *buf );
void VK_DeferRelease( void (*release)( void *data ), void *data );
void VK_RunDeferred( uint64_t completed );
void *VK_UploadStaging( VkDeviceSize size, VkBuffer *buffer, VkDeviceSize *offset, VkCommandBuffer *cmd );
void VK_FlushUploads( void );
void VK_BeginDynamic( int frame );
void *VK_DynamicAlloc( VkDeviceSize size, VkDeviceSize align, VkBuffer *buffer, VkDeviceSize *offset );

//
// vk_pipeline.c
//
typedef enum
{
	VK_BLEND_NONE = 0,
	VK_BLEND_ALPHA,    // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
	VK_BLEND_ADD,      // GL_SRC_ALPHA, GL_ONE
	VK_BLEND_MODULATE, // GL_ZERO, GL_SRC_COLOR
	VK_BLEND_COUNT,
	// world only from here
	VK_BLEND_OVERBRIGHT = VK_BLEND_COUNT, // GL_DST_COLOR, GL_SRC_COLOR
	VK_BLEND_ONE_ONE,                     // GL_ONE, GL_ONE
	VK_BLEND_DSTCOLOR_INVSRCALPHA,        // GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA
	VK_BLEND_PREMULTIPLIED,               // GL_ONE, GL_ONE_MINUS_SRC_ALPHA
	VK_BLEND_NO_COLOR,                    // glColorMask( GL_FALSE, ... ), stencil passes
	VK_BLEND_ALL
} vk_blend_t;

typedef enum
{
	VK_TOPOLOGY_TRIANGLES = 0,
	VK_TOPOLOGY_LINES,
	VK_TOPOLOGY_POINTS,
	VK_TOPOLOGY_COUNT
} vk_topology_t;

typedef struct vk_imm_vertex_s
{
	vec3_t position;
	vec2_t texcoord;
	vec4_t color; // float like glColor, quantized only by the framebuffer
} vk_imm_vertex_t;

// mirrors imm_common.glsl
typedef struct vk_imm_push_s
{
	float mvp[16];
	int texture;
	int alpha_test;
	float alpha_ref;
	int fog;         // VK_FogIndex
} vk_imm_push_t;

// mirrors world_common.glsl
typedef struct vk_world_push_s
{
	float mvp[16];
	int texture;
	int lightmap;
	int mode;
	float scale;
	vec4_t color;
	float alpha_ref;
	int alpha_test;
	float tcoffset[2];
	int fog;         // VK_FogIndex
} vk_world_push_t;

#define VK_PUSH_CONSTANTS_SIZE 128
#define VK_HDR_COLOR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

// the colour targets the scene can be drawn into, each with its own
// pipelines: the main RGBA8 one, and the HDR scene of vk_post.c
typedef enum
{
	VK_TARGET_LDR = 0,
	VK_TARGET_HDR,
	VK_TARGET_COUNT
} vk_target_t;

typedef struct vk_pipelines_s
{
	VkDescriptorSetLayout texture_layout;
	VkDescriptorPool texture_pool;
	VkDescriptorSet texture_set;
	VkDescriptorSetLayout fog_layout;
	VkDescriptorPool fog_pool;
	VkDescriptorSet fog_sets[VK_FRAMES_IN_FLIGHT];
	VkPipelineLayout layout;
	VkPipelineCache cache;
	VkPipeline imm[VK_TARGET_COUNT][VK_TOPOLOGY_COUNT][VK_BLEND_ALL];
	VkPipeline world[VK_TARGET_COUNT][VK_BLEND_ALL][2][2]; // blend, depth write, depth GL_EQUAL
	qboolean hdr_ready; // the VK_TARGET_HDR pipelines exist, built on first use
} vk_pipelines_t;

extern vk_pipelines_t vk_pipe;

qboolean VK_InitPipelines( void );
void VK_ShutdownPipelines( void );
void VK_WriteTextureDescriptor( int texnum, VkImageView view, VkSampler sampler );
void VK_WriteFogDescriptor( int frame, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range );
void VK_BindDescriptorSets( VkCommandBuffer cmd );
VkPipeline VK_WorldPipeline( vk_blend_t blend, qboolean depth_write, qboolean depth_equal );
VkPipeline VK_ImmPipeline( vk_topology_t topology, vk_blend_t blend );
qboolean VK_CreateHDRPipelines( void );
VkShaderModule VK_CreateShader( const uint32_t *code, size_t size, const char *name );

//
// vk_swapchain.c
//
qboolean VK_InitFrames( void );
void VK_ShutdownFrames( void );
void VK_BeginFrame( qboolean clear_scene );
void VK_EndFrame( void );
void VK_ClearScreen( void );
void VK_SetClearColor( float r, float g, float b );
void VK_SetViewport( int x, int y, int width, int height );
void VK_SetDepthRange( float min, float max );
int VK_FrameIndex( void );
void VK_CreateInitialSwapchain( void );
void VK_ResetViewport( void );
void VK_ClearMainDepth( void );
qboolean VK_ReadPixels( int x, int y, int w, int h, byte *rgba );
VkCommandBuffer VK_FrameCommands( void );
vk_image_t *VK_MainColorImage( void );
vk_image_t *VK_MainDepthImage( void );
VkImageView VK_MainDepthSampleView( void );
void VK_SetColorTarget( vk_image_t *img, vk_target_t kind );
vk_target_t VK_ColorTarget( void );
void VK_GetClearColor( vec3_t out );

//
// vk_profile.c: GPU time of each part of the frame (r_vk_gpu_profile)
//
typedef enum
{
	VK_PROF_FRAME_START = 0, // clears and uploads before the scene
	VK_PROF_WORLD,
	VK_PROF_SOLID,
	VK_PROF_EFFECTS,         // solid sprites, client effects, normal triangles
	VK_PROF_TRANSLUCENT,
	VK_PROF_VIEWMODEL,
	VK_PROF_WATER,
	VK_PROF_POST_SSAO,       // post-processing of the HDR scene (vk_post.c)
	VK_PROF_POST_BLOOM,
	VK_PROF_POST_EXPOSURE,
	VK_PROF_POST_TONEMAP,    // tonemap, FXAA, sharpen, copy to the main target
	VK_PROF_2D,
	VK_PROF_PRESENT_COPY,
	VK_PROF_COUNT
} vk_profile_part_t;

void VK_RegisterProfileCvars( void );
void VK_InitProfile( void );
void VK_ShutdownProfile( void );
void VK_ProfileBeginFrame( VkCommandBuffer cmd, int slot );
void VK_ProfileMark( VkCommandBuffer cmd, vk_profile_part_t part );
void VK_ProfileEndFrame( VkCommandBuffer cmd );
qboolean VK_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot );
qboolean VK_ScreenShot( const char *filename, int shot_type );
qboolean VK_FrameRecording( void );
uint64_t VK_CurrentFrame( void );
VkCommandBuffer VK_BeginMainPass( void );
void VK_EndMainPass( void );
void VK_MainExtent( int *width, int *height );
VkFormat VK_MainColorFormat( void );
VkFormat VK_MainDepthFormat( void );
qboolean VK_FormatHasStencil( VkFormat format );

//
// vk_post.c: HDR scene and post-processing (r_vk_quality)
//
void VK_RegisterPostCvars( void );
qboolean VK_InitPost( void );
void VK_ShutdownPost( void );
void VK_PostBeginFrame( void );
qboolean VK_PostBeginScene( void );
void VK_PostEndScene( int x, int y, int w, int h );

//
// RI and tr of ref_gl: the view being drawn and the renderer globals, shared
// by the world and the studio code that are ported from it
//
#define BLOCK_SIZE_MAX  1024
#define MAX_LIGHTMAPS   256
#define FRUSTUM_PLANES  6
#define MAX_DECAL_SURFS 4096

typedef struct vk_frustum_s
{
	mplane_t planes[FRUSTUM_PLANES];
	unsigned int clipFlags;
} vk_frustum_t;

typedef struct vk_draw_list_s
{
	cl_entity_t *solid_entities[MAX_VISIBLE_PACKET];
	cl_entity_t *trans_entities[MAX_VISIBLE_PACKET];
	cl_entity_t *beam_entities[MAX_VISIBLE_PACKET];
	uint num_solid_entities;
	uint num_trans_entities;
	uint num_beam_entities;
} vk_draw_list_t;

typedef struct
{
	ref_viewpass_t rvp;
	vec3_t vforward, vright, vup;
	vec3_t cullorigin, cull_vforward, cull_vright, cull_vup;
	vk_frustum_t frustum;
	mleaf_t *viewleaf, *oldviewleaf;
	byte visbytes[(MAX_MAP_LEAFS+7)/8];
	float farClip;
	float viewplanedist;
	matrix4x4 projectionMatrix;
	matrix4x4 worldviewMatrix;
	matrix4x4 worldviewProjectionMatrix;
	matrix4x4 objectMatrix;
	cl_entity_t *currententity;
	model_t *currentmodel;
	cl_entity_t *currentbeam; // beam entity being drawn
	float skyMins[2][SKYBOX_MAX_SIDES];
	float skyMaxs[2][SKYBOX_MAX_SIDES];

	// fog
	qboolean fogEnabled;    // underwater or Quake fog
	qboolean fogCustom;     // TriFog
	qboolean fogSkybox;
	vec4_t fogColor;
	float fogDensity;
	float fogStart, fogEnd;
	int cached_contents;    // in water
	int cached_waterlevel;
} vk_refinst_t;

typedef struct
{
	model_t *worldmodel;
	world_static_t *world;
	cl_entity_t *entities;
	int max_entities;

	vk_draw_list_t *draw_list;
	msurface_t *draw_decals[MAX_DECAL_SURFS]; // decals batched for the surfaces drawn
	int num_draw_decals;
	cl_entity_t *viewent;    // the view model
	dlight_t *elights;       // entity lights
	color24 *palette;        // quake palette, particle colours
	double frametime;        // of the scene, beams and particles
	byte *texgammatable;
	uint16_t *lightgammatable;
	uint16_t *screengammatable;
	uint16_t *lineargammatable;
	float blend;             // CL_FxBlend of the entity being drawn, 0..1
	qboolean fFlipViewModel; // cl_righthand

	int framecount;
	int visframecount;
	int realframecount;
	int dlightframecount;
	qboolean fResetVis;
	qboolean fCustomRendering;
	qboolean modelviewIdentity;
	vec3_t modelorg;

	int skytexturenum;
	int block_size;
	int lightmapTextures[MAX_LIGHTMAPS];
	int current_lightmap_texture;
	atlas_t atlas;
	byte lightmap_buffer[BLOCK_SIZE_MAX * BLOCK_SIZE_MAX * 4];

	msurface_t *skychain;
	vec2_t ortho_mins, ortho_maxs;

	int skyboxTextures[SKYBOX_MAX_SIDES];
	int solidskyTexture, alphaskyTexture; // Quake sky, unused by HL / CS
} vk_globals_t;

extern vk_refinst_t RI;
extern vk_globals_t tr;

#define WORLDMODEL ( tr.worldmodel )


#define R_ModelOpaque( rm ) ( rm == kRenderNormal )
#define CL_IsViewEntityLocalPlayer() ( gp_cl->viewentity == ( gp_cl->playernum + 1 ))
#define DEFAULT_ALPHATEST 0.0f

static inline cl_entity_t *CL_GetEntityByIndex( int index )
{
	if( unlikely( index < 0 || index >= tr.max_entities || !tr.entities ))
		return NULL;

	return &tr.entities[index];
}

static inline model_t *CL_ModelHandle( int index )
{
	if( unlikely( index < 0 || index >= gp_cl->nummodels ))
		return NULL;

	return gp_cl->models[index];
}

// the gamma tables of gl_local.h
static inline uint LightToTexGamma( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.lightgammatable[b] : b;
}

static inline uint ScreenGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.screengammatable[b] : b;
}

static inline uint LinearGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? tr.lineargammatable[b] : b;
}

//
// vk_world.c
//
extern convar_t gl_overbright;
void VK_InitWorld( void );
void VK_ShutdownWorld( void );
void VK_WorldBeginFrame( void );
void VK_ComputeMVP( float out[16], const matrix4x4 modelview );
qboolean R_CullModel( const cl_entity_t *e, const vec3_t absmin, const vec3_t absmax );
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs );
void R_LightmapCoord( const vec3_t v, const msurface_t *surf, const float sample_size, vec2_t coords );
texture_t *R_TextureAnimation( msurface_t *s );
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime );
int R_WorldToScreen( const vec3_t point, vec3_t screen );
void R_ScreenToWorld( const vec3_t screen, vec3_t point );
int TriWorldToScreen( const float *world, float *screen );
void R_AllowFog( qboolean allowed );
int R_GetEntityRenderMode( cl_entity_t *ent );
void R_NewMap( void );
void GL_RenderFrame( const struct ref_viewpass_s *rvp );
void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities );
void R_GammaChanged( qboolean do_reset_gamma );
void GL_SubdivideSurface( model_t *loadmodel, msurface_t *fa );
byte *Mod_GetCurrentVis( void );
void GL_OrthoBounds( const float *mins, const float *maxs );
void R_SetupSky( int *skyboxTextures );
void R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture );
intptr_t VK_SkyParm( int parm, int arg );
void R_PushScene( void );
void R_PopScene( void );
void R_ClearScene( void );
qboolean R_AddEntity( struct cl_entity_s *clent, int type );
int CL_FxBlend( cl_entity_t *e );

//
// vk_fog.c
//
extern convar_t r_detailtextures;
extern convar_t gl_fog;
void VK_RegisterFogCvars( void );
qboolean VK_InitFog( void );
void VK_ShutdownFog( void );
void VK_FogBeginFrame( int frame );
int VK_FogIndex( void );
void VK_FogSet2D( qboolean enable );
void VK_FogSetupFrame( void );
qboolean VK_FogActive( void );
void VK_FogNewMap( void );
void VK_FogSkyBegin( void );
void VK_FogSkyEnd( void );
void R_DrawFog( void );
void R_CheckFog( void );
void GL_SetupFogColorForSurfacesEx( int passes, float density, qboolean blend_lightmaps );
void GL_SetupFogColorForSurfaces( void );
void GL_ResetFogColor( void );
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn );
void TriFogParams( float flDensity, int iFogSkybox );

//
// vk_decals.c
//
void R_DecalInit( void );
void DrawSurfaceDecals( msurface_t *fa, qboolean single, qboolean reverse );
void DrawDecalsBatch( void );
float *R_DecalSetupVerts( decal_t *pDecal, msurface_t *surf, int texture, int *outCount );
void DrawSingleDecal( decal_t *pDecal, msurface_t *fa );
void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
void R_DecalRemoveAll( int texture );
int R_CreateDecalList( struct decallist_s *pList );
void R_ClearAllDecals( qboolean include_permanent );
void R_EntityRemoveDecals( model_t *mod );
void R_ClearDecals( void );

//
// vk_rpart.c, vk_beams.c
//
void CL_DrawParticles( double frametime, particle_t *cl_active_particles, float partsize );
void CL_DrawTracers( double frametime, particle_t *cl_active_tracers );
void CL_DrawBeams( int fTrans, BEAM *active_beams );

//
// vk_sprite.c
//
void R_SpriteInit( void );
void R_DrawSpriteModel( cl_entity_t *e );

//
// vk_studio.c
//
struct engine_studio_api_s;
struct r_studio_interface_s;
qboolean R_StudioFillAPI( struct engine_studio_api_s *api, struct r_studio_interface_s *pDefaultDraw );
void R_StudioSetDrawInterface( struct r_studio_interface_s *pDraw );
void R_StudioResetPlayerModels( void );
void R_DrawStudioModel( cl_entity_t *e );
void R_DrawViewModel( void );
void R_RunViewmodelEvents( void );
void R_StudioInit( void );
void Mod_StudioLoadTextures( model_t *mod, void *data );
void Mod_StudioUnloadTextures( void *data );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );
float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time );
mstudiotexture_t *R_StudioGetTexture( cl_entity_t *e );
studiohdr_t *R_StudioGetHeader( void );

//
// vk_imm.c
//
void VK_InitImm( void );
void VK_ImmBeginFrame( void );
void VK_ImmEndFrame( void );
void VK_ImmFlush( void );
void VK_ImmSet3D( qboolean enable );
void VK_ImmEnd( void );
void VK_ImmVertex( const float *v, float s, float t, const byte *rgba );
#define VK_IMM_MAX_PRIMITIVE 4096 // vertices between Begin and End
void VK_ImmLoadModelview( const matrix4x4 modelview );
void VK_ImmColor4f( float r, float g, float b, float a );
void VK_ImmColor4ub( byte r, byte g, byte b, byte a );
void VK_ImmDepthTest( qboolean enable );
void VK_ImmDepthMask( qboolean write );
void VK_ImmDepthFunc( VkCompareOp op );
void VK_ImmDepthRange( float min, float max );
void VK_ImmCull( VkCullModeFlags mode );
VkCullModeFlags VK_ImmGetCull( void );
void VK_ImmBlendEnable( qboolean enable );
void VK_ImmBlendFunc( vk_blend_t func );
void VK_ImmAlphaTestEnable( qboolean enable );
void VK_ImmAlphaFunc( float ref );
void VK_ImmTexture2D( qboolean enable );
void VK_ImmTexReplace( qboolean replace );
void VK_ImmFlatShade( qboolean flat );
void VK_ImmStencilTest( qboolean enable );
void VK_ImmStencilFunc( VkCompareOp op, uint32_t ref, uint32_t mask );
void VK_ImmStencilOp( VkStencilOp fail, VkStencilOp zfail, VkStencilOp zpass );
void VK_ImmColorMask( qboolean write );
void GL_PushPolygonOffset( float factor, float units );
void GL_PopPolygonOffset( void );
int TriSpriteTexture( model_t *pSpriteModel, int frame );
void TriBrightness( float brightness );
void TriRenderMode( int mode );
void TriBegin( int primitive );
void TriEnd( void );
void TriColor4f( float r, float g, float b, float a );
void TriColor4ub( byte r, byte g, byte b, byte a );
void TriTexCoord2f( float u, float v );
void TriVertex3f( float x, float y, float z );
void TriVertex3fv( const float *v );
void TriCullFace( TRICULLSTYLE mode );
void GL_Bind( int tmu, unsigned int texnum );
void GL_SetRenderMode( int mode );
void R_Set2DMode( qboolean enable );

// r_speeds counters, as ref_gl's ref_speeds_t
typedef struct
{
	uint c_world_polys;
	uint c_studio_polys;
	uint c_sprite_polys;
	uint c_alias_polys;
	uint c_world_leafs;

	uint c_view_beams_count;
	uint c_active_tents_count;
	uint c_alias_models_drawn;
	uint c_studio_models_drawn;
	uint c_sprite_models_drawn;
	uint c_particle_count;

	uint c_client_ents; // entities that moved to client
	double t_world_node;
	double t_world_draw;
} ref_speeds_t;

extern ref_speeds_t r_stats;

void R_DrawCubemapView( const vec3_t origin, const vec3_t angles, int size );
void R_Set2DOffset( float x, float y );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a );
void VGUI_SetupDrawing( qboolean rect );

//
// vk_textures.c
//
void VK_InitTextures( void );
void VK_ShutdownTextures( void );
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int GL_FindTexture( const char *name );
const char *GL_TextureName( unsigned int texnum );
const byte *GL_TextureData( unsigned int texnum );
void GL_FreeTexture( unsigned int texnum );
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor );
void GL_UpdateTexture( int texnum, int cols, int rows, int width, int height, const byte *buffer, pixformat_t fmt );
void R_OverrideTextureSourceSize( unsigned int texnum, unsigned int srcWidth, unsigned int srcHeight );
const byte *R_GetTextureOriginalBuffer( unsigned int texnum );
void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale );
void R_SetDetailScaleForTexture( int texture, float xScale, float yScale );
void VK_CheckTextureParams( void );
void VK_UpdateTextureRegion( int texnum, int x, int y, int width, int height, const byte *rgba );
int VK_TextureForDraw( int texnum );
void VK_ResolveRegionUploads( void );
int VK_WhiteTexture( void );
int VK_ParticleTexture( void );
void VK_TextureFogParams( int texnum, byte *red, byte *green, byte *blue, byte *density );
int VK_DefaultTexture( void );
#define Mod_AllowMaterials() ( host_allow_materials->value && !FBitSet( gp_host->features, ENGINE_DISABLE_HDTEXTURES ))
qboolean R_SearchForTextureReplacement( char *out, size_t size, const char *modelname, const char *fmt, ... ) FORMAT_CHECK( 4 );
void R_TextureReplacementReport( const char *modelname, int gl_texturenum, const char *foundpath );
intptr_t VK_TextureParm( int parm, int texnum );

//
// vk_main.c
//
extern convar_t r_vk_log_stubs;
extern convar_t r_vk_validation;
extern convar_t r_vk_device;

// log an unimplemented interface call, once per call site
#define VK_STUB() \
	do \
	{ \
		static qboolean logged; \
		if( !logged && r_vk_log_stubs.value ) \
		{ \
			logged = true; \
			gEngfuncs.Con_Printf( "ref_vk: %s is not implemented\n", __func__ ); \
		} \
	} while( 0 )

#endif // VK_LOCAL_H
