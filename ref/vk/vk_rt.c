/*
vk_rt.c - acceleration structures of the scene, for ray queries
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
With r_vk_rt 1, on GPUs with VK_KHR_acceleration_structure and
VK_KHR_ray_query, the scene of every frame is in a TLAS the shaders can
trace rays through (rt_common.glsl):

  world      a BLAS of the world model's surfaces, built at the first frame
             with r_vk_rt 1 after a map load (from copies kept then): one
             geometry for the opaque ones, one for the alpha tested ('{'
             textures, the ray query looks at their alpha); water apart,
             in its own BLAS (mask RT_MASK_WATER)
  brush      a BLAS per brush model ("*1", "*2"...) built with the world's,
             instanced each frame for the brush entities drawn, with their
             transform (mask RT_MASK_TRANS when not kRenderNormal)
  studio     the triangles studio models are drawn with, in world space,
             captured while they're drawn (vk_imm.c): a BLAS per entity,
             updated (refit) every frame while its triangle count stays,
             rebuilt when it changes; the view model has RT_MASK_VIEWMODEL.
             A model out of the view isn't drawn, so isn't in the scene:
             what the rays see is what the player could see (025)
  excluded   sprites, particles, beams and decals: flat, often additive,
             they'd only cost; RT effects treat them as GoldSrc does, drawn
             over the result

The geometry table, the per triangle surface numbers and the vertices are
read by buffer device addresses (rt_scene_r). r_vk_rt_debug draws the
scene traced from the camera, to check it against the raster view.

The world vertex buffer is the one of vk_world.c; its indices are copied
in BLAS order into an index buffer of their own.
*/

#include "vk_local.h"

static const uint32_t rt_debug_spv[] =
#include "shaders/rt_debug.comp.inc"
;

CVAR_DEFINE_AUTO( r_vk_rt, "0", FCVAR_GLCONFIG, "build acceleration structures of the scene every frame (ray tracing GPUs)" );
CVAR_DEFINE_AUTO( r_vk_rt_debug, "0", FCVAR_CHEAT, "show the scene traced from the camera: 1 albedo, 2 normals, 3 instances, 4 depth against the raster view, 5 albedo with the view model" );

#define RT_MASK_WORLD     0x01
#define RT_MASK_MODELS    0x02
#define RT_MASK_VIEWMODEL 0x04
#define RT_MASK_WATER     0x08
#define RT_MASK_TRANS     0x10

#define RT_KIND_BRUSH     0
#define RT_KIND_STUDIO    1

#define RT_SURF_ALPHA     1
#define RT_SURF_WATER     2

#define VK_RT_MAX_INSTANCES 1024
#define VK_RT_MAX_SEGMENTS  256  // studio entities drawn in a frame
#define VK_RT_IDLE_FRAMES   60   // frames before an unused entity BLAS goes

// mirrors rt_common.glsl
typedef struct
{
	uint32_t index_offset;
	uint32_t prim_offset;
	uint32_t kind;
	uint32_t pad;
} vk_rt_geometry_t;

typedef struct
{
	int texture;
	int flags;
} vk_rt_surface_t;

typedef struct
{
	VkDeviceAddress brush_vertices;
	VkDeviceAddress brush_indices;
	VkDeviceAddress brush_prims;
	VkDeviceAddress surfaces;
	VkDeviceAddress geometries;
	VkDeviceAddress studio_vertices;
	VkDeviceAddress studio_tris;
} vk_rt_scene_t;

typedef struct
{
	VkAccelerationStructureKHR handle;
	vk_buffer_t buffer;
	VkDeviceAddress address;
} vk_as_t;

// a BLAS of brush surfaces: its geometries are entries of the table
typedef struct
{
	vk_as_t as;
	int geometry_base; // first entry in the geometry table
	int num_geometries;
} vk_rt_brush_t;

// the triangles of a studio entity this frame
typedef struct
{
	int key;               // entity index, -1 for the view model
	const model_t *model;
	int first_tri, num_tris;
	qboolean viewmodel;
} vk_rt_segment_t;

// a studio entity's BLAS, in one frame slot: updated in place next time
typedef struct
{
	int key;
	const model_t *model;
	uint32_t num_tris;     // an update keeps the geometry: the same count
	vk_as_t as;
	uint64_t last_frame;
} vk_rt_dynamic_t;

typedef struct
{
	vk_rt_dynamic_t dynamic[VK_RT_MAX_SEGMENTS];
	vk_as_t tlas;
	vk_buffer_t scratch;
	VkDescriptorSet debug_set;
} vk_rt_frame_t;

// mirrors rt_debug.comp
typedef struct
{
	float inv_view_proj[16];
	int rect[4];
	float eye[4];
	float forward[4];
	VkDeviceAddress scene;
	int mode;
	int pad;
} vk_rt_debug_push_t;

static struct
{
	qboolean available; // the device can
	qboolean active;    // r_vk_rt, and the world is built
	VkPhysicalDeviceAccelerationStructurePropertiesKHR props;

	// map: world, water, brush models
	vk_rt_brush_t world, water;
	vk_rt_brush_t submodels[MAX_MODELS];
	vk_buffer_t indices, prims, surfaces;
	vk_rt_geometry_t *geometries; // CPU copy of the brush entries, studio ones follow each frame
	int num_geometries;
	VkDeviceAddress vertices;
	size_t static_bytes;

	// the map's geometry, kept to build its acceleration structures when
	// r_vk_rt asks for them: no cost to maps loaded without it
	uint32_t *map_indices, *map_first, *map_count;
	VkBuffer map_vertices;
	uint32_t map_num_verts;
	qboolean map_pending;

	// studio triangles captured this frame, entity by entity
	float *studio_verts;       // 5 floats a vertex, 3 vertices a triangle
	vk_rt_surface_t *studio_tris;
	int num_studio, max_studio;
	vk_rt_segment_t segments[VK_RT_MAX_SEGMENTS];
	int num_segments;
	qboolean capturing;

	// r_vk_rtinfo
	int stat_builds, stat_updates, stat_instances;

	vk_rt_frame_t frames[VK_FRAMES_IN_FLIGHT];
	VkDeviceAddress scene;     // this frame's rt_scene_r, 0 when not built
	uint64_t built_frame;

	// debug view
	VkDescriptorSetLayout debug_layout;
	VkDescriptorPool debug_pool;
	VkPipelineLayout debug_pipeline_layout;
	VkPipeline debug_pipeline;
	VkSampler nearest;
} vk_rt;

void VK_RegisterRTCvars( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_rt_debug );
}

VkDeviceAddress VK_BufferAddress( VkBuffer buffer )
{
	VkBufferDeviceAddressInfo info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = buffer,
	};

	return vkGetBufferDeviceAddress( vk.device, &info );
}

qboolean VK_RTAvailable( void )
{
	return vk_rt.available;
}

/*
=================
Acceleration structures
=================
*/
static void VK_DestroyAS( vk_as_t *as )
{
	if( as->handle )
		vkDestroyAccelerationStructureKHR( vk.device, as->handle, NULL );
	VK_DestroyBuffer( &as->buffer );
	memset( as, 0, sizeof( *as ));
}

static qboolean VK_CreateAS( vk_as_t *as, const char *name, VkAccelerationStructureTypeKHR type, VkDeviceSize size )
{
	if( !VK_CreateBuffer( &as->buffer, name, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ))
		return false;

	VkAccelerationStructureCreateInfoKHR info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = as->buffer.buffer,
		.size = size,
		.type = type,
	};

	if( vkCreateAccelerationStructureKHR( vk.device, &info, NULL, &as->handle ) != VK_SUCCESS )
	{
		VK_DestroyBuffer( &as->buffer );
		return false;
	}

	VkAccelerationStructureDeviceAddressInfoKHR addr = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = as->handle,
	};

	as->address = vkGetAccelerationStructureDeviceAddressKHR( vk.device, &addr );
	VK_SetObjectName( VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (uint64_t)as->handle, "%s", name );
	return true;
}

static VkDeviceSize VK_AlignUp( VkDeviceSize v, VkDeviceSize a )
{
	return ( v + a - 1 ) & ~( a - 1 );
}

static void VK_DeferDestroyBuffer( vk_buffer_t *buf );

// a scratch buffer at least size bytes, kept for the next builds
static qboolean VK_RTScratch( vk_buffer_t *scratch, VkDeviceSize size, const char *name )
{
	size = VK_AlignUp( size, vk_rt.props.minAccelerationStructureScratchOffsetAlignment ) + vk_rt.props.minAccelerationStructureScratchOffsetAlignment;

	if( scratch->buffer && scratch->size >= size )
		return true;

	// builds recorded earlier may still use it
	VK_DeferDestroyBuffer( scratch );
	return VK_CreateBuffer( scratch, name, Q_max( size, 1u << 20 ) * 3 / 2,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
}

static VkDeviceAddress VK_ScratchAddress( const vk_buffer_t *scratch )
{
	return VK_AlignUp( VK_BufferAddress( scratch->buffer ), vk_rt.props.minAccelerationStructureScratchOffsetAlignment );
}

static void VK_ReleaseBuffer( void *data )
{
	VK_DestroyBuffer( data );
	Mem_Free( data );
}

// once the frames recorded so far are done with it
static void VK_DeferDestroyBuffer( vk_buffer_t *buf )
{
	vk_buffer_t *copy;

	if( !buf->buffer )
		return;

	copy = Mem_Malloc( r_temppool, sizeof( *copy ));
	*copy = *buf;
	VK_DeferRelease( VK_ReleaseBuffer, copy );
	memset( buf, 0, sizeof( *buf ));
}

static void VK_RTBarrier( VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst )
{
	VkMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = src_stage,
		.srcAccessMask = src,
		.dstStageMask = dst_stage,
		.dstAccessMask = dst,
	};

	VkDependencyInfo dep = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2( cmd, &dep );
}

// between two builds sharing a scratch buffer, or a BLAS the TLAS reads
static void VK_RTBuildBarrier( VkCommandBuffer cmd )
{
	VK_RTBarrier( cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR );
}

/*
=================
VK_RTBuildAS

builds geometries into as, made (again) when too small; the scratch
buffer grows to what the build needs. Records into cmd.
=================
*/
static qboolean VK_RTBuildAS( VkCommandBuffer cmd, vk_as_t *as, const char *name, VkAccelerationStructureTypeKHR type,
	const VkAccelerationStructureGeometryKHR *geometries, const VkAccelerationStructureBuildRangeInfoKHR *ranges, int count,
	VkBuildAccelerationStructureFlagsKHR flags, vk_buffer_t *scratch )
{
	uint32_t prims[64];
	VkAccelerationStructureBuildGeometryInfoKHR build = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = type,
		.flags = flags,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = count,
		.pGeometries = geometries,
	};
	VkAccelerationStructureBuildSizesInfoKHR sizes = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };

	if( count > (int)ARRAYSIZE( prims ))
		return false;

	for( int i = 0; i < count; i++ )
		prims[i] = ranges[i].primitiveCount;

	vkGetAccelerationStructureBuildSizesKHR( vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, prims, &sizes );

	if( !as->handle || as->buffer.size < sizes.accelerationStructureSize )
	{
		// remade every frame: room to grow, not remade each time
		const VkDeviceSize size = FBitSet( flags, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR )
			? sizes.accelerationStructureSize * 3 / 2 : sizes.accelerationStructureSize;

		VK_DestroyAS( as ); // static ones are built once, dynamic ones in their own frame slot
		if( !VK_CreateAS( as, name, type, size ))
			return false;
	}

	if( !VK_RTScratch( scratch, sizes.buildScratchSize, "rt scratch" ))
		return false;

	build.dstAccelerationStructure = as->handle;
	build.scratchData.deviceAddress = VK_ScratchAddress( scratch );

	const VkAccelerationStructureBuildRangeInfoKHR *range_ptr = ranges;
	vkCmdBuildAccelerationStructuresKHR( cmd, 1, &build, &range_ptr );
	return true;
}

/*
=================
World and brush models, at map load
=================
*/
typedef struct
{
	uint32_t *indices;
	uint32_t *prims;
	uint32_t num_indices, num_prims, max_indices;
} vk_rt_lists_t;

static void VK_RTAddSurface( vk_rt_lists_t *l, const uint32_t *indices, uint32_t first, uint32_t count, uint32_t surfnum )
{
	if( l->num_indices + count > l->max_indices )
	{
		l->max_indices = Q_max( l->max_indices * 2, l->num_indices + count + 65536 );
		l->indices = Mem_Realloc( r_temppool, l->indices, l->max_indices * sizeof( uint32_t ));
		l->prims = Mem_Realloc( r_temppool, l->prims, l->max_indices / 3 * sizeof( uint32_t ) + 4 );
	}

	memcpy( l->indices + l->num_indices, indices + first, count * sizeof( uint32_t ));
	l->num_indices += count;

	for( uint32_t i = 0; i < count / 3; i++ )
		l->prims[l->num_prims++] = surfnum;
}

typedef enum { VK_RT_GROUP_OPAQUE, VK_RT_GROUP_ALPHA, VK_RT_GROUP_WATER, VK_RT_GROUPS } vk_rt_group_t;

static vk_rt_group_t VK_RTSurfaceGroup( const msurface_t *surf )
{
	if( FBitSet( surf->flags, SURF_DRAWTURB ))
		return VK_RT_GROUP_WATER;
	if( surf->texinfo && surf->texinfo->texture && surf->texinfo->texture->name[0] == '{' )
		return VK_RT_GROUP_ALPHA;
	return VK_RT_GROUP_OPAQUE;
}

// the geometries of a range of surfaces: their triangles into the lists,
// a table entry and a build range each
static int VK_RTCollect( vk_rt_lists_t *l, const uint32_t *indices, const uint32_t *first_index, const uint32_t *num_indices,
	int first_surface, int num_surfaces, const vk_rt_group_t *groups, int num_groups,
	VkAccelerationStructureBuildRangeInfoKHR *ranges, qboolean *opaque )
{
	const model_t *mod = WORLDMODEL;
	int count = 0;

	for( int g = 0; g < num_groups; g++ )
	{
		const uint32_t start_index = l->num_indices, start_prim = l->num_prims;

		for( int i = first_surface; i < first_surface + num_surfaces && i < mod->numsurfaces; i++ )
		{
			const msurface_t *surf = &mod->surfaces[i];
			vk_rt_group_t group = VK_RTSurfaceGroup( surf );

			// sky isn't hit: rays through it reach the sky
			if( FBitSet( surf->flags, SURF_DRAWSKY ) || !num_indices[i] )
				continue;

			// a brush model keeps its water with its other surfaces
			if( group == VK_RT_GROUP_WATER && groups[g] != VK_RT_GROUP_WATER && num_groups < 3 )
				group = VK_RT_GROUP_OPAQUE;

			if( group == groups[g] )
				VK_RTAddSurface( l, indices, first_index[i], num_indices[i], i );
		}

		if( l->num_prims == start_prim )
			continue; // no empty geometry: the shader counts them

		vk_rt.geometries = Mem_Realloc( r_temppool, vk_rt.geometries, ( vk_rt.num_geometries + 1 ) * sizeof( vk_rt_geometry_t ));
		vk_rt.geometries[vk_rt.num_geometries++] = (vk_rt_geometry_t){ start_index, start_prim, RT_KIND_BRUSH, 0 };
		ranges[count] = (VkAccelerationStructureBuildRangeInfoKHR){ l->num_prims - start_prim, start_index * sizeof( uint32_t ), 0, 0 };
		opaque[count] = groups[g] != VK_RT_GROUP_ALPHA;
		count++;
	}

	return count;
}

static qboolean VK_RTBuildBrush( VkCommandBuffer cmd, vk_rt_brush_t *b, const char *name, const VkAccelerationStructureBuildRangeInfoKHR *ranges,
	const qboolean *opaque, int count, uint32_t max_vertex, vk_buffer_t *scratch )
{
	VkAccelerationStructureGeometryKHR geometries[VK_RT_GROUPS];

	for( int i = 0; i < count; i++ )
	{
		geometries[i] = (VkAccelerationStructureGeometryKHR){
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
			.geometry.triangles = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
				.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
				.vertexData.deviceAddress = vk_rt.vertices,
				.vertexStride = VERTEXSIZE * sizeof( float ),
				.maxVertex = max_vertex,
				.indexType = VK_INDEX_TYPE_UINT32,
				.indexData.deviceAddress = VK_BufferAddress( vk_rt.indices.buffer ),
			},
			// alpha tested triangles go through the ray query loop
			.flags = opaque[i] ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR,
		};
	}

	if( !VK_RTBuildAS( cmd, &b->as, name, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometries, ranges, count,
		VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, scratch ))
		return false;

	// the next build reuses the scratch buffer
	VK_RTBuildBarrier( cmd );
	vk_rt.static_bytes += b->as.buffer.size;
	return true;
}

static qboolean VK_RTUpload( vk_buffer_t *buf, const char *name, const void *data, VkDeviceSize size )
{
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;
	void *dst;

	size = Q_max( size, 16 );
	if( !VK_CreateBuffer( buf, name, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ))
		return false;

	if( !( dst = VK_UploadStaging( size, &staging, &offset, &cmd )))
		return false;

	memset( dst, 0, size );
	if( data )
		memcpy( dst, data, size );
	VK_UploadBufferRegion( cmd, staging, offset, buf->buffer, 0, size );
	vk_rt.static_bytes += size;
	return true;
}

static void VK_RTFreeMap( void )
{
	VK_DestroyAS( &vk_rt.world.as );
	VK_DestroyAS( &vk_rt.water.as );
	for( int i = 0; i < MAX_MODELS; i++ )
		VK_DestroyAS( &vk_rt.submodels[i].as );
	VK_DestroyBuffer( &vk_rt.indices );
	VK_DestroyBuffer( &vk_rt.prims );
	VK_DestroyBuffer( &vk_rt.surfaces );

	if( vk_rt.geometries )
		Mem_Free( vk_rt.geometries );
	vk_rt.geometries = NULL;
	vk_rt.num_geometries = 0;
	vk_rt.static_bytes = 0;
	memset( &vk_rt.world, 0, sizeof( vk_rt.world ));
	memset( &vk_rt.water, 0, sizeof( vk_rt.water ));
	memset( vk_rt.submodels, 0, sizeof( vk_rt.submodels ));
}

static void VK_RTFreeMapData( void )
{
	if( vk_rt.map_indices )
		Mem_Free( vk_rt.map_indices );
	if( vk_rt.map_first )
		Mem_Free( vk_rt.map_first );
	if( vk_rt.map_count )
		Mem_Free( vk_rt.map_count );
	vk_rt.map_indices = vk_rt.map_first = vk_rt.map_count = NULL;
	vk_rt.map_pending = false;
}

/*
=================
VK_RTNewMap

the geometry of the new map: indices are the world geometry's (vk_world.c),
first_index / num_indices per surface, vertices its buffer. Kept until
r_vk_rt asks for the acceleration structures.
=================
*/
void VK_RTNewMap( const uint32_t *indices, uint32_t total_indices, const uint32_t *first_index, const uint32_t *num_indices,
	VkBuffer vertices, uint32_t num_verts )
{
	const int numsurfaces = WORLDMODEL ? WORLDMODEL->numsurfaces : 0;

	VK_RTFreeMap();
	VK_RTFreeMapData();

	if( !vk_rt.available || !numsurfaces || !num_verts )
		return;

	vk_rt.map_indices = Mem_Malloc( r_temppool, total_indices * sizeof( uint32_t ));
	vk_rt.map_first = Mem_Malloc( r_temppool, numsurfaces * sizeof( uint32_t ));
	vk_rt.map_count = Mem_Malloc( r_temppool, numsurfaces * sizeof( uint32_t ));
	memcpy( vk_rt.map_indices, indices, total_indices * sizeof( uint32_t ));
	memcpy( vk_rt.map_first, first_index, numsurfaces * sizeof( uint32_t ));
	memcpy( vk_rt.map_count, num_indices, numsurfaces * sizeof( uint32_t ));
	vk_rt.map_vertices = vertices;
	vk_rt.map_num_verts = num_verts;
	vk_rt.map_pending = true;
}

/*
=================
VK_RTBuildMap

the acceleration structures of the map, the first time a view wants them
=================
*/
static void VK_RTBuildMap( void )
{
	const uint32_t *indices = vk_rt.map_indices, *first_index = vk_rt.map_first, *num_indices = vk_rt.map_count;
	const VkBuffer vertices = vk_rt.map_vertices;
	const uint32_t num_verts = vk_rt.map_num_verts;
	static const vk_rt_group_t world_groups[] = { VK_RT_GROUP_OPAQUE, VK_RT_GROUP_ALPHA };
	static const vk_rt_group_t water_groups[] = { VK_RT_GROUP_WATER };
	VkAccelerationStructureBuildRangeInfoKHR world_ranges[VK_RT_GROUPS], water_ranges[VK_RT_GROUPS];
	VkAccelerationStructureBuildRangeInfoKHR sub_ranges[MAX_MODELS][2];
	qboolean world_opaque[VK_RT_GROUPS], water_opaque[VK_RT_GROUPS], sub_opaque[MAX_MODELS][2];
	int sub_counts[MAX_MODELS] = { 0 };
	vk_rt_lists_t lists = { 0 };
	vk_rt_surface_t *surfaces;
	const model_t *mod = WORLDMODEL;
	vk_buffer_t scratch = { 0 };
	const double start = gEngfuncs.pfnTime();

	vk_rt.map_pending = false;

	if( !mod || !num_verts )
		return;

	vk_rt.vertices = VK_BufferAddress( vertices );


	vk_rt.world.geometry_base = vk_rt.num_geometries;
	vk_rt.world.num_geometries = VK_RTCollect( &lists, indices, first_index, num_indices, mod->firstmodelsurface, mod->nummodelsurfaces,
		world_groups, 2, world_ranges, world_opaque );
	vk_rt.water.geometry_base = vk_rt.num_geometries;
	vk_rt.water.num_geometries = VK_RTCollect( &lists, indices, first_index, num_indices, mod->firstmodelsurface, mod->nummodelsurfaces,
		water_groups, 1, water_ranges, water_opaque );

	for( int i = 0; i < gp_cl->nummodels && i < MAX_MODELS; i++ )
	{
		const model_t *m = CL_ModelHandle( i + 1 );

		if( !m || m->type != mod_brush || m->name[0] != '*' )
			continue;

		vk_rt.submodels[i].geometry_base = vk_rt.num_geometries;
		sub_counts[i] = vk_rt.submodels[i].num_geometries = VK_RTCollect( &lists, indices, first_index, num_indices,
			m->firstmodelsurface, m->nummodelsurfaces, world_groups, 2, sub_ranges[i], sub_opaque[i] );
	}

	surfaces = Mem_Calloc( r_temppool, Q_max( mod->numsurfaces, 1 ) * sizeof( *surfaces ));
	for( int i = 0; i < mod->numsurfaces; i++ )
	{
		const msurface_t *surf = &mod->surfaces[i];

		if( !surf->texinfo || !surf->texinfo->texture )
			continue;

		surfaces[i].texture = surf->texinfo->texture->gl_texturenum;
		surfaces[i].flags = ( VK_RTSurfaceGroup( surf ) == VK_RT_GROUP_ALPHA ? RT_SURF_ALPHA : 0 )
			| ( FBitSet( surf->flags, SURF_DRAWTURB ) ? RT_SURF_WATER : 0 );
	}

	if( VK_RTUpload( &vk_rt.indices, "rt indices", lists.indices, lists.num_indices * sizeof( uint32_t ))
		&& VK_RTUpload( &vk_rt.prims, "rt triangle surfaces", lists.prims, lists.num_prims * sizeof( uint32_t ))
		&& VK_RTUpload( &vk_rt.surfaces, "rt surfaces", surfaces, mod->numsurfaces * sizeof( *surfaces )))
	{
		VkBuffer staging;
		VkDeviceSize offset;
		VkCommandBuffer cmd;
		qboolean ok = true;

		VK_UploadStaging( 0, &staging, &offset, &cmd );

		// the vertex buffer was uploaded by an earlier submission, without a barrier
		VK_RTBarrier( cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
			VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_MEMORY_READ_BIT );

		if( vk_rt.world.num_geometries )
			ok = VK_RTBuildBrush( cmd, &vk_rt.world, "rt world", world_ranges, world_opaque, vk_rt.world.num_geometries, num_verts - 1, &scratch );
		if( ok && vk_rt.water.num_geometries )
			ok = VK_RTBuildBrush( cmd, &vk_rt.water, "rt water", water_ranges, water_opaque, vk_rt.water.num_geometries, num_verts - 1, &scratch );

		for( int i = 0; ok && i < MAX_MODELS; i++ )
		{
			char name[32];

			if( !sub_counts[i] )
				continue;

			Q_snprintf( name, sizeof( name ), "rt brush model %d", i + 1 );
			ok = VK_RTBuildBrush( cmd, &vk_rt.submodels[i], name, sub_ranges[i], sub_opaque[i], sub_counts[i], num_verts - 1, &scratch );
		}

		VK_RTBarrier( cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR );

		if( !ok )
		{
			gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't build the acceleration structures of the map\n" );
			VK_RTFreeMap();
		}
	}

	VK_DeferDestroyBuffer( &scratch );
	VK_FlushUploads();

	if( lists.indices )
		Mem_Free( lists.indices );
	if( lists.prims )
		Mem_Free( lists.prims );
	Mem_Free( surfaces );

	gEngfuncs.Con_Reportf( "ref_vk: acceleration structures of %u triangles, %d geometries, %.1f MB, %.0f ms\n",
		lists.num_prims, vk_rt.num_geometries, vk_rt.static_bytes / ( 1024.0 * 1024.0 ), ( gEngfuncs.pfnTime() - start ) * 1000.0 );
}

/*
=================
Studio triangles, captured while drawn
=================
*/
// an entity's model is drawn: its triangles go to its segment
void VK_RTBeginStudio( const cl_entity_t *e, const model_t *model, qboolean viewmodel )
{
	const int key = viewmodel ? -1 : e->index;
	vk_rt_segment_t *last = vk_rt.num_segments ? &vk_rt.segments[vk_rt.num_segments - 1] : NULL;

	vk_rt.capturing = false;
	if( !vk_rt.active )
		return;

	// body parts come one after the other
	if( !last || last->key != key || last->model != model )
	{
		if( vk_rt.num_segments >= VK_RT_MAX_SEGMENTS )
			return;

		last = &vk_rt.segments[vk_rt.num_segments++];
		last->key = key;
		last->model = model;
		last->first_tri = vk_rt.num_studio;
		last->num_tris = 0;
		last->viewmodel = viewmodel;
	}

	vk_rt.capturing = true;
}

void VK_RTEndStudio( void )
{
	vk_rt.capturing = false;
}

// count vertices of triangles, as vk_imm.c batches them
void VK_RTCaptureTriangles( const vk_imm_vertex_t *verts, int count, int texture, qboolean alpha_test )
{
	const int tris = count / 3;

	if( !vk_rt.capturing || tris <= 0 )
		return;

	if( vk_rt.num_studio + tris > vk_rt.max_studio )
	{
		vk_rt.max_studio = Q_max( vk_rt.max_studio * 2, vk_rt.num_studio + tris + 4096 );
		vk_rt.studio_verts = Mem_Realloc( r_temppool, vk_rt.studio_verts, vk_rt.max_studio * 15 * sizeof( float ));
		vk_rt.studio_tris = Mem_Realloc( r_temppool, vk_rt.studio_tris, vk_rt.max_studio * sizeof( vk_rt_surface_t ));
	}

	for( int t = 0; t < tris; t++ )
	{
		float *out = vk_rt.studio_verts + ( vk_rt.num_studio + t ) * 15;

		for( int k = 0; k < 3; k++ )
		{
			const vk_imm_vertex_t *v = &verts[t * 3 + k];

			VectorCopy( v->position, out + k * 5 );
			out[k * 5 + 3] = v->texcoord[0];
			out[k * 5 + 4] = v->texcoord[1];
		}

		vk_rt.studio_tris[vk_rt.num_studio + t] = (vk_rt_surface_t){ texture, alpha_test ? RT_SURF_ALPHA : 0 };
	}

	vk_rt.num_studio += tris;
	vk_rt.segments[vk_rt.num_segments - 1].num_tris += tris;
}

/*
=================
VK_RTBeginScene

a main view starts: nothing captured yet
=================
*/
void VK_RTBeginScene( void )
{
	if( vk_rt.available && r_vk_rt.value && vk_rt.map_pending )
		VK_RTBuildMap();

	vk_rt.active = vk_rt.available && r_vk_rt.value && vk_rt.world.as.handle;
	vk_rt.num_studio = vk_rt.num_segments = 0;
	vk_rt.scene = 0;
}

static void VK_RTStudioGeometry( VkAccelerationStructureGeometryKHR *g, VkDeviceAddress verts, uint32_t num_tris )
{
	*g = (VkAccelerationStructureGeometryKHR){
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry.triangles = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
			.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
			.vertexData.deviceAddress = verts,
			.vertexStride = 5 * sizeof( float ),
			.maxVertex = num_tris * 3 - 1,
			.indexType = VK_INDEX_TYPE_NONE_KHR,
		},
		// masked textures: through the ray query loop
		.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR,
	};
}

static void *VK_RTDynamic( VkDeviceSize size, VkDeviceSize align, VkDeviceAddress *address )
{
	VkBuffer buffer;
	VkDeviceSize offset;
	void *ptr = VK_DynamicAlloc( Q_max( size, 16 ), align, &buffer, &offset );

	if( ptr )
		*address = VK_BufferAddress( buffer ) + offset;
	return ptr;
}

static void VK_RTInstance( VkAccelerationStructureInstanceKHR *inst, const matrix4x4 m, uint32_t custom, uint32_t mask, VkDeviceAddress blas )
{
	memset( inst, 0, sizeof( *inst ));

	for( int r = 0; r < 3; r++ )
	{
		for( int c = 0; c < 4; c++ )
			inst->transform.matrix[r][c] = m[r][c];
	}

	inst->instanceCustomIndex = custom;
	inst->mask = mask;
	inst->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	inst->accelerationStructureReference = blas;
}

// the BLAS of a segment in this frame slot: the one it had, else a free one
static vk_rt_dynamic_t *VK_RTDynamicFor( vk_rt_frame_t *frame, const vk_rt_segment_t *seg )
{
	vk_rt_dynamic_t *same = NULL, *free_entry = NULL, *oldest = NULL;
	const uint64_t now = VK_CurrentFrame();

	for( int i = 0; i < VK_RT_MAX_SEGMENTS; i++ )
	{
		vk_rt_dynamic_t *d = &frame->dynamic[i];

		if( d->last_frame == now && d->as.handle )
			continue; // taken this frame

		if( d->as.handle && d->key == seg->key && d->model == seg->model )
			same = d;
		else if( !d->as.handle && !free_entry )
			free_entry = d;
		else if( d->as.handle && ( !oldest || d->last_frame < oldest->last_frame ))
			oldest = d;
	}

	return same ? same : free_entry ? free_entry : oldest;
}

/*
=================
VK_RTBuildStudio

the BLAS of every studio entity drawn, updated when its triangle count is
the one it was built with, built otherwise; all in one call, each with
its part of the scratch buffer
=================
*/
static int VK_RTBuildStudio( VkCommandBuffer cmd, vk_rt_frame_t *frame, VkDeviceAddress verts, VkAccelerationStructureInstanceKHR *instances )
{
	static VkAccelerationStructureGeometryKHR geometries[VK_RT_MAX_SEGMENTS];
	static VkAccelerationStructureBuildGeometryInfoKHR infos[VK_RT_MAX_SEGMENTS];
	static VkAccelerationStructureBuildRangeInfoKHR ranges[VK_RT_MAX_SEGMENTS];
	static const VkAccelerationStructureBuildRangeInfoKHR *range_ptrs[VK_RT_MAX_SEGMENTS];
	static VkDeviceSize scratch_offsets[VK_RT_MAX_SEGMENTS];
	const VkDeviceSize align = vk_rt.props.minAccelerationStructureScratchOffsetAlignment;
	const uint64_t now = VK_CurrentFrame();
	VkDeviceSize scratch_size = 0;
	int count = 0, num_instances = 0;
	matrix4x4 identity;

	Matrix4x4_LoadIdentity( identity );

	for( int i = 0; i < vk_rt.num_segments; i++ )
	{
		const vk_rt_segment_t *seg = &vk_rt.segments[i];
		vk_rt_dynamic_t *d;
		VkAccelerationStructureBuildSizesInfoKHR sizes = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		qboolean update;

		if( seg->num_tris <= 0 || !( d = VK_RTDynamicFor( frame, seg )))
			continue;

		update = d->as.handle && d->key == seg->key && d->model == seg->model && d->num_tris == (uint32_t)seg->num_tris;

		VK_RTStudioGeometry( &geometries[count], verts + (VkDeviceSize)seg->first_tri * 15 * sizeof( float ), seg->num_tris );
		infos[count] = (VkAccelerationStructureBuildGeometryInfoKHR){
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
			.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.geometryCount = 1,
			.pGeometries = &geometries[count],
		};

		const uint32_t prims = seg->num_tris;
		vkGetAccelerationStructureBuildSizesKHR( vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &infos[count], &prims, &sizes );

		if( !update )
		{
			if( !d->as.handle || d->as.buffer.size < sizes.accelerationStructureSize )
			{
				VK_DestroyAS( &d->as ); // this slot's frame is done with it
				if( !VK_CreateAS( &d->as, "rt studio entity", VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize * 5 / 4 ))
					continue;
			}

			d->key = seg->key;
			d->model = seg->model;
			d->num_tris = seg->num_tris;
			vk_rt.stat_builds++;
		}
		else
		{
			infos[count].srcAccelerationStructure = d->as.handle;
			vk_rt.stat_updates++;
		}

		d->last_frame = now;
		infos[count].dstAccelerationStructure = d->as.handle;
		scratch_offsets[count] = scratch_size;
		scratch_size += VK_AlignUp( update ? sizes.updateScratchSize : sizes.buildScratchSize, align );
		ranges[count] = (VkAccelerationStructureBuildRangeInfoKHR){ seg->num_tris, 0, 0, 0 };
		range_ptrs[count] = &ranges[count];

		VK_RTInstance( &instances[num_instances++], identity, vk_rt.num_geometries + i,
			seg->viewmodel ? RT_MASK_VIEWMODEL : RT_MASK_MODELS, d->as.address );
		count++;
	}

	// the entities not seen for a while give their memory back
	for( int i = 0; i < VK_RT_MAX_SEGMENTS; i++ )
	{
		vk_rt_dynamic_t *d = &frame->dynamic[i];

		if( d->as.handle && now > d->last_frame + VK_RT_IDLE_FRAMES )
		{
			VK_DestroyAS( &d->as );
			memset( d, 0, sizeof( *d ));
		}
	}

	if( !count )
		return 0;

	if( !VK_RTScratch( &frame->scratch, scratch_size, "rt scratch" ))
		return 0;

	const VkDeviceAddress base = VK_ScratchAddress( &frame->scratch );
	for( int i = 0; i < count; i++ )
		infos[i].scratchData.deviceAddress = base + scratch_offsets[i];

	vkCmdBuildAccelerationStructuresKHR( cmd, count, infos, range_ptrs );
	VK_RTBuildBarrier( cmd );
	return num_instances;
}

/*
=================
VK_RTBuildScene

the main view is drawn: the studio BLAS of what it drew, then the TLAS of
the frame, before the passes tracing rays through it
=================
*/
void VK_RTBuildScene( void )
{
	VkCommandBuffer cmd = VK_FrameCommands();
	vk_rt_frame_t *frame = &vk_rt.frames[VK_FrameIndex()];
	VkAccelerationStructureInstanceKHR *instances;
	VkDeviceAddress studio_verts = 0, studio_tris = 0, instances_addr, geometries_addr, scene_addr;
	matrix4x4 identity;
	int num_instances = 0;

	if( !vk_rt.active || !cmd )
		return;

	VK_EndMainPass();
	VK_ProfileMark( cmd, VK_PROF_RT_BUILD );
	VK_BeginLabel( cmd, "rt build" );
	Matrix4x4_LoadIdentity( identity );
	vk_rt.stat_builds = vk_rt.stat_updates = 0;

	// the captured triangles, read by the builds and the shaders
	if( vk_rt.num_studio )
	{
		float *v = VK_RTDynamic( vk_rt.num_studio * 15 * sizeof( float ), 16, &studio_verts );
		vk_rt_surface_t *t = VK_RTDynamic( vk_rt.num_studio * sizeof( vk_rt_surface_t ), 16, &studio_tris );

		if( !v || !t )
		{
			VK_EndLabel( cmd );
			return;
		}

		memcpy( v, vk_rt.studio_verts, vk_rt.num_studio * 15 * sizeof( float ));
		memcpy( t, vk_rt.studio_tris, vk_rt.num_studio * sizeof( vk_rt_surface_t ));
	}

	instances = VK_RTDynamic( VK_RT_MAX_INSTANCES * sizeof( *instances ), 16, &instances_addr );
	if( !instances )
	{
		VK_EndLabel( cmd );
		return;
	}

	if( vk_rt.num_studio )
		num_instances = VK_RTBuildStudio( cmd, frame, studio_verts, instances );

	// the map, and its brush models where their entities are
	VK_RTInstance( &instances[num_instances++], identity, vk_rt.world.geometry_base, RT_MASK_WORLD, vk_rt.world.as.address );
	if( vk_rt.water.as.handle )
		VK_RTInstance( &instances[num_instances++], identity, vk_rt.water.geometry_base, RT_MASK_WATER, vk_rt.water.as.address );
	for( int list = 0; list < 2; list++ )
	{
		cl_entity_t **ents = list ? tr.draw_list->trans_entities : tr.draw_list->solid_entities;
		const uint count = list ? tr.draw_list->num_trans_entities : tr.draw_list->num_solid_entities;

		for( uint i = 0; i < count && num_instances < VK_RT_MAX_INSTANCES; i++ )
		{
			const cl_entity_t *e = ents[i];
			int index;
			matrix4x4 m;

			if( !e->model || e->model->type != mod_brush || e->model->name[0] != '*' )
				continue;

			index = e->curstate.modelindex - 1;
			if( index < 0 || index >= MAX_MODELS || !vk_rt.submodels[index].as.handle )
				continue;

			Matrix4x4_CreateFromEntity( m, e->angles, e->origin, 1.0f );
			VK_RTInstance( &instances[num_instances++], m, vk_rt.submodels[index].geometry_base,
				e->curstate.rendermode == kRenderNormal ? RT_MASK_WORLD : RT_MASK_TRANS, vk_rt.submodels[index].as.address );
		}
	}

	// the TLAS
	VkAccelerationStructureGeometryKHR tlas_geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry.instances = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
			.data.deviceAddress = instances_addr,
		},
	};
	VkAccelerationStructureBuildRangeInfoKHR tlas_range = { num_instances, 0, 0, 0 };

	// the TLAS's scratch is another part of the buffer would be needed to
	// build it alongside: after the BLAS builds, the barrier above orders them
	if( !VK_RTBuildAS( cmd, &frame->tlas, "rt scene", VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &tlas_geometry, &tlas_range, 1,
		VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR, &frame->scratch ))
	{
		VK_EndLabel( cmd );
		return;
	}

	VK_RTBarrier( cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR );

	// what the shaders read: the brush entries, then one per studio segment
	vk_rt_geometry_t *geometries = VK_RTDynamic(( vk_rt.num_geometries + vk_rt.num_segments ) * sizeof( vk_rt_geometry_t ), 16, &geometries_addr );
	vk_rt_scene_t *scene = VK_RTDynamic( sizeof( vk_rt_scene_t ), 16, &scene_addr );

	vk_rt.stat_instances = num_instances;

	if( geometries && scene )
	{
		memcpy( geometries, vk_rt.geometries, vk_rt.num_geometries * sizeof( vk_rt_geometry_t ));
		for( int i = 0; i < vk_rt.num_segments; i++ )
			geometries[vk_rt.num_geometries + i] = (vk_rt_geometry_t){ 0, vk_rt.segments[i].first_tri, RT_KIND_STUDIO, 0 };

		scene->brush_vertices = vk_rt.vertices;
		scene->brush_indices = VK_BufferAddress( vk_rt.indices.buffer );
		scene->brush_prims = VK_BufferAddress( vk_rt.prims.buffer );
		scene->surfaces = VK_BufferAddress( vk_rt.surfaces.buffer );
		scene->geometries = geometries_addr;
		scene->studio_vertices = studio_verts;
		scene->studio_tris = studio_tris;
		vk_rt.scene = scene_addr;
		vk_rt.built_frame = VK_CurrentFrame();
	}

	VK_EndLabel( cmd );
}

/*
=================
Debug view
=================
*/
static qboolean VK_RTInitDebug( void )
{
	VkDescriptorSetLayoutBinding bindings[3] = {
		{ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
	};

	VkDescriptorSetLayoutCreateInfo layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = bindings,
	};

	VkDescriptorPoolSize sizes[3] = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_FRAMES_IN_FLIGHT },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_FRAMES_IN_FLIGHT },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_FRAMES_IN_FLIGHT },
	};

	VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = VK_FRAMES_IN_FLIGHT,
		.poolSizeCount = 3,
		.pPoolSizes = sizes,
	};

	if( vkCreateDescriptorSetLayout( vk.device, &layout, NULL, &vk_rt.debug_layout ) != VK_SUCCESS
		|| vkCreateDescriptorPool( vk.device, &pool, NULL, &vk_rt.debug_pool ) != VK_SUCCESS )
		return false;

	VkDescriptorSetLayout set_layouts[2] = { vk_pipe.texture_layout, vk_rt.debug_layout };
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( vk_rt_debug_push_t ) };
	VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push,
	};

	if( vkCreatePipelineLayout( vk.device, &pipeline_layout, NULL, &vk_rt.debug_pipeline_layout ) != VK_SUCCESS )
		return false;

	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
	{
		VkDescriptorSetAllocateInfo alloc = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = vk_rt.debug_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &vk_rt.debug_layout,
		};

		if( vkAllocateDescriptorSets( vk.device, &alloc, &vk_rt.frames[i].debug_set ) != VK_SUCCESS )
			return false;
	}

	VkShaderModule module = VK_CreateShader( rt_debug_spv, sizeof( rt_debug_spv ), "rt_debug.comp" );
	if( !module )
		return false;

	VkComputePipelineCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = module,
			.pName = "main",
		},
		.layout = vk_rt.debug_pipeline_layout,
	};

	VkResult result = vkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &info, NULL, &vk_rt.debug_pipeline );
	vkDestroyShaderModule( vk.device, module, NULL );

	VkSamplerCreateInfo sampler = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};

	return result == VK_SUCCESS && vkCreateSampler( vk.device, &sampler, NULL, &vk_rt.nearest ) == VK_SUCCESS;
}

/*
=================
VK_RTDebugView

r_vk_rt_debug: the view's rectangle (bottom left origin, as VK_SetViewport
takes it) traced through this frame's TLAS, over what was drawn
=================
*/
void VK_RTDebugView( int x, int y, int w, int h )
{
	VkCommandBuffer cmd = VK_FrameCommands();
	vk_rt_frame_t *frame = &vk_rt.frames[VK_FrameIndex()];
	vk_image_t *out, *main_color = VK_MainColorImage(), *depth = VK_MainDepthImage();
	vk_rt_debug_push_t push = { 0 };
	matrix4x4 mvp, inv;
	int width, height;

	if( !r_vk_rt_debug.value || !vk_rt.scene || vk_rt.built_frame != VK_CurrentFrame() || !cmd || !vk_rt.debug_pipeline )
		return;

	VK_MainExtent( &width, &height );
	y = height - y - h; // top down
	if( w <= 0 || h <= 0 )
		return;

	out = VK_GraphImage( "rt debug", VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0 );
	if( !out )
		return;

	VK_EndMainPass();
	VK_BeginLabel( cmd, "rt debug" );

	VK_GraphUse( cmd, out, VK_USE_STORAGE );
	VK_GraphUse( cmd, depth, VK_USE_SAMPLED );

	VkWriteDescriptorSetAccelerationStructureKHR as_info = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &frame->tlas.handle,
	};
	VkDescriptorImageInfo image = { VK_NULL_HANDLE, out->view, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorImageInfo depth_info = { vk_rt.nearest, VK_MainDepthSampleView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet writes[3] = {
		{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = &as_info, .dstSet = frame->debug_set, .dstBinding = 0,
			.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR },
		{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = frame->debug_set, .dstBinding = 1,
			.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &image },
		{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = frame->debug_set, .dstBinding = 2,
			.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &depth_info },
	};

	// this frame slot's set: the GPU is done with its previous frame
	vkUpdateDescriptorSets( vk.device, 3, writes, 0, NULL );

	Matrix4x4_Concat( mvp, RI.projectionMatrix, RI.worldviewMatrix );
	Matrix4x4_Invert_Full( inv, mvp );
	Matrix4x4_ToArrayFloatGL( inv, push.inv_view_proj );
	push.rect[0] = x;
	push.rect[1] = y;
	push.rect[2] = w;
	push.rect[3] = h;
	VectorCopy( RI.rvp.vieworigin, push.eye );
	push.eye[3] = 4.0f; // R_SetupProjectionMatrix
	VectorCopy( RI.vforward, push.forward );
	push.forward[3] = Q_max( 256.0f, RI.farClip );
	push.scene = vk_rt.scene;
	push.mode = (int)r_vk_rt_debug.value;

	VkDescriptorSet sets[2] = { vk_pipe.texture_set, frame->debug_set };
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_rt.debug_pipeline );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_rt.debug_pipeline_layout, 0, 2, sets, 0, NULL );
	vkCmdPushConstants( cmd, vk_rt.debug_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
	vkCmdDispatch( cmd, ( w + 7 ) / 8, ( h + 7 ) / 8, 1 );

	// over the view, under the 2D
	VK_GraphUse( cmd, out, VK_USE_COPY_SRC );
	VK_GraphUse( cmd, main_color, VK_USE_COPY_DST );

	VkImageCopy copy = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.srcOffset = { x, y, 0 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstOffset = { x, y, 0 },
		.extent = { w, h, 1 },
	};

	vkCmdCopyImage( cmd, out->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, main_color->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
	VK_EndLabel( cmd );
}

static void VK_RTInfo_f( void )
{
	if( !vk_rt.available )
	{
		gEngfuncs.Con_Printf( "ray tracing: not available on this device\n" );
		return;
	}

	gEngfuncs.Con_Printf( "ray tracing %s: map %.1f MB, %d brush geometries; last frame %d studio triangles in %d entities "
		"(%d BLAS built, %d updated), %d instances\n", vk_rt.active ? "on" : "off", vk_rt.static_bytes / ( 1024.0 * 1024.0 ),
		vk_rt.num_geometries, vk_rt.num_studio, vk_rt.num_segments, vk_rt.stat_builds, vk_rt.stat_updates, vk_rt.stat_instances );
}

/*
=================
Lifetime
=================
*/
void VK_InitRT( void )
{
	memset( &vk_rt, 0, sizeof( vk_rt ));

	if( !vk.caps.rt )
		return;

	vk_rt.props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &vk_rt.props };
	vkGetPhysicalDeviceProperties2( vk.physical_device, &props );

	if( !VK_RTInitDebug( ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create the ray tracing debug view\n" );
		return;
	}

	vk_rt.available = true;
	gEngfuncs.Cmd_AddCommand( "r_vk_rtinfo", VK_RTInfo_f, "the ray traced scene of the last frame" );
}

void VK_ShutdownRT( void )
{
	if( !vk.device )
		return;

	vkDeviceWaitIdle( vk.device );
	VK_RTFreeMap();
	VK_RTFreeMapData();

	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
	{
		for( int j = 0; j < VK_RT_MAX_SEGMENTS; j++ )
			VK_DestroyAS( &vk_rt.frames[i].dynamic[j].as );
		VK_DestroyAS( &vk_rt.frames[i].tlas );
		VK_DestroyBuffer( &vk_rt.frames[i].scratch );
	}

	if( vk_rt.debug_pipeline )
		vkDestroyPipeline( vk.device, vk_rt.debug_pipeline, NULL );
	if( vk_rt.debug_pipeline_layout )
		vkDestroyPipelineLayout( vk.device, vk_rt.debug_pipeline_layout, NULL );
	if( vk_rt.debug_pool )
		vkDestroyDescriptorPool( vk.device, vk_rt.debug_pool, NULL );
	if( vk_rt.debug_layout )
		vkDestroyDescriptorSetLayout( vk.device, vk_rt.debug_layout, NULL );
	if( vk_rt.nearest )
		vkDestroySampler( vk.device, vk_rt.nearest, NULL );
	if( vk_rt.studio_verts )
		Mem_Free( vk_rt.studio_verts );
	if( vk_rt.studio_tris )
		Mem_Free( vk_rt.studio_tris );

	gEngfuncs.Cmd_RemoveCommand( "r_vk_rtinfo" );
	memset( &vk_rt, 0, sizeof( vk_rt ));
}
