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
	X( vkGetPipelineCacheData )

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
	VK_BLEND_COUNT
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
} vk_imm_push_t;

typedef struct vk_pipelines_s
{
	VkDescriptorSetLayout texture_layout;
	VkDescriptorPool texture_pool;
	VkDescriptorSet texture_set;
	VkPipelineLayout layout;
	VkPipelineCache cache;
	VkPipeline imm[VK_TOPOLOGY_COUNT][VK_BLEND_COUNT];
} vk_pipelines_t;

extern vk_pipelines_t vk_pipe;

qboolean VK_InitPipelines( void );
void VK_ShutdownPipelines( void );
void VK_WriteTextureDescriptor( int texnum, VkImageView view, VkSampler sampler );

//
// vk_swapchain.c
//
qboolean VK_InitFrames( void );
void VK_ShutdownFrames( void );
void VK_BeginFrame( qboolean clear_scene );
void VK_EndFrame( void );
void VK_ClearScreen( void );
void VK_SetClearColor( float r, float g, float b );
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
// vk_imm.c
//
void VK_InitImm( void );
void VK_ImmBeginFrame( void );
void VK_ImmEndFrame( void );
void VK_ImmFlush( void );
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
int VK_TextureForDraw( int texnum );
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
