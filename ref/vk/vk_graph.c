/*
vk_graph.c - images of the frame, their uses and the barriers between them
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
A minimal render graph for a renderer that records as the engine calls
it: the passes are not known in advance, but every pass says how it uses
each image before using it, and the graph does the rest.

- VK_GraphUse: an image is about to be used as a colour or depth target,
  sampled, written by a compute shader, copied from or to. The image
  remembers its last use (layout, stages, accesses); the barrier between
  the two is the one they need, nothing when two reads follow each other
  in the same layout. vk_swapchain.c (the scene and 2D passes, clears,
  readbacks, the present copy) and vk_post.c (the compute chain) go
  through it.
- VK_GraphBeginRendering: a graphics pass on a colour and a depth image.
- VK_GraphImage: the transient images of the frame (the HDR scene and the
  post-processing ones), asked for by name with a format and a size
  relative to the main target. They are made on first use, made again
  when the main target changes size, and given back once nobody asked for
  them for a few seconds (r_vk_quality back to classic, for instance).
*/

#include "vk_local.h"

#define VK_GRAPH_MAX_IMAGES 32
#define VK_GRAPH_IDLE_TIME  3.0 // seconds before an image nobody asks for is freed

typedef struct
{
	VkImageLayout layout;
	VkImageLayout depth_layout; // for depth images, when it differs
	VkPipelineStageFlags2 stages;
	VkAccessFlags2 access;
	qboolean writes;
} vk_use_info_t;

static const vk_use_info_t vk_uses[VK_USE_COUNT] =
{
	[VK_USE_NONE] = { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, false },
	[VK_USE_COLOR_TARGET] = { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, true },
	[VK_USE_DEPTH_TARGET] = { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, true },
	[VK_USE_SAMPLED] = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, false },
	[VK_USE_STORAGE] = { VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true },
	[VK_USE_COPY_SRC] = { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
		VK_ACCESS_2_TRANSFER_READ_BIT, false },
	[VK_USE_COPY_DST] = { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT, true },
};

typedef struct
{
	char name[32];
	vk_image_t image;
	VkFormat format;
	VkImageUsageFlags usage;
	int shift;          // size: the main target's divided by 2^shift, rounded up
	double last_asked;
} vk_graph_image_t;

static struct
{
	vk_graph_image_t images[VK_GRAPH_MAX_IMAGES];
	int width, height;  // of the main target the images were made for
	int generation;     // bumped when images are made again
} vk_graph;

static VkImageLayout VK_UseLayout( const vk_image_t *img, vk_use_t use )
{
	return FBitSet( img->aspect, VK_IMAGE_ASPECT_DEPTH_BIT ) ? vk_uses[use].depth_layout : vk_uses[use].layout;
}

/*
=================
VK_GraphUse

the barrier from the image's last use to this one: after a write, before
a write, or for a layout change; none between two reads in one layout
=================
*/
void VK_GraphUse( VkCommandBuffer cmd, vk_image_t *img, vk_use_t use )
{
	const vk_use_info_t *prev = &vk_uses[img->use];
	const vk_use_info_t *next = &vk_uses[use];
	const VkImageLayout layout = VK_UseLayout( img, use );

	if( img->layout == layout && !prev->writes && !next->writes && img->use != VK_USE_NONE )
	{
		img->use = use;
		return;
	}

	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		// an image no pass has used yet: whatever came before is done with it
		.srcStageMask = img->use == VK_USE_NONE ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : prev->stages,
		.srcAccessMask = img->use == VK_USE_NONE ? VK_ACCESS_2_MEMORY_WRITE_BIT : ( prev->writes ? prev->access : VK_ACCESS_2_NONE ),
		.dstStageMask = next->stages,
		.dstAccessMask = next->access,
		.oldLayout = img->layout,
		.newLayout = layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img->image,
		.subresourceRange = { img->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
	};

	VkDependencyInfo dep = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2( cmd, &dep );
	img->layout = layout;
	img->use = use;
}

// the contents don't matter: the next use starts from UNDEFINED
void VK_GraphDiscard( vk_image_t *img )
{
	img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
	img->use = VK_USE_NONE;
}

/*
=================
VK_GraphBeginRendering

a graphics pass drawing into color and depth (either may be NULL), their
contents kept
=================
*/
void VK_GraphBeginRendering( VkCommandBuffer cmd, vk_image_t *color, vk_image_t *depth, qboolean stencil )
{
	VkRenderingAttachmentInfo color_info = { 0 }, depth_info = { 0 };
	const vk_image_t *size = color ? color : depth;

	if( color )
	{
		VK_GraphUse( cmd, color, VK_USE_COLOR_TARGET );
		color_info = (VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = color->view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		};
	}

	if( depth )
	{
		VK_GraphUse( cmd, depth, VK_USE_DEPTH_TARGET );
		depth_info = (VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depth->view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		};
	}

	VkRenderingInfo info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { { 0, 0 }, { size->width, size->height } },
		.layerCount = 1,
		.colorAttachmentCount = color ? 1 : 0,
		.pColorAttachments = color ? &color_info : NULL,
		.pDepthAttachment = depth ? &depth_info : NULL,
		.pStencilAttachment = depth && stencil ? &depth_info : NULL,
	};

	vkCmdBeginRendering( cmd, &info );
}

/*
=================
Transient images
=================
*/
static void VK_GraphReleaseImage( void *data )
{
	vk_image_t *img = data;

	VK_DestroyImage( img );
	Mem_Free( img );
}

// the frames in flight may still use it
static void VK_GraphFreeImage( vk_graph_image_t *gi )
{
	vk_image_t *copy;

	if( !gi->image.image )
		return;

	copy = Mem_Malloc( r_temppool, sizeof( *copy ));
	*copy = gi->image;
	VK_DeferRelease( VK_GraphReleaseImage, copy );
	memset( &gi->image, 0, sizeof( gi->image ));
}

/*
=================
VK_GraphBeginFrame

before the frame records: the images follow the main target's size, and
the ones nobody asks for any more go
=================
*/
void VK_GraphBeginFrame( void )
{
	const double now = gEngfuncs.pfnTime();
	int width, height;

	VK_MainExtent( &width, &height );

	if( width != vk_graph.width || height != vk_graph.height )
	{
		for( int i = 0; i < VK_GRAPH_MAX_IMAGES; i++ )
			VK_GraphFreeImage( &vk_graph.images[i] );

		vk_graph.width = width;
		vk_graph.height = height;
		vk_graph.generation++;
	}

	for( int i = 0; i < VK_GRAPH_MAX_IMAGES; i++ )
	{
		vk_graph_image_t *gi = &vk_graph.images[i];

		if( gi->image.image && now > gi->last_asked + VK_GRAPH_IDLE_TIME )
		{
			VK_GraphFreeImage( gi );
			vk_graph.generation++;
		}
	}
}

/*
=================
VK_GraphImage

the transient image called name, the main target's size divided by
2^shift; made when missing. NULL when it can't be made.
=================
*/
vk_image_t *VK_GraphImage( const char *name, VkFormat format, VkImageUsageFlags usage, int shift )
{
	vk_graph_image_t *gi = NULL, *free_slot = NULL;
	const int div = 1 << shift;

	for( int i = 0; i < VK_GRAPH_MAX_IMAGES && !gi; i++ )
	{
		vk_graph_image_t *it = &vk_graph.images[i];

		if( !Q_strcmp( it->name, name ) && it->format == format && it->usage == usage && it->shift == shift )
			gi = it;
		else if( !it->image.image && !free_slot )
			free_slot = it;
	}

	if( !gi )
	{
		if( !( gi = free_slot ))
			return NULL;

		Q_strncpy( gi->name, name, sizeof( gi->name ));
		gi->format = format;
		gi->usage = usage;
		gi->shift = shift;
	}

	gi->last_asked = gEngfuncs.pfnTime();

	if( !gi->image.image )
	{
		const uint32_t w = Q_max(( vk_graph.width + div - 1 ) / div, 1 );
		const uint32_t h = Q_max(( vk_graph.height + div - 1 ) / div, 1 );

		if( !VK_CreateImage( &gi->image, gi->name, w, h, format, usage, VK_IMAGE_ASPECT_COLOR_BIT ))
		{
			gi->name[0] = 0;
			return NULL;
		}

		vk_graph.generation++;
	}

	return &gi->image;
}

// changes when any transient image was made again: views to forget
int VK_GraphGeneration( void )
{
	return vk_graph.generation;
}

void VK_ShutdownGraph( void )
{
	if( vk.device )
	{
		vkDeviceWaitIdle( vk.device );
		for( int i = 0; i < VK_GRAPH_MAX_IMAGES; i++ )
			VK_DestroyImage( &vk_graph.images[i].image );
	}

	memset( &vk_graph, 0, sizeof( vk_graph ));
}
