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

// lowest instance version the renderer accepts
#define VK_MIN_API_VERSION VK_API_VERSION_1_2

// entry points loaded with a NULL instance
#define VK_GLOBAL_FUNCS( X ) \
	X( vkCreateInstance ) \
	X( vkEnumerateInstanceVersion ) \
	X( vkEnumerateInstanceExtensionProperties ) \
	X( vkEnumerateInstanceLayerProperties )

// entry points loaded from the instance
#define VK_INSTANCE_FUNCS( X ) \
	X( vkDestroyInstance ) \
	X( vkDestroySurfaceKHR ) \
	X( vkEnumeratePhysicalDevices ) \
	X( vkGetPhysicalDeviceProperties ) \
	X( vkCreateDebugUtilsMessengerEXT ) \
	X( vkDestroyDebugUtilsMessengerEXT )

#define VK_DECLARE_FUNC( name ) extern PFN_##name name;
VK_GLOBAL_FUNCS( VK_DECLARE_FUNC )
VK_INSTANCE_FUNCS( VK_DECLARE_FUNC )
#undef VK_DECLARE_FUNC

typedef struct vk_state_s
{
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
	uint32_t api_version; // instance version actually created
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger; // only with -vkvalidation
	VkSurfaceKHR surface;
	qboolean validation;
} vk_state_t;

extern vk_state_t vk;

//
// vk_core.c
//
qboolean VK_InitCore( void );
void VK_ShutdownCore( void );

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
