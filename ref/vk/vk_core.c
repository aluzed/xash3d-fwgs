/*
vk_core.c - Vulkan instance, device and shared helpers
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
VK_DEVICE_FUNCS( VK_DEFINE_FUNC )
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

static void VK_LoadDeviceFuncs( void )
{
#define VK_LOAD_FUNC( name ) name = (PFN_##name)vkGetDeviceProcAddr( vk.device, #name );
	VK_DEVICE_FUNCS( VK_LOAD_FUNC )
#undef VK_LOAD_FUNC
}

static void VK_ClearFuncs( void )
{
#define VK_CLEAR_FUNC( name ) name = NULL;
	VK_GLOBAL_FUNCS( VK_CLEAR_FUNC )
	VK_INSTANCE_FUNCS( VK_CLEAR_FUNC )
	VK_DEVICE_FUNCS( VK_CLEAR_FUNC )
#undef VK_CLEAR_FUNC
}

/*
=================
Debug helpers, no-ops without VK_EXT_debug_utils
=================
*/
void VK_SetObjectName( VkObjectType type, uint64_t handle, const char *fmt, ... )
{
	char name[128];
	va_list args;

	if( !vk.debug_utils || !handle )
		return;

	va_start( args, fmt );
	Q_vsnprintf( name, sizeof( name ), fmt, args );
	va_end( args );

	VkDebugUtilsObjectNameInfoEXT info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = type,
		.objectHandle = handle,
		.pObjectName = name,
	};

	vkSetDebugUtilsObjectNameEXT( vk.device, &info );
}

void VK_BeginLabel( VkCommandBuffer cmd, const char *name )
{
	if( !vk.debug_utils )
		return;

	VkDebugUtilsLabelEXT label = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = name,
	};

	vkCmdBeginDebugUtilsLabelEXT( cmd, &label );
}

void VK_EndLabel( VkCommandBuffer cmd )
{
	if( vk.debug_utils )
		vkCmdEndDebugUtilsLabelEXT( cmd );
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VK_DebugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user )
{
	const char *prefix = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? S_ERROR : S_WARN;

	gEngfuncs.Con_Printf( "%sVulkan: %s\n", prefix, data->pMessage );
	return VK_FALSE;
}

/*
=================
Memory and image helpers, until the allocator of story 007
=================
*/
int VK_FindMemoryType( uint32_t type_bits, VkMemoryPropertyFlags flags )
{
	for( uint32_t i = 0; i < vk.memory.memoryTypeCount; i++ )
	{
		if( FBitSet( type_bits, BIT( i )) && ( vk.memory.memoryTypes[i].propertyFlags & flags ) == flags )
			return i;
	}

	return -1;
}

qboolean VK_CreateImage( vk_image_t *img, const char *name, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect )
{
	VkMemoryRequirements req;

	memset( img, 0, sizeof( *img ));

	VkImageCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	if( vkCreateImage( vk.device, &info, NULL, &img->image ) != VK_SUCCESS )
		return false;

	vkGetImageMemoryRequirements( vk.device, img->image, &req );

	if( !VK_Alloc( &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &img->alloc ))
	{
		VK_DestroyImage( img );
		return false;
	}

	vkBindImageMemory( vk.device, img->image, img->alloc.memory, img->alloc.offset );

	VkImageViewCreateInfo view = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = img->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = { aspect, 0, 1, 0, 1 },
	};

	if( vkCreateImageView( vk.device, &view, NULL, &img->view ) != VK_SUCCESS )
	{
		VK_DestroyImage( img );
		return false;
	}

	img->format = format;
	img->aspect = aspect;
	img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
	img->width = width;
	img->height = height;

	VK_SetObjectName( VK_OBJECT_TYPE_IMAGE, (uint64_t)img->image, "%s", name );
	VK_SetObjectName( VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)img->view, "%s view", name );

	return true;
}

void VK_DestroyImage( vk_image_t *img )
{
	if( img->view )
		vkDestroyImageView( vk.device, img->view, NULL );
	if( img->image )
		vkDestroyImage( vk.device, img->image, NULL );
	VK_Free( &img->alloc );

	memset( img, 0, sizeof( *img ));
}

/*
=================
VK_ImageBarrierRaw

full barrier, correct but not tuned: passes refine stages when they exist
=================
*/
void VK_ImageBarrierRaw( VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to )
{
	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		.oldLayout = from,
		.newLayout = to,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
	};

	VkDependencyInfo dep = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2( cmd, &dep );
}

// a full barrier made outside the graph (uploads): its next use waits for everything
void VK_ImageBarrier( VkCommandBuffer cmd, vk_image_t *img, VkImageLayout layout )
{
	VK_ImageBarrierRaw( cmd, img->image, img->aspect, img->layout, layout );
	img->layout = layout;
	img->use = VK_USE_NONE;
}

/*
=================
Instance
=================
*/
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

static qboolean VK_HasInstanceExtension( const char *name )
{
	VkExtensionProperties *exts;
	uint32_t count = 0;
	qboolean found = false;

	if( vkEnumerateInstanceExtensionProperties( NULL, &count, NULL ) != VK_SUCCESS || !count )
		return false;

	exts = Mem_Malloc( r_temppool, count * sizeof( *exts ));

	if( vkEnumerateInstanceExtensionProperties( NULL, &count, exts ) == VK_SUCCESS )
	{
		for( uint32_t i = 0; i < count && !found; i++ )
			found = !Q_strcmp( exts[i].extensionName, name );
	}

	Mem_Free( exts );
	return found;
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
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: the Vulkan loader is %u.%u, %u.%u is needed\n",
			VK_API_VERSION_MAJOR( version ), VK_API_VERSION_MINOR( version ),
			VK_API_VERSION_MAJOR( VK_MIN_API_VERSION ), VK_API_VERSION_MINOR( VK_MIN_API_VERSION ));
		return false;
	}

	vk.api_version = VK_MIN_API_VERSION;

	if( !gEngfuncs.Vulkan_GetInstanceExtensions( &num_extensions, extensions ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't get the window instance extensions\n" );
		return false;
	}

	vk.validation = r_vk_validation.value || gEngfuncs.Sys_CheckParm( "-vkvalidation" );

	if( vk.validation )
	{
		if( VK_HasInstanceLayer( VK_VALIDATION_LAYER ))
		{
			layers[num_layers++] = VK_VALIDATION_LAYER;
			extensions[num_extensions++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
			vk.debug_utils = true;
		}
		else
		{
			gEngfuncs.Con_Printf( S_WARN "ref_vk: validation requested but " VK_VALIDATION_LAYER " isn't installed\n" );
			vk.validation = false;
		}
	}

	// object names and GPU labels for RenderDoc / Nsight, even without validation
	if( !vk.debug_utils && VK_HasInstanceExtension( VK_EXT_DEBUG_UTILS_EXTENSION_NAME ))
	{
		extensions[num_extensions++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		vk.debug_utils = true;
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

	vk.debug_utils = vk.debug_utils && vkSetDebugUtilsObjectNameEXT && vkCmdBeginDebugUtilsLabelEXT;

	gEngfuncs.Con_Reportf( "ref_vk: instance %u.%u created, %u extensions%s\n",
		VK_API_VERSION_MAJOR( vk.api_version ), VK_API_VERSION_MINOR( vk.api_version ),
		num_extensions, vk.validation ? ", validation on" : "" );

	return true;
}

/*
=================
Physical device selection
=================
*/
typedef struct vk_device_info_s
{
	VkPhysicalDevice handle;
	VkPhysicalDeviceProperties props;
	VkPhysicalDeviceFeatures2 features;
	VkPhysicalDeviceVulkan12Features features12;
	VkPhysicalDeviceVulkan13Features features13;
	vk_caps_t caps;
	qboolean swapchain;
	uint32_t graphics_family; // UINT32_MAX when none can present
	uint32_t transfer_family; // UINT32_MAX when there is no dedicated one
	const char *reject; // why the device can't be used, NULL if it can
} vk_device_info_t;

static qboolean VK_HasExtension( const VkExtensionProperties *exts, uint32_t count, const char *name )
{
	for( uint32_t i = 0; i < count; i++ )
	{
		if( !Q_strcmp( exts[i].extensionName, name ))
			return true;
	}

	return false;
}

static void VK_QueryDevice( VkPhysicalDevice handle, vk_device_info_t *info )
{
	VkExtensionProperties *exts;
	VkQueueFamilyProperties *families;
	uint32_t count = 0;

	memset( info, 0, sizeof( *info ));
	info->handle = handle;
	info->graphics_family = info->transfer_family = UINT32_MAX;

	vkGetPhysicalDeviceProperties( handle, &info->props );

	info->features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	info->features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	info->features12.pNext = &info->features13;
	info->features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	info->features.pNext = &info->features12;

	if( info->props.apiVersion >= VK_MIN_API_VERSION )
		vkGetPhysicalDeviceFeatures2( handle, &info->features );

	vkEnumerateDeviceExtensionProperties( handle, NULL, &count, NULL );
	exts = Mem_Malloc( r_temppool, Q_max( count, 1 ) * sizeof( *exts ));
	vkEnumerateDeviceExtensionProperties( handle, NULL, &count, exts );

	info->swapchain = VK_HasExtension( exts, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME );
	info->caps.memory_budget = VK_HasExtension( exts, count, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
	info->caps.acceleration_structure = VK_HasExtension( exts, count, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME )
		&& VK_HasExtension( exts, count, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );
	info->caps.ray_query = VK_HasExtension( exts, count, VK_KHR_RAY_QUERY_EXTENSION_NAME );
	info->caps.ray_tracing_pipeline = VK_HasExtension( exts, count, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME );
	info->caps.texture_compression_bc = info->features.features.textureCompressionBC;
	info->caps.anisotropy = info->features.features.samplerAnisotropy;
	Mem_Free( exts );

	vkGetPhysicalDeviceQueueFamilyProperties( handle, &count, NULL );
	families = Mem_Malloc( r_temppool, Q_max( count, 1 ) * sizeof( *families ));
	vkGetPhysicalDeviceQueueFamilyProperties( handle, &count, families );

	for( uint32_t i = 0; i < count; i++ )
	{
		const VkQueueFlags flags = families[i].queueFlags;
		VkBool32 present = VK_FALSE;

		vkGetPhysicalDeviceSurfaceSupportKHR( handle, i, vk.surface, &present );

		if( info->graphics_family == UINT32_MAX && present
			&& FBitSet( flags, VK_QUEUE_GRAPHICS_BIT ) && FBitSet( flags, VK_QUEUE_COMPUTE_BIT ))
			info->graphics_family = i;

		// a transfer-only family maps to the copy engine on discrete GPUs
		if( info->transfer_family == UINT32_MAX && FBitSet( flags, VK_QUEUE_TRANSFER_BIT )
			&& !FBitSet( flags, VK_QUEUE_GRAPHICS_BIT ) && !FBitSet( flags, VK_QUEUE_COMPUTE_BIT ))
			info->transfer_family = i;
	}

	Mem_Free( families );

	if( info->props.apiVersion < VK_MIN_API_VERSION )
		info->reject = "Vulkan 1.3 not supported";
	else if( !info->features13.dynamicRendering || !info->features13.synchronization2 )
		info->reject = "no dynamic rendering or synchronization2";
	else if( !info->features12.descriptorIndexing || !info->features12.bufferDeviceAddress
		|| !info->features12.descriptorBindingPartiallyBound || !info->features12.descriptorBindingSampledImageUpdateAfterBind
		|| !info->features12.descriptorBindingUpdateUnusedWhilePending )
		info->reject = "no bindless descriptors or buffer device address";
	else if( !info->swapchain )
		info->reject = "no swapchain support";
	else if( info->graphics_family == UINT32_MAX )
		info->reject = "can't present to the window";
}

static int VK_DeviceScore( const vk_device_info_t *info )
{
	switch( info->props.deviceType )
	{
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 1000;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 100;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 50;
	default: return 10; // CPU implementations last
	}
}

static qboolean VK_PickPhysicalDevice( vk_device_info_t *picked )
{
	static const char *types[] = { "other", "integrated", "discrete", "virtual", "cpu" };
	VkPhysicalDevice *handles;
	vk_device_info_t info;
	uint32_t count = 0;
	int best = -1, best_score = 0;
	int forced = r_vk_device.value;

	if( vkEnumeratePhysicalDevices( vk.instance, &count, NULL ) != VK_SUCCESS || !count )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: no Vulkan device found\n" );
		return false;
	}

	handles = Mem_Malloc( r_temppool, count * sizeof( *handles ));
	vkEnumeratePhysicalDevices( vk.instance, &count, handles );

	for( uint32_t i = 0; i < count; i++ )
	{
		int score;

		VK_QueryDevice( handles[i], &info );

		gEngfuncs.Con_Printf( "ref_vk: device %u: %s (%s, Vulkan %u.%u, ray tracing: %s)%s%s\n", i,
			info.props.deviceName, types[Q_min( info.props.deviceType, ARRAYSIZE( types ) - 1 )],
			VK_API_VERSION_MAJOR( info.props.apiVersion ), VK_API_VERSION_MINOR( info.props.apiVersion ),
			info.caps.acceleration_structure && info.caps.ray_query ? ( info.caps.ray_tracing_pipeline ? "query + pipeline" : "query" ) : "no",
			info.reject ? ", unusable: " : "", info.reject ? info.reject : "" );

		if( info.reject )
			continue;

		score = VK_DeviceScore( &info );

		if( forced == (int)i )
			score = INT_MAX;

		if( score > best_score )
		{
			best_score = score;
			best = i;
			*picked = info;
		}
	}

	Mem_Free( handles );

	if( best < 0 )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: no device supports the Vulkan 1.3 baseline\n" );
		return false;
	}

	if( forced >= 0 && forced != best )
		gEngfuncs.Con_Printf( S_WARN "ref_vk: r_vk_device %d can't be used\n", forced );

	return true;
}

static qboolean VK_CreateDevice( const vk_device_info_t *info )
{
	const float priority = 1.0f;
	const char *extensions[2];
	uint32_t num_extensions = 0;
	uint32_t num_queues = 1;
	VkResult result;

	vk.physical_device = info->handle;
	vk.props = info->props;
	vk.caps = info->caps;
	vk.graphics_family = info->graphics_family;
	vk.transfer_family = info->transfer_family != UINT32_MAX ? info->transfer_family : info->graphics_family;
	vkGetPhysicalDeviceMemoryProperties( vk.physical_device, &vk.memory );

	extensions[num_extensions++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if( vk.caps.memory_budget )
		extensions[num_extensions++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;

	VkDeviceQueueCreateInfo queues[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = vk.graphics_family,
			.queueCount = 1,
			.pQueuePriorities = &priority,
		},
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = vk.transfer_family,
			.queueCount = 1,
			.pQueuePriorities = &priority,
		},
	};

	if( vk.transfer_family != vk.graphics_family )
		num_queues = 2;

	// the baseline, plus what later stories rely on when it is there
	VkPhysicalDeviceVulkan13Features features13 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.dynamicRendering = VK_TRUE,
		.synchronization2 = VK_TRUE,
		.maintenance4 = info->features13.maintenance4,
	};

	VkPhysicalDeviceVulkan12Features features12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &features13,
		.descriptorIndexing = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE,
		.runtimeDescriptorArray = info->features12.runtimeDescriptorArray,
		.descriptorBindingPartiallyBound = VK_TRUE,
		.descriptorBindingVariableDescriptorCount = info->features12.descriptorBindingVariableDescriptorCount,
		.shaderSampledImageArrayNonUniformIndexing = info->features12.shaderSampledImageArrayNonUniformIndexing,
		.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
		.descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
		.timelineSemaphore = info->features12.timelineSemaphore,
		.scalarBlockLayout = info->features12.scalarBlockLayout,
	};

	VkPhysicalDeviceFeatures2 features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features12,
		.features = {
			.samplerAnisotropy = info->features.features.samplerAnisotropy,
			.textureCompressionBC = info->features.features.textureCompressionBC,
			.fillModeNonSolid = info->features.features.fillModeNonSolid,
			.depthClamp = info->features.features.depthClamp,
		},
	};

	VkDeviceCreateInfo create = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.queueCreateInfoCount = num_queues,
		.pQueueCreateInfos = queues,
		.enabledExtensionCount = num_extensions,
		.ppEnabledExtensionNames = extensions,
	};

	result = vkCreateDevice( vk.physical_device, &create, NULL, &vk.device );

	if( result != VK_SUCCESS )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: vkCreateDevice failed (%d)\n", result );
		vk.device = VK_NULL_HANDLE;
		return false;
	}

	VK_LoadDeviceFuncs();

	vkGetDeviceQueue( vk.device, vk.graphics_family, 0, &vk.graphics_queue );
	vkGetDeviceQueue( vk.device, vk.transfer_family, 0, &vk.transfer_queue );

	VK_SetObjectName( VK_OBJECT_TYPE_DEVICE, (uint64_t)vk.device, "ref_vk device" );
	VK_SetObjectName( VK_OBJECT_TYPE_QUEUE, (uint64_t)vk.graphics_queue, "graphics queue" );
	if( vk.transfer_queue != vk.graphics_queue )
		VK_SetObjectName( VK_OBJECT_TYPE_QUEUE, (uint64_t)vk.transfer_queue, "transfer queue" );

	gEngfuncs.Con_Printf( "ref_vk: using %s, queue families: graphics %u, transfer %u%s\n",
		vk.props.deviceName, vk.graphics_family, vk.transfer_family,
		vk.transfer_family != vk.graphics_family ? " (dedicated)" : "" );

	return true;
}

/*
=================
VK_InitCore

expects a window created with R_Init_Video( REF_VULKAN )
=================
*/
qboolean VK_InitCore( void )
{
	vk_device_info_t device = { 0 };

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

	if( !VK_PickPhysicalDevice( &device ) || !VK_CreateDevice( &device ))
	{
		VK_ShutdownCore();
		return false;
	}

	return true;
}

/*
=================
VK_ShutdownCore
=================
*/
void VK_ShutdownCore( void )
{
	if( vk.device )
		vkDestroyDevice( vk.device, NULL );

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
