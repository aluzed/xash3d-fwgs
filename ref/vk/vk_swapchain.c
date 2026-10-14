/*
vk_swapchain.c - swapchain, frames in flight and the main render target
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

/*
 * Rendering never targets the swapchain directly: everything goes to an
 * offscreen colour + depth pair, copied to the swapchain image at the end
 * of the frame. Screenshots and later post-processing read the offscreen
 * image, whatever the swapchain format or state.
 *
 * The scene may be drawn into another colour target (the HDR scene of
 * vk_post.c, VK_SetColorTarget) sharing the main depth: vk_post.c turns it
 * into the main target's pixels before the 2D is drawn.
 */

#define VK_MAIN_COLOR_FORMAT VK_FORMAT_R8G8B8A8_UNORM

typedef struct vk_frame_s
{
	VkCommandPool pool;
	VkCommandBuffer cmd;
	VkFence fence;           // signaled when the GPU is done with this frame
	VkSemaphore acquired;    // swapchain image ready to be written
} vk_frame_t;

typedef struct vk_swapchain_s
{
	VkSwapchainKHR handle;
	VkFormat format;
	VkExtent2D extent;
	VkPresentModeKHR present_mode;
	uint32_t num_images;
	VkImage images[VK_MAX_SWAPCHAIN_IMAGES];
	VkSemaphore rendered[VK_MAX_SWAPCHAIN_IMAGES]; // one per image: presentation holds it
	float vsync; // gl_vsync value it was created with
} vk_swapchain_t;

static struct
{
	vk_frame_t frames[VK_FRAMES_IN_FLIGHT];
	uint32_t frame_index; // frame in flight being recorded
	uint64_t frame_count;

	vk_swapchain_t swapchain;
	qboolean recreate; // out of date or suboptimal, rebuild before the next frame

	vk_image_t color; // main render target
	vk_image_t depth;
	VkImageView depth_sample_view; // depth aspect only, for sampling
	VkFormat depth_format;

	vk_image_t *target;      // colour target of the pass, &color unless the scene goes elsewhere
	vk_target_t target_kind; // its pipelines

	qboolean recording; // between a successful VK_BeginFrame and VK_EndFrame
	qboolean in_pass;   // main colour + depth dynamic rendering is open
	vec4_t clear_color; // glClearColor state, the frame clear uses it
	VkViewport viewport;  // glViewport and glDepthRange of the open pass
	uint32_t image_index; // acquired swapchain image
} vk_frame;

VkFormat VK_MainColorFormat( void )
{
	return VK_MAIN_COLOR_FORMAT;
}

VkFormat VK_MainDepthFormat( void )
{
	return vk_frame.depth_format;
}

qboolean VK_FormatHasStencil( VkFormat format )
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

qboolean VK_FrameRecording( void )
{
	return vk_frame.recording;
}

uint64_t VK_CurrentFrame( void )
{
	return vk_frame.frame_count;
}

VkCommandBuffer VK_FrameCommands( void )
{
	return vk_frame.recording ? vk_frame.frames[vk_frame.frame_index].cmd : VK_NULL_HANDLE;
}

vk_image_t *VK_MainColorImage( void )
{
	return &vk_frame.color;
}

vk_image_t *VK_MainDepthImage( void )
{
	return &vk_frame.depth;
}

VkImageView VK_MainDepthSampleView( void )
{
	return vk_frame.depth_sample_view;
}

void VK_GetClearColor( vec3_t out )
{
	VectorCopy( vk_frame.clear_color, out );
}

/*
=================
VK_SetColorTarget

the colour target the next passes draw into, the same size as the main
one; NULL goes back to the main target
=================
*/
void VK_SetColorTarget( vk_image_t *img, vk_target_t kind )
{
	VK_EndMainPass();

	vk_frame.target = img ? img : &vk_frame.color;
	vk_frame.target_kind = img ? kind : VK_TARGET_LDR;
}

vk_target_t VK_ColorTarget( void )
{
	return vk_frame.target_kind;
}

void VK_MainExtent( int *width, int *height )
{
	*width = vk_frame.color.width;
	*height = vk_frame.color.height;
}

/*
=================
VK_BeginMainPass

opens dynamic rendering on the main target if needed, returns the command
buffer to record draws in, or NULL outside a frame
=================
*/
VkCommandBuffer VK_BeginMainPass( void )
{
	VkCommandBuffer cmd;

	if( !vk_frame.recording )
		return VK_NULL_HANDLE;

	cmd = vk_frame.frames[vk_frame.frame_index].cmd;

	if( vk_frame.in_pass )
		return cmd;

	VK_ImageBarrier( cmd, vk_frame.target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	VK_ImageBarrier( cmd, &vk_frame.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );

	VkRenderingAttachmentInfo color = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = vk_frame.target->view,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};

	VkRenderingAttachmentInfo depth = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = vk_frame.depth.view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};

	VkRenderingInfo info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { { 0, 0 }, { vk_frame.color.width, vk_frame.color.height } },
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color,
		.pDepthAttachment = &depth,
		.pStencilAttachment = VK_FormatHasStencil( vk_frame.depth_format ) ? &depth : NULL,
	};

	vkCmdBeginRendering( cmd, &info );

	// negative height: GL's bottom-up Y, so GL matrices work unchanged
	VkViewport viewport = {
		.x = 0.0f,
		.y = (float)vk_frame.color.height,
		.width = (float)vk_frame.color.width,
		.height = -(float)vk_frame.color.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor = { { 0, 0 }, { vk_frame.color.width, vk_frame.color.height } };

	vk_frame.viewport = viewport;
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	vkCmdSetScissor( cmd, 0, 1, &scissor );

	vk_frame.in_pass = true;
	return cmd;
}

/*
=================
VK_SetViewport

x, y, width, height as glViewport takes them: from the bottom left corner
=================
*/
void VK_SetViewport( int x, int y, int width, int height )
{
	VkCommandBuffer cmd = vk_frame.frames[vk_frame.frame_index].cmd;
	const int target_height = vk_frame.color.height;

	if( !vk_frame.in_pass || width <= 0 || height <= 0 )
		return;

	VkViewport viewport = {
		.x = (float)x,
		.y = (float)( target_height - y ),
		.width = (float)width,
		.height = -(float)height,
		.minDepth = vk_frame.viewport.minDepth,
		.maxDepth = vk_frame.viewport.maxDepth,
	};

	VkRect2D scissor = {
		{ Q_max( x, 0 ), Q_max( target_height - y - height, 0 ) },
		{ width, height },
	};

	vk_frame.viewport = viewport;
	vkCmdSetViewport( cmd, 0, 1, &viewport );
	vkCmdSetScissor( cmd, 0, 1, &scissor );
}

// the frame in flight being recorded
int VK_FrameIndex( void )
{
	return vk_frame.frame_index;
}

// glDepthRange, kept until changed again
void VK_SetDepthRange( float min, float max )
{
	VkCommandBuffer cmd = vk_frame.frames[vk_frame.frame_index].cmd;

	vk_frame.viewport.minDepth = min;
	vk_frame.viewport.maxDepth = max;

	if( vk_frame.in_pass )
		vkCmdSetViewport( cmd, 0, 1, &vk_frame.viewport );
}

void VK_ResetViewport( void )
{
	VK_SetViewport( 0, 0, vk_frame.color.width, vk_frame.color.height );
}

// glClear( GL_DEPTH_BUFFER_BIT ) inside the pass, the whole target like GL
void VK_ClearMainDepth( void )
{
	VkCommandBuffer cmd = VK_BeginMainPass();

	if( !cmd )
		return;

	VkClearAttachment clear = {
		.aspectMask = vk_frame.depth.aspect,
		.clearValue.depthStencil = { 1.0f, 0 },
	};

	VkClearRect rect = {
		.rect = { { 0, 0 }, { vk_frame.color.width, vk_frame.color.height } },
		.layerCount = 1,
	};

	vkCmdClearAttachments( cmd, 1, &clear, 1, &rect );
}

void VK_EndMainPass( void )
{
	VK_ImmFlush();

	if( !vk_frame.in_pass )
		return;

	vkCmdEndRendering( vk_frame.frames[vk_frame.frame_index].cmd );
	vk_frame.in_pass = false;
}

/*
=================
Render targets
=================
*/
static VkFormat VK_PickDepthFormat( void )
{
	static const VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT };

	for( size_t i = 0; i < ARRAYSIZE( candidates ); i++ )
	{
		VkFormatProperties props;

		vkGetPhysicalDeviceFormatProperties( vk.physical_device, candidates[i], &props );

		if( FBitSet( props.optimalTilingFeatures, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ))
			return candidates[i];
	}

	return VK_FORMAT_UNDEFINED;
}

static void VK_DestroyRenderTargets( void )
{
	if( vk_frame.depth_sample_view )
		vkDestroyImageView( vk.device, vk_frame.depth_sample_view, NULL );
	vk_frame.depth_sample_view = VK_NULL_HANDLE;

	VK_DestroyImage( &vk_frame.color );
	VK_DestroyImage( &vk_frame.depth );
}

static qboolean VK_CreateRenderTargets( uint32_t width, uint32_t height )
{
	VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

	if( vk_frame.depth_format != VK_FORMAT_D32_SFLOAT )
		SetBits( depth_aspect, VK_IMAGE_ASPECT_STENCIL_BIT );

	if( !VK_CreateImage( &vk_frame.color, "main color", width, height, VK_MAIN_COLOR_FORMAT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT ))
		return false;

	// sampled by the post-processing (SSAO)
	if( !VK_CreateImage( &vk_frame.depth, "main depth", width, height, vk_frame.depth_format,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, depth_aspect ))
	{
		VK_DestroyImage( &vk_frame.color );
		return false;
	}

	VkImageViewCreateInfo view = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = vk_frame.depth.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = vk_frame.depth_format,
		.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
	};

	if( vkCreateImageView( vk.device, &view, NULL, &vk_frame.depth_sample_view ) != VK_SUCCESS )
	{
		VK_DestroyRenderTargets();
		return false;
	}

	VK_SetObjectName( VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)vk_frame.depth_sample_view, "main depth sample view" );
	return true;
}

/*
=================
Swapchain
=================
*/
static VkPresentModeKHR VK_PickPresentMode( float vsync )
{
	VkPresentModeKHR modes[16];
	uint32_t count = ARRAYSIZE( modes );
	qboolean mailbox = false, immediate = false;

	// FIFO is always there and is what vsync means
	if( vsync )
		return VK_PRESENT_MODE_FIFO_KHR;

	vkGetPhysicalDeviceSurfacePresentModesKHR( vk.physical_device, vk.surface, &count, modes );

	for( uint32_t i = 0; i < count; i++ )
	{
		if( modes[i] == VK_PRESENT_MODE_MAILBOX_KHR )
			mailbox = true;
		else if( modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR )
			immediate = true;
	}

	// no vsync like ref_gl: never wait for the display, prefer no tearing
	if( mailbox )
		return VK_PRESENT_MODE_MAILBOX_KHR;
	if( immediate )
		return VK_PRESENT_MODE_IMMEDIATE_KHR;
	return VK_PRESENT_MODE_FIFO_KHR;
}

static VkSurfaceFormatKHR VK_PickSurfaceFormat( void )
{
	VkSurfaceFormatKHR formats[64];
	uint32_t count = ARRAYSIZE( formats );

	vkGetPhysicalDeviceSurfaceFormatsKHR( vk.physical_device, vk.surface, &count, formats );

	// the engine applies gamma itself, so the swapchain must not be sRGB
	for( uint32_t i = 0; i < count; i++ )
	{
		if(( formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM )
			&& formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
			return formats[i];
	}

	return formats[0];
}

static void VK_DestroySwapchainResources( void )
{
	for( uint32_t i = 0; i < vk_frame.swapchain.num_images; i++ )
	{
		if( vk_frame.swapchain.rendered[i] )
			vkDestroySemaphore( vk.device, vk_frame.swapchain.rendered[i], NULL );
	}

	if( vk_frame.swapchain.handle )
		vkDestroySwapchainKHR( vk.device, vk_frame.swapchain.handle, NULL );

	memset( &vk_frame.swapchain, 0, sizeof( vk_frame.swapchain ));
	VK_DestroyRenderTargets();
}

/*
=================
VK_CreateSwapchain

returns false when the window has no area (minimised), the caller skips the frame
=================
*/
static qboolean VK_CreateSwapchain( int width, int height )
{
	VkSurfaceCapabilitiesKHR caps;
	VkSurfaceFormatKHR format;
	VkSwapchainKHR old = vk_frame.swapchain.handle;
	vk_swapchain_t *sc = &vk_frame.swapchain;
	uint32_t num_images;
	VkResult result;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vk.physical_device, vk.surface, &caps );

	// the surface size wins when the platform fixes it
	if( caps.currentExtent.width != UINT32_MAX )
	{
		width = caps.currentExtent.width;
		height = caps.currentExtent.height;
	}

	if( width <= 0 || height <= 0 )
		return false;

	width = bound( caps.minImageExtent.width, (uint32_t)width, caps.maxImageExtent.width );
	height = bound( caps.minImageExtent.height, (uint32_t)height, caps.maxImageExtent.height );

	num_images = Q_max( caps.minImageCount + 1, VK_FRAMES_IN_FLIGHT + 1 );
	if( caps.maxImageCount )
		num_images = Q_min( num_images, caps.maxImageCount );
	num_images = Q_min( num_images, VK_MAX_SWAPCHAIN_IMAGES );

	format = VK_PickSurfaceFormat();

	VkSwapchainCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = vk.surface,
		.minImageCount = num_images,
		.imageFormat = format.format,
		.imageColorSpace = format.colorSpace,
		.imageExtent = { width, height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = caps.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PickPresentMode( gl_vsync->value ),
		.clipped = VK_TRUE,
		.oldSwapchain = old,
	};

	// the old swapchain and its images may still be in use
	vkDeviceWaitIdle( vk.device );

	VkSwapchainKHR handle;
	result = vkCreateSwapchainKHR( vk.device, &info, NULL, &handle );

	sc->handle = VK_NULL_HANDLE; // the old one is destroyed below either way
	if( old )
		vkDestroySwapchainKHR( vk.device, old, NULL );
	for( uint32_t i = 0; i < sc->num_images; i++ )
		vkDestroySemaphore( vk.device, sc->rendered[i], NULL );
	memset( sc, 0, sizeof( *sc ));
	VK_DestroyRenderTargets();

	if( result != VK_SUCCESS )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: vkCreateSwapchainKHR failed (%d)\n", result );
		return false;
	}

	sc->handle = handle;
	sc->format = format.format;
	sc->extent = info.imageExtent;
	sc->present_mode = info.presentMode;
	sc->vsync = gl_vsync->value;
	sc->num_images = VK_MAX_SWAPCHAIN_IMAGES;
	vkGetSwapchainImagesKHR( vk.device, sc->handle, &sc->num_images, sc->images );

	VkSemaphoreCreateInfo sem = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	for( uint32_t i = 0; i < sc->num_images; i++ )
	{
		vkCreateSemaphore( vk.device, &sem, NULL, &sc->rendered[i] );
		VK_SetObjectName( VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)sc->rendered[i], "swapchain %u rendered", i );
		VK_SetObjectName( VK_OBJECT_TYPE_IMAGE, (uint64_t)sc->images[i], "swapchain image %u", i );
	}

	VK_SetObjectName( VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)sc->handle, "swapchain" );

	if( !VK_CreateRenderTargets( width, height ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the %dx%d render targets\n", width, height );
		VK_DestroySwapchainResources();
		return false;
	}

	vk_frame.recreate = false;

	gEngfuncs.Con_Reportf( "ref_vk: swapchain %ux%u, %u images, present mode %d\n",
		sc->extent.width, sc->extent.height, sc->num_images, sc->present_mode );

	return true;
}

/*
=================
VK_PrepareSwapchain

creates or rebuilds the swapchain when needed, false when there is nothing to draw on
=================
*/
static qboolean VK_PrepareSwapchain( void );

// at renderer init, like a GL context: not on the first frame drawn
void VK_CreateInitialSwapchain( void )
{
	VK_PrepareSwapchain();
}

static qboolean VK_PrepareSwapchain( void )
{
	int width, height;

	gEngfuncs.Vulkan_GetDrawableSize( &width, &height );

	// minimised: don't touch the swapchain, it may report a zero extent
	if( width <= 0 || height <= 0 )
		return false;

	if( vk_frame.swapchain.handle && !vk_frame.recreate
		&& vk_frame.swapchain.extent.width == (uint32_t)width
		&& vk_frame.swapchain.extent.height == (uint32_t)height
		&& vk_frame.swapchain.vsync == gl_vsync->value )
		return true;

	return VK_CreateSwapchain( width, height );
}

/*
=================
Frames in flight
=================
*/
void VK_SetClearColor( float r, float g, float b )
{
	VectorSet( vk_frame.clear_color, r, g, b );
}

qboolean VK_InitFrames( void )
{
	memset( &vk_frame, 0, sizeof( vk_frame ));
	VK_SetClearColor( 0.5f, 0.5f, 0.5f ); // GL_SetDefaultState

	vk_frame.depth_format = VK_PickDepthFormat();
	vk_frame.target = &vk_frame.color;

	if( vk_frame.depth_format == VK_FORMAT_UNDEFINED )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: no usable depth format\n" );
		return false;
	}

	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
	{
		vk_frame_t *frame = &vk_frame.frames[i];

		VkCommandPoolCreateInfo pool = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = vk.graphics_family,
		};

		VkFenceCreateInfo fence = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};

		VkSemaphoreCreateInfo sem = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		if( vkCreateCommandPool( vk.device, &pool, NULL, &frame->pool ) != VK_SUCCESS
			|| vkCreateFence( vk.device, &fence, NULL, &frame->fence ) != VK_SUCCESS
			|| vkCreateSemaphore( vk.device, &sem, NULL, &frame->acquired ) != VK_SUCCESS )
		{
			gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the frame %d objects\n", i );
			VK_ShutdownFrames();
			return false;
		}

		VkCommandBufferAllocateInfo alloc = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = frame->pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		vkAllocateCommandBuffers( vk.device, &alloc, &frame->cmd );

		VK_SetObjectName( VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)frame->pool, "frame %d pool", i );
		VK_SetObjectName( VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)frame->cmd, "frame %d commands", i );
		VK_SetObjectName( VK_OBJECT_TYPE_FENCE, (uint64_t)frame->fence, "frame %d fence", i );
		VK_SetObjectName( VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)frame->acquired, "frame %d acquired", i );
	}

	return true;
}

void VK_ShutdownFrames( void )
{
	if( !vk.device )
		return;

	vkDeviceWaitIdle( vk.device );

	VK_DestroySwapchainResources();

	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
	{
		vk_frame_t *frame = &vk_frame.frames[i];

		if( frame->pool )
			vkDestroyCommandPool( vk.device, frame->pool, NULL );
		if( frame->fence )
			vkDestroyFence( vk.device, frame->fence, NULL );
		if( frame->acquired )
			vkDestroySemaphore( vk.device, frame->acquired, NULL );
	}

	memset( &vk_frame, 0, sizeof( vk_frame ));
}

static void VK_BeginCommands( vk_frame_t *frame )
{
	VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkResetCommandPool( vk.device, frame->pool, 0 );
	vkBeginCommandBuffer( frame->cmd, &begin );
}

static void VK_ClearColor( VkCommandBuffer cmd, float r, float g, float b )
{
	VkClearColorValue color = { .float32 = { r, g, b, 1.0f } };
	VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	VK_ImageBarrier( cmd, &vk_frame.color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	vkCmdClearColorImage( cmd, vk_frame.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range );
}

static void VK_ClearDepth( VkCommandBuffer cmd )
{
	VkClearDepthStencilValue value = { .depth = 1.0f, .stencil = 0 };
	VkImageSubresourceRange range = { vk_frame.depth.aspect, 0, 1, 0, 1 };

	VK_ImageBarrier( cmd, &vk_frame.depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	vkCmdClearDepthStencilImage( cmd, vk_frame.depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range );
}

/*
=================
VK_BeginFrame
=================
*/
void VK_BeginFrame( qboolean clear_scene )
{
	vk_frame_t *frame;
	VkResult result;

	// the engine may begin a frame without ending the previous one (level
	// change): submit it, or its fence would stay unsignaled forever
	if( vk_frame.recording )
		VK_EndFrame();

	frame = &vk_frame.frames[vk_frame.frame_index];

	if( !VK_PrepareSwapchain( ))
		return; // minimised, the engine keeps ticking without drawing

	vkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX );

	// a window the compositor doesn't show may never release an image:
	// skip the frame rather than block the engine
	result = vkAcquireNextImageKHR( vk.device, vk_frame.swapchain.handle, 1000000000ull,
		frame->acquired, VK_NULL_HANDLE, &vk_frame.image_index );

	if( result == VK_TIMEOUT || result == VK_NOT_READY )
		return;

	if( result == VK_ERROR_OUT_OF_DATE_KHR )
	{
		vk_frame.recreate = true;
		return;
	}

	if( result == VK_SUBOPTIMAL_KHR )
		vk_frame.recreate = true; // still usable for this frame
	else if( result != VK_SUCCESS )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: vkAcquireNextImageKHR failed (%d)\n", result );
		return;
	}

	// only reset once a submit is certain to follow
	vkResetFences( vk.device, 1, &frame->fence );

	// this slot's previous frame is done: so is everything recorded before it
	if( vk_frame.frame_count >= VK_FRAMES_IN_FLIGHT )
		VK_RunDeferred( vk_frame.frame_count - VK_FRAMES_IN_FLIGHT );
	VK_BeginDynamic( vk_frame.frame_index );
	VK_FogBeginFrame( vk_frame.frame_index );
	VK_StreamBeginFrame();    // may switch the textures of materials
	VK_MaterialsBeginFrame();
	VK_WorldDataBeginFrame( vk_frame.frame_index );
	VK_CheckTextureParams();
	VK_FlushUploads(); // textures loaded since the last frame
	VK_PostBeginFrame(); // may wait for the GPU to rebuild its targets

	VK_BeginCommands( frame );
	vk_frame.recording = true;
	vk_frame.in_pass = false;
	vk_frame.target = &vk_frame.color;
	vk_frame.target_kind = VK_TARGET_LDR;

	VK_BeginLabel( frame->cmd, "frame" );
	VK_ProfileBeginFrame( frame->cmd, vk_frame.frame_index );

	// ref_gl only clears on request, the previous frame stays otherwise;
	// a new target has no contents yet and is always cleared
	if( vk_frame.color.layout == VK_IMAGE_LAYOUT_UNDEFINED || (( gl_clear->value || ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
		&& clear_scene && ENGINE_GET_PARM( PARM_CONNSTATE ) != ca_cinematic ))
		VK_ClearColor( frame->cmd, vk_frame.clear_color[0], vk_frame.clear_color[1], vk_frame.clear_color[2] );
	VK_ClearDepth( frame->cmd );

	VK_ImmBeginFrame();
}

/*
=================
VK_ClearScreen
=================
*/
void VK_ClearScreen( void )
{
	if( !vk_frame.recording )
		return;

	VK_EndMainPass();

	// ref_gl leaves black as the clear colour afterwards
	VK_SetClearColor( 0.0f, 0.0f, 0.0f );
	VK_ClearColor( vk_frame.frames[vk_frame.frame_index].cmd, 0.0f, 0.0f, 0.0f );
}

/*
=================
VK_EndFrame
=================
*/
void VK_EndFrame( void )
{
	vk_frame_t *frame = &vk_frame.frames[vk_frame.frame_index];
	vk_swapchain_t *sc = &vk_frame.swapchain;
	VkImage target;
	VkResult result;

	if( !vk_frame.recording )
		return;

	VK_ImmEndFrame();
	VK_SetColorTarget( NULL, VK_TARGET_LDR ); // ends the pass
	VK_FlushUploads(); // the frame may sample textures updated while it was recorded
	VK_ProfileMark( frame->cmd, VK_PROF_PRESENT_COPY );

	vk_frame.recording = false;
	target = sc->images[vk_frame.image_index];

	VK_BeginLabel( frame->cmd, "present copy" );
	VK_ImageBarrier( frame->cmd, &vk_frame.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
	VK_ImageBarrierRaw( frame->cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

	// a blit converts RGBA to the swapchain BGRA; the sizes always match
	VkImageBlit blit = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.srcOffsets = { { 0, 0, 0 }, { vk_frame.color.width, vk_frame.color.height, 1 } },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstOffsets = { { 0, 0, 0 }, { sc->extent.width, sc->extent.height, 1 } },
	};

	vkCmdBlitImage( frame->cmd, vk_frame.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST );

	VK_ImageBarrierRaw( frame->cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
	VK_EndLabel( frame->cmd );

	VK_EndLabel( frame->cmd ); // frame
	VK_ProfileEndFrame( frame->cmd );
	vkEndCommandBuffer( frame->cmd );

	VkSemaphoreSubmitInfo wait = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = frame->acquired,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	};

	VkSemaphoreSubmitInfo signal = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = sc->rendered[vk_frame.image_index],
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	};

	VkCommandBufferSubmitInfo cmd = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = frame->cmd,
	};

	VkSubmitInfo2 submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &wait,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmd,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &signal,
	};

	result = vkQueueSubmit2( vk.graphics_queue, 1, &submit, frame->fence );

	if( result != VK_SUCCESS )
		gEngfuncs.Host_Error( "ref_vk: vkQueueSubmit2 failed (%d)\n", result );

	VkPresentInfoKHR present = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &sc->rendered[vk_frame.image_index],
		.swapchainCount = 1,
		.pSwapchains = &sc->handle,
		.pImageIndices = &vk_frame.image_index,
	};

	result = vkQueuePresentKHR( vk.graphics_queue, &present );

	if( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR )
		vk_frame.recreate = true;
	else if( result != VK_SUCCESS )
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: vkQueuePresentKHR failed (%d)\n", result );

	vk_frame.frame_index = ( vk_frame.frame_index + 1 ) % VK_FRAMES_IN_FLIGHT;
	vk_frame.frame_count++;
}

/*
=================
VK_FlushFrame

submits what the frame recorded so far and waits for it, then resumes
recording: the engine asks for screenshots in the middle of a frame
=================
*/
static void VK_FlushFrame( void )
{
	vk_frame_t *frame = &vk_frame.frames[vk_frame.frame_index];

	VK_EndLabel( frame->cmd ); // frame
	vkEndCommandBuffer( frame->cmd );

	VkCommandBufferSubmitInfo cmd = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = frame->cmd,
	};

	// no semaphores: the swapchain image isn't touched yet, the final
	// submit of the frame still waits for the acquire
	VkSubmitInfo2 submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmd,
	};

	vkQueueSubmit2( vk.graphics_queue, 1, &submit, frame->fence );
	vkWaitForFences( vk.device, 1, &frame->fence, VK_TRUE, UINT64_MAX );
	vkResetFences( vk.device, 1, &frame->fence );

	VK_BeginCommands( frame );
	VK_BeginLabel( frame->cmd, "frame" );
}

/*
=================
VK_ReadPixels

copies a rectangle of the frame so far into rgba (w * h * 4 bytes, rows
top to bottom), waiting for the GPU
=================
*/
qboolean VK_ReadPixels( int x, int y, int w, int h, byte *rgba )
{
	vk_frame_t *frame = &vk_frame.frames[vk_frame.frame_index];
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkMemoryRequirements req;
	void *data;
	int type;

	if( !vk_frame.recording || w <= 0 || h <= 0 || x < 0 || y < 0
		|| x + w > (int)vk_frame.color.width || y + h > (int)vk_frame.color.height )
		return false;

	VK_EndMainPass();
	VK_FlushUploads();

	const VkDeviceSize size = (VkDeviceSize)w * h * 4;

	VkBufferCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	if( vkCreateBuffer( vk.device, &info, NULL, &buffer ) != VK_SUCCESS )
		return false;

	vkGetBufferMemoryRequirements( vk.device, buffer, &req );
	type = VK_FindMemoryType( req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

	VkMemoryAllocateInfo alloc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = req.size,
		.memoryTypeIndex = type,
	};

	if( type < 0 || vkAllocateMemory( vk.device, &alloc, NULL, &memory ) != VK_SUCCESS )
	{
		vkDestroyBuffer( vk.device, buffer, NULL );
		return false;
	}

	vkBindBufferMemory( vk.device, buffer, memory, 0 );
	VK_SetObjectName( VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, "pixel readback" );

	VK_BeginLabel( frame->cmd, "read pixels" );
	VK_ImageBarrier( frame->cmd, &vk_frame.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );

	VkBufferImageCopy copy = {
		.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.imageOffset = { x, y, 0 },
		.imageExtent = { w, h, 1 },
	};

	vkCmdCopyImageToBuffer( frame->cmd, vk_frame.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy );
	VK_EndLabel( frame->cmd );

	VK_FlushFrame();

	vkMapMemory( vk.device, memory, 0, size, 0, &data );
	memcpy( rgba, data, size );
	vkUnmapMemory( vk.device, memory );

	vkDestroyBuffer( vk.device, buffer, NULL );
	vkFreeMemory( vk.device, memory, NULL );

	return true;
}

/*
=================
VK_ScreenShot
=================
*/
qboolean VK_ScreenShot( const char *filename, int shot_type )
{
	uint flags = 0;
	int width = 0, height = 0;
	rgbdata_t *shot;
	qboolean result;

	const uint32_t w = vk_frame.color.width, h = vk_frame.color.height;
	const size_t size = (size_t)w * h * 4;

	// rows are top to bottom already, unlike glReadPixels
	shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
	shot->width = w;
	shot->height = h;
	shot->flags = IMAGE_HAS_COLOR;
	shot->type = PF_RGBA_32;
	shot->size = size;
	shot->buffer = Mem_Malloc( r_temppool, size );

	if( !VK_ReadPixels( 0, 0, w, h, shot->buffer ))
	{
		gEngfuncs.FS_FreeImage( shot );
		return false;
	}

	switch( shot_type )
	{
	case VID_SCREENSHOT:
		break;
	case VID_SNAPSHOT:
		gEngfuncs.fsapi->AllowDirectPaths( true );
		break;
	case VID_LEVELSHOT:
	case VID_MINISHOT:
		flags |= IMAGE_RESAMPLE;
		height = shot_type == VID_MINISHOT ? 200 : 480;
		width = Q_rint( height * ((double)shot->width / shot->height ));
		break;
	case VID_MAPSHOT:
		flags |= IMAGE_RESAMPLE|IMAGE_QUANTIZE; // GoldSrc requests overviews in 8-bit format
		height = 768;
		width = 1024;
		break;
	}

	gEngfuncs.Image_Process( &shot, width, height, flags, 0.0f );

	result = gEngfuncs.FS_SaveImage( filename, shot );
	gEngfuncs.fsapi->AllowDirectPaths( false );
	gEngfuncs.FS_FreeImage( shot );

	return result;
}

/*
=================
VK_CubemapShot

ref_gl/gl_backend.c VID_CubemapShot: six 90 degree views drawn in the
bottom left corner of the frame and read back one by one. glReadPixels
gives rows bottom to top and the side flags expect it, so the rows are
turned over the same way.
=================
*/
// used for 'env' and 'sky' shots
typedef struct envmap_s
{
	vec3_t	angles;
	int	flags;
} envmap_t;

static const envmap_t r_skyBoxInfo[6] =
{
{{   0, 270, 180}, IMAGE_FLIP_X },
{{   0,  90, 180}, IMAGE_FLIP_X },
{{ -90,   0, 180}, IMAGE_FLIP_X },
{{  90,   0, 180}, IMAGE_FLIP_X },
{{   0,   0, 180}, IMAGE_FLIP_X },
{{   0, 180, 180}, IMAGE_FLIP_X },
};

static const envmap_t r_envMapInfo[6] =
{
{{  0,   0,  90}, 0 },
{{  0, 180, -90}, 0 },
{{  0,  90,   0}, 0 },
{{  0, 270, 180}, 0 },
{{-90, 180, -90}, 0 },
{{ 90,   0,  90}, 0 }
};

qboolean VK_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || !WORLDMODEL || !vk_frame.recording )
		return false;

	// make sure the specified size is valid
	uint i = 1;
	while( i < size ) i <<= 1;

	if( i != size ) return false;
	if( size > gpGlobals->width || size > gpGlobals->height )
		return false;
	if( size > vk_frame.color.width || size > vk_frame.color.height )
		return false;

	// alloc space
	byte *rgba = Mem_Malloc( r_temppool, size * size * 4 );
	byte *temp = Mem_Malloc( r_temppool, size * size * 3 );
	byte *buffer = Mem_Malloc( r_temppool, size * size * 3 * 6 );
	rgbdata_t *r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
	rgbdata_t *r_side = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
	qboolean ok = true;

	// use client vieworg
	if( !vieworg ) vieworg = RI.rvp.vieworigin;

	for( i = 0; i < 6 && ok; i++ )
	{
		// go into 3d mode
		R_Set2DMode( false );

		int flags;
		if( skyshot )
		{
			R_DrawCubemapView( vieworg, r_skyBoxInfo[i].angles, size );
			flags = r_skyBoxInfo[i].flags;
		}
		else
		{
			R_DrawCubemapView( vieworg, r_envMapInfo[i].angles, size );
			flags = r_envMapInfo[i].flags;
		}

		// the bottom left corner, read bottom row first like glReadPixels
		ok = VK_ReadPixels( 0, vk_frame.color.height - size, size, size, rgba );
		for( uint row = 0; row < size && ok; row++ )
		{
			const byte *src = rgba + ( size - 1 - row ) * size * 4;
			byte *dst = temp + row * size * 3;

			for( uint x = 0; x < size; x++ )
			{
				dst[x * 3 + 0] = src[x * 4 + 0];
				dst[x * 3 + 1] = src[x * 4 + 1];
				dst[x * 3 + 2] = src[x * 4 + 2];
			}
		}

		r_side->flags = IMAGE_HAS_COLOR;
		r_side->width = r_side->height = size;
		r_side->type = PF_RGB_24;
		r_side->size = r_side->width * r_side->height * 3;
		r_side->buffer = temp;

		if( flags ) gEngfuncs.Image_Process( &r_side, 0, 0, flags, 0.0f );
		memcpy( buffer + (size * size * 3 * i), r_side->buffer, size * size * 3 );
	}

	Mem_Free( rgba );

	r_shot->flags = IMAGE_HAS_COLOR;
	r_shot->flags |= (skyshot) ? IMAGE_SKYBOX : IMAGE_CUBEMAP;
	r_shot->width = size;
	r_shot->height = size;
	r_shot->type = PF_RGB_24;
	r_shot->size = r_shot->width * r_shot->height * 3 * 6;
	r_shot->palette = NULL;
	r_shot->buffer = buffer;

	// make sure what we have right extension
	string basename;
	Q_strncpy( basename, base, sizeof( basename ));
	COM_ReplaceExtension( basename, ".tga", sizeof( basename ));

	// write image as 6 sides
	int result = ok && gEngfuncs.FS_SaveImage( basename, r_shot );
	gEngfuncs.FS_FreeImage( r_shot );
	gEngfuncs.FS_FreeImage( r_side );

	return result;
}
