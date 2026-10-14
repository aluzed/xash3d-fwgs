/*
vk_core.c - Vulkan instance and window surface
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

#include "vk_local.h"

#define VK_DEFINE_FUNC( name ) PFN_##name name;
VK_GLOBAL_FUNCS( VK_DEFINE_FUNC )
VK_INSTANCE_FUNCS( VK_DEFINE_FUNC )
#undef VK_DEFINE_FUNC

vk_state_t vk;

#define VK_VALIDATION_LAYER "VK_LAYER_KHRONOS_validation"
#define VK_MAX_INSTANCE_EXTENSIONS 32

static void VK_LoadGlobalFuncs( void )
{
#define VK_LOAD_FUNC( name ) name = (PFN_##name)vk.get_instance_proc_addr( VK_NULL_HANDLE, #name );
	VK_GLOBAL_FUNCS( VK_LOAD_FUNC )
#undef VK_LOAD_FUNC
}

static void VK_LoadInstanceFuncs( void )
{
#define VK_LOAD_FUNC( name ) name = (PFN_##name)vk.get_instance_proc_addr( vk.instance, #name );
	VK_INSTANCE_FUNCS( VK_LOAD_FUNC )
#undef VK_LOAD_FUNC
}

static void VK_ClearFuncs( void )
{
#define VK_CLEAR_FUNC( name ) name = NULL;
	VK_GLOBAL_FUNCS( VK_CLEAR_FUNC )
	VK_INSTANCE_FUNCS( VK_CLEAR_FUNC )
#undef VK_CLEAR_FUNC
}

static qboolean VK_HasInstanceLayer( const char *name )
{
	VkLayerProperties *layers;
	uint32_t count = 0;
	qboolean found = false;

	if( vkEnumerateInstanceLayerProperties( &count, NULL ) != VK_SUCCESS || !count )
		return false;

	layers = Mem_Malloc( r_temppool, count * sizeof( *layers ));

	if( vkEnumerateInstanceLayerProperties( &count, layers ) == VK_SUCCESS )
	{
		for( uint32_t i = 0; i < count && !found; i++ )
			found = !Q_strcmp( layers[i].layerName, name );
	}

	Mem_Free( layers );
	return found;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VK_DebugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user )
{
	const char *prefix = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? S_ERROR : S_WARN;

	gEngfuncs.Con_Printf( "%sVulkan: %s\n", prefix, data->pMessage );
	return VK_FALSE;
}

static qboolean VK_CreateInstance( void )
{
	const char *extensions[VK_MAX_INSTANCE_EXTENSIONS];
	const char *layers[1];
	uint32_t num_extensions = VK_MAX_INSTANCE_EXTENSIONS - 1; // room for debug utils
	uint32_t num_layers = 0;
	uint32_t version = VK_API_VERSION_1_0;
	VkResult result;

	if( vkEnumerateInstanceVersion )
		vkEnumerateInstanceVersion( &version );

	if( version < VK_MIN_API_VERSION )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: Vulkan %u.%u is too old, %u.%u is needed\n",
			VK_API_VERSION_MAJOR( version ), VK_API_VERSION_MINOR( version ),
			VK_API_VERSION_MAJOR( VK_MIN_API_VERSION ), VK_API_VERSION_MINOR( VK_MIN_API_VERSION ));
		return false;
	}

	// ask for 1.3 when the loader has it, newer features stay optional
	vk.api_version = Q_min( version, VK_API_VERSION_1_3 );

	if( !gEngfuncs.Vulkan_GetInstanceExtensions( &num_extensions, extensions ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't get the window instance extensions\n" );
		return false;
	}

	vk.validation = gEngfuncs.Sys_CheckParm( "-vkvalidation" ) != 0;

	if( vk.validation )
	{
		if( VK_HasInstanceLayer( VK_VALIDATION_LAYER ))
		{
			layers[num_layers++] = VK_VALIDATION_LAYER;
			extensions[num_extensions++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		}
		else
		{
			gEngfuncs.Con_Printf( S_WARN "ref_vk: -vkvalidation given but " VK_VALIDATION_LAYER " isn't installed\n" );
			vk.validation = false;
		}
	}

	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Xash3D FWGS",
		.pEngineName = "ref_vk",
		.apiVersion = vk.api_version,
	};

	VkInstanceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
		.enabledLayerCount = num_layers,
		.ppEnabledLayerNames = layers,
		.enabledExtensionCount = num_extensions,
		.ppEnabledExtensionNames = extensions,
	};

	result = vkCreateInstance( &info, NULL, &vk.instance );

	if( result != VK_SUCCESS )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: vkCreateInstance failed (%d)\n", result );
		vk.instance = VK_NULL_HANDLE;
		return false;
	}

	VK_LoadInstanceFuncs();

	if( vk.validation && vkCreateDebugUtilsMessengerEXT )
	{
		VkDebugUtilsMessengerCreateInfoEXT debug = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = VK_DebugCallback,
		};

		vkCreateDebugUtilsMessengerEXT( vk.instance, &debug, NULL, &vk.debug_messenger );
	}

	gEngfuncs.Con_Reportf( "ref_vk: instance %u.%u created, %u extensions%s\n",
		VK_API_VERSION_MAJOR( vk.api_version ), VK_API_VERSION_MINOR( vk.api_version ),
		num_extensions, vk.validation ? ", validation on" : "" );

	return true;
}

static void VK_ListPhysicalDevices( void )
{
	VkPhysicalDevice *devices;
	uint32_t count = 0;

	if( vkEnumeratePhysicalDevices( vk.instance, &count, NULL ) != VK_SUCCESS || !count )
	{
		gEngfuncs.Con_Printf( S_WARN "ref_vk: no Vulkan device found\n" );
		return;
	}

	devices = Mem_Malloc( r_temppool, count * sizeof( *devices ));

	if( vkEnumeratePhysicalDevices( vk.instance, &count, devices ) == VK_SUCCESS )
	{
		for( uint32_t i = 0; i < count; i++ )
		{
			VkPhysicalDeviceProperties props;

			vkGetPhysicalDeviceProperties( devices[i], &props );
			gEngfuncs.Con_Printf( "ref_vk: device %u: %s (Vulkan %u.%u.%u)\n", i, props.deviceName,
				VK_API_VERSION_MAJOR( props.apiVersion ), VK_API_VERSION_MINOR( props.apiVersion ),
				VK_API_VERSION_PATCH( props.apiVersion ));
		}
	}

	Mem_Free( devices );
}

/*
=================
VK_InitCore

expects a window created with R_Init_Video( REF_VULKAN )
=================
*/
qboolean VK_InitCore( void )
{
	memset( &vk, 0, sizeof( vk ));

	vk.get_instance_proc_addr = (PFN_vkGetInstanceProcAddr)gEngfuncs.Vulkan_GetVkGetInstanceProcAddr();

	if( !vk.get_instance_proc_addr )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: the engine didn't load a Vulkan library\n" );
		return false;
	}

	VK_LoadGlobalFuncs();

	if( !vkCreateInstance || !vkEnumerateInstanceExtensionProperties )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: the Vulkan loader is missing core entry points\n" );
		return false;
	}

	if( !VK_CreateInstance( ))
		return false;

	if( !gEngfuncs.Vulkan_CreateSurface( vk.instance, &vk.surface ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the window surface\n" );
		VK_ShutdownCore();
		return false;
	}

	VK_ListPhysicalDevices();

	return true;
}

/*
=================
VK_ShutdownCore
=================
*/
void VK_ShutdownCore( void )
{
	if( vk.instance )
	{
		if( vk.surface )
			vkDestroySurfaceKHR( vk.instance, vk.surface, NULL );

		if( vk.debug_messenger && vkDestroyDebugUtilsMessengerEXT )
			vkDestroyDebugUtilsMessengerEXT( vk.instance, vk.debug_messenger, NULL );

		vkDestroyInstance( vk.instance, NULL );
	}

	memset( &vk, 0, sizeof( vk ));
	VK_ClearFuncs();
}
