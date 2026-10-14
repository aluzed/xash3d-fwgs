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
#define RT_MASK_BRUSH     0x20

#define RT_KIND_BRUSH  0 // world and brush entity triangles: indexed glpoly2_t vertices
#define RT_KIND_STUDIO 1 // studio model triangles, captured each frame

#define RT_SURF_ALPHA   1 // alpha tested texture
#define RT_SURF_WATER   2
#define RT_SURF_REFLECT 4 // material with reflective 1

// rt_instance_s flags
#define RT_INST_WORLD     1
#define RT_INST_BRUSH     2
#define RT_INST_STUDIO    4
#define RT_INST_VIEWMODEL 8
#define RT_INST_GLASS     16  // brush entity drawn with kRenderTransTexture
#define RT_INST_SKYLIT    32  // studio model lit by the sky (R_EntityDynamicLight)
#define RT_INST_HIDDEN    64  // online, out of the player's sight: casts and reflects nothing (rt_visibility.comp)

#define RT_STUDIO_STRIDE  6   // floats a studio vertex: xyz, st, colour (RGBA8)
#define BRUSH_STRIDE      7   // glpoly2_t: xyz, st, lightmap st

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
	int lightmap;      // brush: lightmap page texture
	int pad;
};

// one per instance of the TLAS
struct rt_instance_s
{
	vec4 origin;       // entity origin
	uint flags;        // RT_INST_*
	int entity;        // index, -1 for the view model
	float ambient;     // studio lighting it was drawn with (R_StudioLighting)
	float shade;
};

// a dynamic light, as R_AddDynamicLights adds it to the lightmaps
struct rt_light_s
{
	vec4 origin_radius;
	vec4 color_minlight; // colour 0-255
	int key;             // entity it follows
	int pad0, pad1, pad2;
};

layout( buffer_reference, std430, buffer_reference_align = 4 ) readonly buffer rt_floats_r { float v[]; };
layout( buffer_reference, std430, buffer_reference_align = 4 ) readonly buffer rt_uints_r { uint v[]; };
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer rt_geometries_r { rt_geometry_s v[]; };
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer rt_surfaces_r { rt_surface_s v[]; };
layout( buffer_reference, std430, buffer_reference_align = 16 ) buffer rt_instances_r { rt_instance_s v[]; };
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer rt_lights_r { rt_light_s v[]; };

// one per frame, in the dynamic ring
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer rt_scene_r
{
	uint64_t brush_vertices;  // glpoly2_t vertices: xyz, st, lightmap st
	uint64_t brush_indices;
	uint64_t brush_prims;     // surface number of each triangle
	uint64_t surfaces;        // rt_surface_s per surface of the world model
	uint64_t geometries;      // rt_geometry_s
	uint64_t studio_vertices; // RT_STUDIO_STRIDE floats per vertex, three per triangle
	uint64_t studio_tris;     // rt_surface_s per triangle
	uint64_t instances;       // rt_instance_s per instance of the TLAS
	uint64_t lights;          // rt_light_s
	uint64_t gamma;           // uint lightgammatable[1024], then float inverse[256]
	ivec4 sky;                // skybox textures of the axes 0-3
	ivec4 sky2;               // axes 4, 5; number of lights; R_BuildLightMap's lightscale
	ivec4 counts;             // x: instances
	vec4 sun_dir;             // towards the sun
	vec4 sun_color;           // movevars skycolor, lightmap units (0-255)
};

layout( set = VK_SET_TEXTURES, binding = VK_BINDING_TEXTURES ) uniform sampler2D textures[VK_MAX_TEXTURES];

struct rt_hit_s
{
	vec3 position;
	vec3 normal;    // geometric, facing the ray
	vec2 uv;
	int texture;
	int flags;
	uint kind;
	vec2 lightuv;   // brush: lightmap page coordinates
	int lightmap;   // brush: lightmap page texture
	vec4 color;     // studio: lit vertex colour
};

vec4 RTUnpackColor( float packed )
{
	return unpackUnorm4x8( floatBitsToUint( packed ));
}

// the triangle of a committed or candidate intersection
rt_hit_s RTHitData( rt_scene_r scene, int custom, int geometry, int prim, vec2 bary, mat4x3 to_world, vec3 dir )
{
	rt_geometry_s g = rt_geometries_r( scene.geometries ).v[custom + geometry];
	vec3 w = vec3( 1.0 - bary.x - bary.y, bary.x, bary.y );
	vec3 p[3];
	vec2 st[3];
	rt_hit_s hit;

	hit.kind = g.kind;
	hit.lightuv = vec2( 0.0 );
	hit.lightmap = 0;
	hit.color = vec4( 1.0 );

	if( g.kind == RT_KIND_BRUSH )
	{
		rt_uints_r indices = rt_uints_r( scene.brush_indices );
		rt_floats_r verts = rt_floats_r( scene.brush_vertices );
		vec2 lm[3];

		for( int k = 0; k < 3; k++ )
		{
			uint i = indices.v[g.index_offset + prim * 3 + k] * BRUSH_STRIDE;
			p[k] = vec3( verts.v[i], verts.v[i + 1], verts.v[i + 2] );
			st[k] = vec2( verts.v[i + 3], verts.v[i + 4] );
			lm[k] = vec2( verts.v[i + 5], verts.v[i + 6] );
		}

		rt_surface_s surf = rt_surfaces_r( scene.surfaces ).v[rt_uints_r( scene.brush_prims ).v[g.prim_offset + prim]];
		hit.texture = surf.texture;
		hit.flags = surf.flags;
		hit.lightmap = surf.lightmap;
		hit.lightuv = lm[0] * w.x + lm[1] * w.y + lm[2] * w.z;
	}
	else
	{
		rt_floats_r verts = rt_floats_r( scene.studio_vertices );
		uint tri = g.prim_offset + prim;
		vec4 c[3];

		for( int k = 0; k < 3; k++ )
		{
			uint i = ( tri * 3 + k ) * RT_STUDIO_STRIDE;
			p[k] = vec3( verts.v[i], verts.v[i + 1], verts.v[i + 2] );
			st[k] = vec2( verts.v[i + 3], verts.v[i + 4] );
			c[k] = RTUnpackColor( verts.v[i + 5] );
		}

		rt_surface_s surf = rt_surfaces_r( scene.studio_tris ).v[tri];
		hit.texture = surf.texture;
		hit.flags = surf.flags;
		hit.color = c[0] * w.x + c[1] * w.y + c[2] * w.z;
	}

	hit.position = to_world * vec4( p[0] * w.x + p[1] * w.y + p[2] * w.z, 1.0 );
	hit.uv = st[0] * w.x + st[1] * w.y + st[2] * w.z;
	hit.normal = normalize( mat3( to_world ) * cross( p[1] - p[0], p[2] - p[0] ));
	if( dot( hit.normal, dir ) > 0.0 )
		hit.normal = -hit.normal;

	return hit;
}

// what a ray lets through: alpha tested texels where transparent, hidden
// instances, the entity the ray starts from or goes to
#define RT_SKIP_NONE -32768

bool rt_skip_hidden = true; // false for r_vk_rt_debug 6, which shows them

bool RTCandidate( rayQueryEXT rq, rt_scene_r scene, vec3 dir, int skip_entity )
{
	int id = rayQueryGetIntersectionInstanceIdEXT( rq, false );
	rt_instance_s inst = rt_instances_r( scene.instances ).v[id];

	if((( inst.flags & RT_INST_HIDDEN ) != 0 && rt_skip_hidden ) || ( skip_entity != RT_SKIP_NONE && inst.entity == skip_entity ))
		return false;

	rt_hit_s c = RTHitData( scene, rayQueryGetIntersectionInstanceCustomIndexEXT( rq, false ),
		rayQueryGetIntersectionGeometryIndexEXT( rq, false ), rayQueryGetIntersectionPrimitiveIndexEXT( rq, false ),
		rayQueryGetIntersectionBarycentricsEXT( rq, false ), rayQueryGetIntersectionObjectToWorldEXT( rq, false ), dir );

	// glAlphaFunc( GL_GREATER, 0.25 ) of masked textures
	return ( c.flags & RT_SURF_ALPHA ) == 0 || c.texture <= 0
		|| textureLod( textures[nonuniformEXT( c.texture )], c.uv, 0.0 ).a > 0.25;
}

// closest hit
bool RTTraceSkip( accelerationStructureEXT tlas, rt_scene_r scene, vec3 origin, vec3 dir, float tmin, float tmax, uint mask,
	int skip_entity, out rt_hit_s hit, out int instance, out float t )
{
	rayQueryEXT rq;

	rayQueryInitializeEXT( rq, tlas, gl_RayFlagsNoneEXT, mask, origin, tmin, dir, tmax );

	while( rayQueryProceedEXT( rq ))
	{
		if( rayQueryGetIntersectionTypeEXT( rq, false ) == gl_RayQueryCandidateIntersectionTriangleEXT
			&& RTCandidate( rq, scene, dir, skip_entity ))
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

bool RTTrace( accelerationStructureEXT tlas, rt_scene_r scene, vec3 origin, vec3 dir, float tmax, uint mask,
	out rt_hit_s hit, out int instance, out float t )
{
	return RTTraceSkip( tlas, scene, origin, dir, 0.0, tmax, mask, RT_SKIP_NONE, hit, instance, t );
}

// anything between: the first hit found ends the ray
bool RTOccluded( accelerationStructureEXT tlas, rt_scene_r scene, vec3 origin, vec3 dir, float tmax, uint mask, int skip_entity )
{
	rayQueryEXT rq;

	rayQueryInitializeEXT( rq, tlas, gl_RayFlagsTerminateOnFirstHitEXT, mask, origin, 0.0, dir, tmax );

	while( rayQueryProceedEXT( rq ))
	{
		if( rayQueryGetIntersectionTypeEXT( rq, false ) == gl_RayQueryCandidateIntersectionTriangleEXT
			&& RTCandidate( rq, scene, dir, skip_entity ))
			rayQueryConfirmIntersectionEXT( rq );
	}

	return rayQueryGetIntersectionTypeEXT( rq, true ) == gl_RayQueryCommittedIntersectionTriangleEXT;
}

// R_BuildLightMap's encoding: 10 bit light (lightmap units * 4 * lightscale / 256) to texel
float RTLightToTexel( rt_scene_r scene, float l )
{
	rt_uints_r table = rt_uints_r( scene.gamma );
	float x = clamp( l, 0.0, 1023.0 );
	int i = int( x );
	float a = float( table.v[i] ), b = float( table.v[min( i + 1, 1023 )] );

	return mix( a, b, x - float( i )) / 1023.0;
}

// and back, from a filtered texel (0-1)
float RTTexelToLight( rt_scene_r scene, float v )
{
	rt_floats_r inverse = rt_floats_r( scene.gamma + 1024 * 4 );
	float x = clamp( v * 255.0, 0.0, 255.0 );
	int i = int( x );

	return mix( inverse.v[i], inverse.v[min( i + 1, 255 )], x - float( i ));
}

// the skybox seen in a direction (ref_skybox.c, R_DrawSkyBox)
vec3 RTSky( rt_scene_r scene, vec3 dir )
{
	vec3 a = abs( dir );
	int axis;
	vec3 st; // s, t, depth

	if( a.x > a.y && a.x > a.z )
	{
		axis = dir.x < 0.0 ? 1 : 0;
		st = dir.x < 0.0 ? vec3( dir.y, dir.z, -dir.x ) : vec3( -dir.y, dir.z, dir.x );
	}
	else if( a.y > a.z && a.y > a.x )
	{
		axis = dir.y < 0.0 ? 3 : 2;
		st = dir.y < 0.0 ? vec3( -dir.x, dir.z, -dir.y ) : vec3( dir.x, dir.z, dir.y );
	}
	else
	{
		axis = dir.z < 0.0 ? 5 : 4;
		st = dir.z < 0.0 ? vec3( -dir.y, dir.x, -dir.z ) : vec3( -dir.y, -dir.x, dir.z );
	}

	int tex = axis < 4 ? scene.sky[axis] : scene.sky2[axis - 4];
	if( tex <= 0 )
		return vec3( 0.0 );

	vec2 uv = clamp(( st.xy / st.z + 1.0 ) * 0.5, 0.0, 1.0 );
	uv.y = 1.0 - uv.y;
	return pow( textureLod( textures[nonuniformEXT( tex )], uv, 0.0 ).rgb, vec3( 2.2 ));
}
