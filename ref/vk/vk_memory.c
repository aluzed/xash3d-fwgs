/*
vk_memory.c - device memory, dynamic ring, uploads and deferred release
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
 * Decision (story 007): a small block allocator instead of Vulkan Memory
 * Allocator. VMA is C++ and would need a wrapper object; what the renderer
 * needs is simple: 64 MB blocks per memory type, sub-allocated first-fit,
 * with buffers and images in separate blocks so bufferImageGranularity
 * never matters. Host visible blocks stay mapped for their whole life.
 */

#define VK_BLOCK_SIZE        ( 64u << 20 )
#define VK_MAX_BLOCKS        256
#define VK_MAX_FREE_RANGES   2048
#define VK_STAGING_SIZE      ( 32u << 20 )
#define VK_MAX_DEFERRED      4096

typedef struct vk_range_s
{
	VkDeviceSize offset;
	VkDeviceSize size;
} vk_range_t;

typedef struct vk_block_s
{
	VkDeviceMemory memory;
	VkDeviceSize size;
	VkDeviceSize used;
	uint32_t type;
	qboolean linear;     // buffers, as opposed to optimal tiling images
	qboolean dedicated;  // a single allocation larger than a block
	byte *mapped;
	int num_free;
	vk_range_t *free;    // sorted by offset
} vk_block_t;

static vk_block_t vk_blocks[VK_MAX_BLOCKS];

/*
=================
Block allocator
=================
*/
static vk_block_t *VK_NewBlock( uint32_t type, VkDeviceSize size, qboolean linear, qboolean dedicated )
{
	vk_block_t *block = NULL;

	for( int i = 0; i < VK_MAX_BLOCKS; i++ )
	{
		if( !vk_blocks[i].memory )
		{
			block = &vk_blocks[i];
			break;
		}
	}

	if( !block )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: out of memory blocks\n" );
		return NULL;
	}

	VkMemoryAllocateFlagsInfo flags = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, // buffer device address for later passes
	};

	VkMemoryAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = linear ? &flags : NULL,
		.allocationSize = size,
		.memoryTypeIndex = type,
	};

	if( vkAllocateMemory( vk.device, &info, NULL, &block->memory ) != VK_SUCCESS )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: vkAllocateMemory of %u MB failed\n", (uint)( size >> 20 ));
		block->memory = VK_NULL_HANDLE;
		return NULL;
	}

	block->size = size;
	block->used = 0;
	block->type = type;
	block->linear = linear;
	block->dedicated = dedicated;
	block->free = Mem_Malloc( r_temppool, sizeof( vk_range_t ) * ( dedicated ? 1 : VK_MAX_FREE_RANGES ));
	block->num_free = 1;
	block->free[0].offset = 0;
	block->free[0].size = size;

	if( FBitSet( vk.memory.memoryTypes[type].propertyFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ))
		vkMapMemory( vk.device, block->memory, 0, VK_WHOLE_SIZE, 0, (void **)&block->mapped );

	VK_SetObjectName( VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)block->memory, "block %d, type %u%s",
		(int)( block - vk_blocks ), type, linear ? ", buffers" : ", images" );

	return block;
}

static void VK_FreeBlock( vk_block_t *block )
{
	if( block->mapped )
		vkUnmapMemory( vk.device, block->memory );

	vkFreeMemory( vk.device, block->memory, NULL );
	Mem_Free( block->free );
	memset( block, 0, sizeof( *block ));
}

static qboolean VK_BlockAlloc( vk_block_t *block, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *offset )
{
	for( int i = 0; i < block->num_free; i++ )
	{
		vk_range_t *r = &block->free[i];
		VkDeviceSize start = ( r->offset + align - 1 ) & ~( align - 1 );
		VkDeviceSize end = start + size;

		if( end > r->offset + r->size )
			continue;

		VkDeviceSize tail = r->offset + r->size - end;
		VkDeviceSize head = start - r->offset;

		if( head && tail )
		{
			// split in two, needs a new range slot
			if( block->num_free >= VK_MAX_FREE_RANGES )
				continue;

			memmove( &block->free[i + 2], &block->free[i + 1], sizeof( vk_range_t ) * ( block->num_free - i - 1 ));
			block->free[i].size = head;
			block->free[i + 1].offset = end;
			block->free[i + 1].size = tail;
			block->num_free++;
		}
		else if( head )
			r->size = head;
		else if( tail )
		{
			r->offset = end;
			r->size = tail;
		}
		else
		{
			memmove( &block->free[i], &block->free[i + 1], sizeof( vk_range_t ) * ( block->num_free - i - 1 ));
			block->num_free--;
		}

		block->used += size;
		*offset = start;
		return true;
	}

	return false;
}

static void VK_BlockFree( vk_block_t *block, VkDeviceSize offset, VkDeviceSize size )
{
	int i;

	block->used -= size;

	// find the insertion point, then merge with the neighbours
	for( i = 0; i < block->num_free && block->free[i].offset < offset; i++ );

	qboolean merge_prev = i > 0 && block->free[i - 1].offset + block->free[i - 1].size == offset;
	qboolean merge_next = i < block->num_free && offset + size == block->free[i].offset;

	if( merge_prev && merge_next )
	{
		block->free[i - 1].size += size + block->free[i].size;
		memmove( &block->free[i], &block->free[i + 1], sizeof( vk_range_t ) * ( block->num_free - i - 1 ));
		block->num_free--;
	}
	else if( merge_prev )
		block->free[i - 1].size += size;
	else if( merge_next )
	{
		block->free[i].offset = offset;
		block->free[i].size += size;
	}
	else
	{
		if( block->num_free >= VK_MAX_FREE_RANGES )
		{
			// leak the range rather than corrupt the list, reported by r_vk_meminfo
			gEngfuncs.Con_Printf( S_WARN "ref_vk: memory block %d is too fragmented\n", (int)( block - vk_blocks ));
			return;
		}

		memmove( &block->free[i + 1], &block->free[i], sizeof( vk_range_t ) * ( block->num_free - i ));
		block->free[i].offset = offset;
		block->free[i].size = size;
		block->num_free++;
	}
}

/*
=================
VK_Alloc

linear is true for buffers, false for optimal tiling images
=================
*/
qboolean VK_Alloc( const VkMemoryRequirements *req, VkMemoryPropertyFlags flags, qboolean linear, vk_alloc_t *out )
{
	int type = VK_FindMemoryType( req->memoryTypeBits, flags );
	vk_block_t *block;

	memset( out, 0, sizeof( *out ));

	if( type < 0 )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: no memory type with flags 0x%x\n", flags );
		return false;
	}

	// large resources get their own allocation
	if( req->size > VK_BLOCK_SIZE / 2 )
	{
		block = VK_NewBlock( type, req->size, linear, true );
		if( !block || !VK_BlockAlloc( block, req->size, req->alignment, &out->offset ))
			return false;
	}
	else
	{
		block = NULL;

		for( int i = 0; i < VK_MAX_BLOCKS && !block; i++ )
		{
			vk_block_t *b = &vk_blocks[i];

			if( b->memory && !b->dedicated && b->type == (uint32_t)type && b->linear == linear
				&& VK_BlockAlloc( b, req->size, req->alignment, &out->offset ))
				block = b;
		}

		if( !block )
		{
			block = VK_NewBlock( type, VK_BLOCK_SIZE, linear, false );
			if( !block || !VK_BlockAlloc( block, req->size, req->alignment, &out->offset ))
				return false;
		}
	}

	out->memory = block->memory;
	out->size = req->size;
	out->block = (int)( block - vk_blocks );
	out->mapped = block->mapped ? block->mapped + out->offset : NULL;
	return true;
}

void VK_Free( vk_alloc_t *alloc )
{
	vk_block_t *block;

	if( !alloc->memory )
		return;

	block = &vk_blocks[alloc->block];
	VK_BlockFree( block, alloc->offset, alloc->size );

	// keep empty shared blocks, the next map reuses them
	if( block->dedicated )
		VK_FreeBlock( block );

	memset( alloc, 0, sizeof( *alloc ));
}

qboolean VK_CreateBuffer( vk_buffer_t *buf, const char *name, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags )
{
	VkMemoryRequirements req;

	memset( buf, 0, sizeof( *buf ));

	VkBufferCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	if( vkCreateBuffer( vk.device, &info, NULL, &buf->buffer ) != VK_SUCCESS )
		return false;

	vkGetBufferMemoryRequirements( vk.device, buf->buffer, &req );

	if( !VK_Alloc( &req, flags, true, &buf->alloc ))
	{
		vkDestroyBuffer( vk.device, buf->buffer, NULL );
		buf->buffer = VK_NULL_HANDLE;
		return false;
	}

	vkBindBufferMemory( vk.device, buf->buffer, buf->alloc.memory, buf->alloc.offset );
	buf->size = size;
	VK_SetObjectName( VK_OBJECT_TYPE_BUFFER, (uint64_t)buf->buffer, "%s", name );

	return true;
}

void VK_DestroyBuffer( vk_buffer_t *buf )
{
	if( buf->buffer )
		vkDestroyBuffer( vk.device, buf->buffer, NULL );

	VK_Free( &buf->alloc );
	memset( buf, 0, sizeof( *buf ));
}

/*
=================
Deferred release: objects the frames in flight may still use
=================
*/
typedef struct vk_deferred_s
{
	void (*release)( void *data );
	void *data;
	uint64_t frame; // released once this frame has completed
} vk_deferred_t;

static vk_deferred_t vk_deferred[VK_MAX_DEFERRED];
static int vk_num_deferred;

void VK_DeferRelease( void (*release)( void *data ), void *data )
{
	if( vk_num_deferred >= VK_MAX_DEFERRED )
	{
		// rare: drain everything rather than leak
		vkDeviceWaitIdle( vk.device );
		VK_RunDeferred( UINT64_MAX );
	}

	vk_deferred[vk_num_deferred].release = release;
	vk_deferred[vk_num_deferred].data = data;
	vk_deferred[vk_num_deferred].frame = VK_CurrentFrame();
	vk_num_deferred++;
}

/*
=================
VK_RunDeferred

releases what frames up to and including completed no longer use
=================
*/
void VK_RunDeferred( uint64_t completed )
{
	int kept = 0;

	for( int i = 0; i < vk_num_deferred; i++ )
	{
		if( vk_deferred[i].frame <= completed )
			vk_deferred[i].release( vk_deferred[i].data );
		else
			vk_deferred[kept++] = vk_deferred[i];
	}

	vk_num_deferred = kept;
}

/*
=================
Uploads

copies are recorded in a dedicated command buffer and submitted before the
frame that needs them, or right away when the staging buffer is full
=================
*/
static struct
{
	vk_buffer_t staging;
	VkDeviceSize used;
	VkCommandPool pool;
	VkCommandBuffer cmd;
	VkFence fence;
	qboolean recording;
	vk_buffer_t *oversized[64]; // one-shot staging for uploads larger than the ring
	int num_oversized;
} vk_upload;

static void VK_BeginUploadCommands( void )
{
	if( vk_upload.recording )
		return;

	VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkResetCommandPool( vk.device, vk_upload.pool, 0 );
	vkBeginCommandBuffer( vk_upload.cmd, &begin );
	VK_BeginLabel( vk_upload.cmd, "uploads" );
	vk_upload.recording = true;
}

/*
=================
VK_FlushUploads

submits the pending copies and waits for them
=================
*/
void VK_FlushUploads( void )
{
	if( !vk_upload.recording )
		return;

	VK_EndLabel( vk_upload.cmd );
	vkEndCommandBuffer( vk_upload.cmd );

	VkCommandBufferSubmitInfo cmd = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = vk_upload.cmd,
	};

	VkSubmitInfo2 submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmd,
	};

	vkResetFences( vk.device, 1, &vk_upload.fence );
	vkQueueSubmit2( vk.graphics_queue, 1, &submit, vk_upload.fence );
	vkWaitForFences( vk.device, 1, &vk_upload.fence, VK_TRUE, UINT64_MAX );

	vk_upload.recording = false;
	vk_upload.used = 0;

	for( int i = 0; i < vk_upload.num_oversized; i++ )
	{
		VK_DestroyBuffer( vk_upload.oversized[i] );
		Mem_Free( vk_upload.oversized[i] );
	}
	vk_upload.num_oversized = 0;
}

/*
=================
VK_UploadStaging

returns a CPU pointer to size bytes of staging memory and the command
buffer that must record the copies reading it
=================
*/
void *VK_UploadStaging( VkDeviceSize size, VkBuffer *buffer, VkDeviceSize *offset, VkCommandBuffer *cmd )
{
	const VkDeviceSize align = 16;
	VkDeviceSize start = ( vk_upload.used + align - 1 ) & ~( align - 1 );

	if( size > VK_STAGING_SIZE )
	{
		vk_buffer_t *big;

		if( vk_upload.num_oversized >= ARRAYSIZE( vk_upload.oversized ))
			VK_FlushUploads();

		big = Mem_Calloc( r_temppool, sizeof( *big ));
		if( !VK_CreateBuffer( big, "oversized staging", size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ))
		{
			Mem_Free( big );
			return NULL;
		}

		vk_upload.oversized[vk_upload.num_oversized++] = big;
		VK_BeginUploadCommands();
		*buffer = big->buffer;
		*offset = 0;
		*cmd = vk_upload.cmd;
		return big->alloc.mapped;
	}

	if( start + size > VK_STAGING_SIZE )
	{
		VK_FlushUploads();
		start = 0;
	}

	VK_BeginUploadCommands();
	vk_upload.used = start + size;

	*buffer = vk_upload.staging.buffer;
	*offset = start;
	*cmd = vk_upload.cmd;
	return vk_upload.staging.alloc.mapped + start;
}

/*
=================
Per-frame dynamic ring, for vertices rebuilt every frame
=================
*/
#define VK_MAX_DYNAMIC_CHUNKS 8

typedef struct vk_dynamic_s
{
	vk_buffer_t chunks[VK_MAX_DYNAMIC_CHUNKS]; // more than one only after an overflow
	int num_chunks;
	VkDeviceSize used; // in the last chunk
} vk_dynamic_t;

static vk_dynamic_t vk_dynamic[VK_FRAMES_IN_FLIGHT];
static int vk_dynamic_frame;

static qboolean VK_AddDynamicChunk( vk_dynamic_t *dyn, VkDeviceSize size )
{
	if( dyn->num_chunks >= VK_MAX_DYNAMIC_CHUNKS )
		return false;

	if( !VK_CreateBuffer( &dyn->chunks[dyn->num_chunks], "dynamic ring", size,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
		| VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ))
		return false;

	dyn->num_chunks++;
	dyn->used = 0;
	return true;
}

/*
=================
VK_BeginDynamic

called when a frame slot is reused: its previous contents are no longer read
=================
*/
void VK_BeginDynamic( int frame )
{
	vk_dynamic_t *dyn = &vk_dynamic[frame];
	VkDeviceSize wanted = (VkDeviceSize)Q_max( 1, (int)r_vk_dynamic_mb.value ) << 20;
	VkDeviceSize total = 0;

	vk_dynamic_frame = frame;

	for( int i = 0; i < dyn->num_chunks; i++ )
		total += dyn->chunks[i].size;

	// overflowed last time: replace the chain by a single buffer large enough
	if( dyn->num_chunks > 1 || ( dyn->num_chunks == 1 && dyn->chunks[0].size < wanted ))
	{
		wanted = Q_max( wanted, total );

		for( int i = 0; i < dyn->num_chunks; i++ )
			VK_DestroyBuffer( &dyn->chunks[i] );
		dyn->num_chunks = 0;
	}

	if( !dyn->num_chunks )
		VK_AddDynamicChunk( dyn, wanted );

	dyn->used = 0;
}

/*
=================
VK_DynamicAlloc

returns NULL when even a new chunk can't be created
=================
*/
void *VK_DynamicAlloc( VkDeviceSize size, VkDeviceSize align, VkBuffer *buffer, VkDeviceSize *offset )
{
	vk_dynamic_t *dyn = &vk_dynamic[vk_dynamic_frame];
	vk_buffer_t *chunk;
	VkDeviceSize start;

	if( !dyn->num_chunks )
		return NULL;

	chunk = &dyn->chunks[dyn->num_chunks - 1];
	start = ( dyn->used + align - 1 ) & ~( align - 1 );

	if( start + size > chunk->size )
	{
		static qboolean warned;

		if( !warned )
		{
			gEngfuncs.Con_Printf( S_WARN "ref_vk: dynamic ring overflow, growing (raise r_vk_dynamic_mb)\n" );
			warned = true;
		}

		if( !VK_AddDynamicChunk( dyn, Q_max( chunk->size * 2, size )))
			return NULL;

		chunk = &dyn->chunks[dyn->num_chunks - 1];
		start = 0;
	}

	dyn->used = start + size;
	*buffer = chunk->buffer;
	*offset = start;
	return chunk->alloc.mapped + start;
}

/*
=================
r_vk_meminfo
=================
*/
static void VK_MemInfo_f( void )
{
	VkDeviceSize total[VK_MAX_MEMORY_TYPES] = { 0 }, used[VK_MAX_MEMORY_TYPES] = { 0 };
	int blocks = 0;

	for( int i = 0; i < VK_MAX_BLOCKS; i++ )
	{
		if( !vk_blocks[i].memory )
			continue;

		total[vk_blocks[i].type] += vk_blocks[i].size;
		used[vk_blocks[i].type] += vk_blocks[i].used;
		blocks++;
	}

	gEngfuncs.Con_Printf( "ref_vk memory: %d blocks\n", blocks );

	for( uint32_t i = 0; i < vk.memory.memoryTypeCount; i++ )
	{
		if( !total[i] )
			continue;

		gEngfuncs.Con_Printf( "  type %u (heap %u, flags 0x%x): %.1f / %.1f MB used\n", i,
			vk.memory.memoryTypes[i].heapIndex, vk.memory.memoryTypes[i].propertyFlags,
			used[i] / 1048576.0, total[i] / 1048576.0 );
	}

	if( vk.caps.memory_budget && vkGetPhysicalDeviceMemoryProperties2 )
	{
		VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
		};
		VkPhysicalDeviceMemoryProperties2 props = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
			.pNext = &budget,
		};

		vkGetPhysicalDeviceMemoryProperties2( vk.physical_device, &props );

		for( uint32_t i = 0; i < props.memoryProperties.memoryHeapCount; i++ )
		{
			gEngfuncs.Con_Printf( "  heap %u%s: %.1f MB used by the process, budget %.1f MB\n", i,
				FBitSet( props.memoryProperties.memoryHeaps[i].flags, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) ? " (device)" : "",
				budget.heapUsage[i] / 1048576.0, budget.heapBudget[i] / 1048576.0 );
		}
	}
}

qboolean VK_InitMemory( void )
{
	memset( vk_blocks, 0, sizeof( vk_blocks ));
	memset( &vk_upload, 0, sizeof( vk_upload ));
	memset( vk_dynamic, 0, sizeof( vk_dynamic ));
	vk_num_deferred = 0;

	if( !VK_CreateBuffer( &vk_upload.staging, "staging ring", VK_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ))
		return false;

	VkCommandPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = vk.graphics_family,
	};

	VkFenceCreateInfo fence = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

	if( vkCreateCommandPool( vk.device, &pool, NULL, &vk_upload.pool ) != VK_SUCCESS
		|| vkCreateFence( vk.device, &fence, NULL, &vk_upload.fence ) != VK_SUCCESS )
		return false;

	VkCommandBufferAllocateInfo alloc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk_upload.pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	vkAllocateCommandBuffers( vk.device, &alloc, &vk_upload.cmd );
	VK_SetObjectName( VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)vk_upload.pool, "upload pool" );
	VK_SetObjectName( VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)vk_upload.cmd, "upload commands" );
	VK_SetObjectName( VK_OBJECT_TYPE_FENCE, (uint64_t)vk_upload.fence, "upload fence" );

	gEngfuncs.Cmd_AddCommand( "r_vk_meminfo", VK_MemInfo_f, "print Vulkan memory usage" );

	return true;
}

void VK_ShutdownMemory( void )
{
	if( !vk.device )
		return;

	gEngfuncs.Cmd_RemoveCommand( "r_vk_meminfo" );

	VK_FlushUploads();
	vkDeviceWaitIdle( vk.device );
	VK_RunDeferred( UINT64_MAX );

	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
	{
		for( int j = 0; j < vk_dynamic[i].num_chunks; j++ )
			VK_DestroyBuffer( &vk_dynamic[i].chunks[j] );
	}

	VK_DestroyBuffer( &vk_upload.staging );

	if( vk_upload.pool )
		vkDestroyCommandPool( vk.device, vk_upload.pool, NULL );
	if( vk_upload.fence )
		vkDestroyFence( vk.device, vk_upload.fence, NULL );

	for( int i = 0; i < VK_MAX_BLOCKS; i++ )
	{
		if( vk_blocks[i].memory )
			VK_FreeBlock( &vk_blocks[i] );
	}

	memset( &vk_upload, 0, sizeof( vk_upload ));
	memset( vk_dynamic, 0, sizeof( vk_dynamic ));
}
