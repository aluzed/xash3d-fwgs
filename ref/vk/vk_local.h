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
	X( vkCmdEndDebugUtilsLabelEXT )

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
	X( vkCmdCopyImageToBuffer )

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

// an image together with the layout it was last left in
typedef struct vk_image_s
{
	VkImage image;
	VkImageView view;
	VkDeviceMemory memory;
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
// vk_swapchain.c
//
qboolean VK_InitFrames( void );
void VK_ShutdownFrames( void );
void VK_BeginFrame( qboolean clear_scene );
void VK_EndFrame( void );
void VK_ClearScreen( void );
qboolean VK_ScreenShot( const char *filename, int shot_type );

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
void GL_FreeTexture( unsigned int texnum );

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
