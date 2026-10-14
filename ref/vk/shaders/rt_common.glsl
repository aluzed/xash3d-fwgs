// the ray traced scene of vk_rt.c, mirrored by its structures: what a ray
// query hit is, read through buffer device addresses

#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "bindings.h"

// instance masks
#define RT_MASK_WORLD     0x01
#define RT_MASK_MODELS    0x02
#define RT_MASK_VIEWMODEL 0x04
#define RT_MASK_WATER     0x08
#define RT_MASK_TRANS     0x10

#define RT_KIND_BRUSH  0 // world and brush entity triangles: indexed glpoly2_t vertices
#define RT_KIND_STUDIO 1 // studio model triangles, captured each frame

#define RT_SURF_ALPHA  1 // alpha tested texture
#define RT_SURF_WATER  2

// a geometry of a BLAS: instance custom index + geometry index
struct rt_geometry_s
{
	uint index_offset; // brush: first index in the index buffer
	uint prim_offset;  // first triangle in the per triangle data
	uint kind;
	uint pad;
};

struct rt_surface_s
{
	int texture;
	int flags;
};

layout( buffer_reference, std430, buffer_reference_align = 4 ) readonly buffer rt_floats_r { float v[]; };
layout( buffer_reference, std430, buffer_reference_align = 4 ) readonly buffer rt_uints_r { uint v[]; };
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer rt_geometries_r { rt_geometry_s v[]; };
layout( buffer_reference, std430, buffer_reference_align = 8 ) readonly buffer rt_surfaces_r { rt_surface_s v[]; };

// one per frame, in the dynamic ring
layout( buffer_reference, std430, buffer_reference_align = 8 ) readonly buffer rt_scene_r
{
	uint64_t brush_vertices;  // glpoly2_t vertices: xyz, st, lightmap st
	uint64_t brush_indices;
	uint64_t brush_prims;     // surface number of each triangle
	uint64_t surfaces;        // rt_surface_s per surface of the world model
	uint64_t geometries;      // rt_geometry_s
	uint64_t studio_vertices; // xyz, st per vertex, three per triangle
	uint64_t studio_tris;     // rt_surface_s per triangle
};

layout( set = VK_SET_TEXTURES, binding = VK_BINDING_TEXTURES ) uniform sampler2D textures[VK_MAX_TEXTURES];

#define BRUSH_STRIDE 7

struct rt_hit_s
{
	vec3 position;
	vec3 normal;    // geometric, facing the ray
	vec2 uv;
	int texture;
	int flags;
};

// the triangle of a committed or candidate intersection
rt_hit_s RTHitData( rt_scene_r scene, int custom, int geometry, int prim, vec2 bary, mat4x3 to_world, vec3 dir )
{
	rt_geometry_s g = rt_geometries_r( scene.geometries ).v[custom + geometry];
	vec3 p[3];
	vec2 st[3];
	rt_hit_s hit;

	if( g.kind == RT_KIND_BRUSH )
	{
		rt_uints_r indices = rt_uints_r( scene.brush_indices );
		rt_floats_r verts = rt_floats_r( scene.brush_vertices );

		for( int k = 0; k < 3; k++ )
		{
			uint i = indices.v[g.index_offset + prim * 3 + k] * BRUSH_STRIDE;
			p[k] = vec3( verts.v[i], verts.v[i + 1], verts.v[i + 2] );
			st[k] = vec2( verts.v[i + 3], verts.v[i + 4] );
		}

		rt_surface_s surf = rt_surfaces_r( scene.surfaces ).v[rt_uints_r( scene.brush_prims ).v[g.prim_offset + prim]];
		hit.texture = surf.texture;
		hit.flags = surf.flags;
	}
	else
	{
		rt_floats_r verts = rt_floats_r( scene.studio_vertices );
		uint tri = g.prim_offset + prim;

		for( int k = 0; k < 3; k++ )
		{
			uint i = ( tri * 3 + k ) * 5;
			p[k] = vec3( verts.v[i], verts.v[i + 1], verts.v[i + 2] );
			st[k] = vec2( verts.v[i + 3], verts.v[i + 4] );
		}

		rt_surface_s surf = rt_surfaces_r( scene.studio_tris ).v[tri];
		hit.texture = surf.texture;
		hit.flags = surf.flags;
	}

	vec3 w = vec3( 1.0 - bary.x - bary.y, bary.x, bary.y );
	hit.position = to_world * vec4( p[0] * w.x + p[1] * w.y + p[2] * w.z, 1.0 );
	hit.uv = st[0] * w.x + st[1] * w.y + st[2] * w.z;
	hit.normal = normalize( mat3( to_world ) * cross( p[1] - p[0], p[2] - p[0] ));
	if( dot( hit.normal, dir ) > 0.0 )
		hit.normal = -hit.normal;

	return hit;
}

// closest hit, alpha tested triangles let the ray through where transparent
bool RTTrace( accelerationStructureEXT tlas, rt_scene_r scene, vec3 origin, vec3 dir, float tmax, uint mask,
	out rt_hit_s hit, out int instance, out float t )
{
	rayQueryEXT rq;

	rayQueryInitializeEXT( rq, tlas, gl_RayFlagsNoneEXT, mask, origin, 0.0, dir, tmax );

	while( rayQueryProceedEXT( rq ))
	{
		if( rayQueryGetIntersectionTypeEXT( rq, false ) != gl_RayQueryCandidateIntersectionTriangleEXT )
			continue;

		rt_hit_s c = RTHitData( scene, rayQueryGetIntersectionInstanceCustomIndexEXT( rq, false ),
			rayQueryGetIntersectionGeometryIndexEXT( rq, false ), rayQueryGetIntersectionPrimitiveIndexEXT( rq, false ),
			rayQueryGetIntersectionBarycentricsEXT( rq, false ), rayQueryGetIntersectionObjectToWorldEXT( rq, false ), dir );

		// glAlphaFunc( GL_GREATER, 0.25 ) of masked textures
		if( c.texture <= 0 || textureLod( textures[nonuniformEXT( c.texture )], c.uv, 0.0 ).a > 0.25 )
			rayQueryConfirmIntersectionEXT( rq );
	}

	if( rayQueryGetIntersectionTypeEXT( rq, true ) != gl_RayQueryCommittedIntersectionTriangleEXT )
		return false;

	t = rayQueryGetIntersectionTEXT( rq, true );
	instance = rayQueryGetIntersectionInstanceIdEXT( rq, true );
	hit = RTHitData( scene, rayQueryGetIntersectionInstanceCustomIndexEXT( rq, true ),
		rayQueryGetIntersectionGeometryIndexEXT( rq, true ), rayQueryGetIntersectionPrimitiveIndexEXT( rq, true ),
		rayQueryGetIntersectionBarycentricsEXT( rq, true ), rayQueryGetIntersectionObjectToWorldEXT( rq, true ), dir );
	return true;
}
