/*
vk_material.c - material definitions (.mat) for high resolution and PBR textures
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
A material gives a game texture its replacement maps. It is a text file
next to where the engine looks for TGA replacements (host_allow_materials,
Documentation/hd-textures.md), with the same name and the .mat extension:

  materials/<map model name>/<texture>.mat   e.g. materials/maps/de_dust2.bsp/sandwall.mat
  materials/common/<texture>.mat
  materials/models/<model>/<texture>.mat     studio models

made of "key value" pairs, // comments allowed:

  albedo    sandwall.ktx2       colour, replaces the game texture in every look
  normal    sandwall_n.ktx2     tangent space normal map, green up (OpenGL)
  rma       sandwall_rma.ktx2   roughness, metalness, ambient occlusion
  emissive  sandwall_e.ktx2     emitted colour
  height    sandwall_h.ktx2     height (parallax), reserved
  emissive_strength 4           multiplies the emissive map
  reflective 1                  for the ray traced reflections (story 025)

A file name without a directory is next to the .mat; anything the engine
image loader reads works (TGA, PNG, DDS, KTX2 with BC4 / BC5 / BC7). The
maps are sampled with the game texture's coordinates, so their size is
free. The classic look only uses the albedo, r_vk_quality enhanced all of
them (world surfaces for now).

The renderer keeps one material per game texture number, and the list of
materials in a storage buffer the world shaders index (world_material.glsl).
*/

#include "vk_local.h"

#define VK_MAX_MATERIALS 2048

typedef struct
{
	char path[MAX_QPATH];   // of the .mat, to share it between textures
	int albedo, normal, rma, emissive, height; // texture numbers, 0 when absent
	float emissive_strength;
	qboolean reflective;
	int refs;               // game textures using it
} vk_material_t;

// mirrors world_material.glsl
typedef struct
{
	int albedo, normal, rma, emissive;
	float emissive_strength;
	int flags;
	int pad[2];
} vk_material_gpu_t;

#define VK_MATERIAL_REFLECTIVE 1

static struct
{
	vk_material_t list[VK_MAX_MATERIALS]; // index 0 unused: "no material"
	int count;
	unsigned short of_texture[VK_MAX_TEXTURES]; // game texture -> material
	vk_buffer_t buffer;
	VkDeviceSize buffer_size;
	qboolean dirty; // the buffer is behind the list
} vk_mat;

/*
=================
Parsing
=================
*/
static int VK_LoadMaterialMap( const char *mat_path, const char *value, int flags )
{
	char path[MAX_QPATH];
	int texnum;

	// a bare name is next to the .mat
	if( !Q_strchr( value, '/' ))
	{
		char dir[MAX_QPATH];

		COM_ExtractFilePath( mat_path, dir );
		Q_snprintf( path, sizeof( path ), "%s/%s", dir, value );
	}
	else Q_strncpy( path, value, sizeof( path ));

	texnum = GL_LoadTexture( path, NULL, 0, flags );

	if( !texnum )
		gEngfuncs.Con_Printf( S_WARN "%s: can't load %s\n", mat_path, path );

	return texnum;
}

// GL_LoadTexture gives the same texture to every .mat naming the same file
static void VK_ReleaseMap( const vk_material_t *owner, int texnum )
{
	if( !texnum )
		return;

	for( int i = 1; i < vk_mat.count; i++ )
	{
		const vk_material_t *mat = &vk_mat.list[i];

		if( mat != owner && mat->path[0] && ( mat->albedo == texnum || mat->normal == texnum
			|| mat->rma == texnum || mat->emissive == texnum || mat->height == texnum ))
			return;
	}

	GL_FreeTexture( texnum );
}

static void VK_FreeMaterial( vk_material_t *mat )
{
	VK_ReleaseMap( mat, mat->albedo );
	VK_ReleaseMap( mat, mat->normal );
	VK_ReleaseMap( mat, mat->rma );
	VK_ReleaseMap( mat, mat->emissive );
	VK_ReleaseMap( mat, mat->height );
	memset( mat, 0, sizeof( *mat ));
	vk_mat.dirty = true;
}

// the index of an already loaded .mat, or of a free slot
static int VK_FindMaterialSlot( const char *path, qboolean *loaded )
{
	int free_slot = 0;

	for( int i = 1; i < vk_mat.count; i++ )
	{
		if( !vk_mat.list[i].path[0] )
		{
			if( !free_slot )
				free_slot = i;
		}
		else if( !Q_stricmp( vk_mat.list[i].path, path ))
		{
			*loaded = true;
			return i;
		}
	}

	*loaded = false;

	if( free_slot )
		return free_slot;

	if( vk_mat.count >= VK_MAX_MATERIALS )
		return 0;

	if( !vk_mat.count )
		vk_mat.count = 1; // keep 0 for "no material"

	return vk_mat.count++;
}

static int VK_LoadMaterial( const char *path, int texflags )
{
	char token[MAX_QPATH], key[MAX_QPATH];
	qboolean loaded;
	byte *data;
	char *p;
	int index;

	index = VK_FindMaterialSlot( path, &loaded );
	if( loaded || !index )
		return index;

	data = gEngfuncs.fsapi->LoadFile( path, NULL, false );
	if( !data )
		return 0;

	vk_material_t *mat = &vk_mat.list[index];
	memset( mat, 0, sizeof( *mat ));
	Q_strncpy( mat->path, path, sizeof( mat->path ));
	mat->emissive_strength = 1.0f;

	for( p = (char *)data; ( p = COM_ParseFile( p, key, sizeof( key ))) != NULL; )
	{
		if( !( p = COM_ParseFile( p, token, sizeof( token ))))
		{
			gEngfuncs.Con_Printf( S_WARN "%s: no value for %s\n", path, key );
			break;
		}

		if( !Q_stricmp( key, "albedo" ))
			mat->albedo = VK_LoadMaterialMap( path, token, texflags );
		else if( !Q_stricmp( key, "normal" ))
			mat->normal = VK_LoadMaterialMap( path, token, texflags | TF_NORMALMAP );
		else if( !Q_stricmp( key, "rma" ))
			mat->rma = VK_LoadMaterialMap( path, token, texflags );
		else if( !Q_stricmp( key, "emissive" ))
			mat->emissive = VK_LoadMaterialMap( path, token, texflags );
		else if( !Q_stricmp( key, "height" ))
			mat->height = VK_LoadMaterialMap( path, token, texflags );
		else if( !Q_stricmp( key, "emissive_strength" ))
			mat->emissive_strength = Q_atof( token );
		else if( !Q_stricmp( key, "reflective" ))
			mat->reflective = Q_atoi( token ) != 0;
		else gEngfuncs.Con_Printf( S_WARN "%s: unknown key %s\n", path, key );
	}

	Mem_Free( data );
	vk_mat.dirty = true;
	return index;
}

/*
=================
Binding to game textures
=================
*/
static void VK_UnbindMaterial( int texnum )
{
	int index;

	if( texnum <= 0 || texnum >= VK_MAX_TEXTURES || !( index = vk_mat.of_texture[texnum] ))
		return;

	vk_mat.of_texture[texnum] = 0;

	if( --vk_mat.list[index].refs <= 0 )
		VK_FreeMaterial( &vk_mat.list[index] );
}

// the game texture is freed: so is its material once nothing else uses it
void VK_MaterialTextureFreed( int texnum )
{
	VK_UnbindMaterial( texnum );
}

/*
=================
VK_FindMaterial

looks for <texture>.mat in the replacement directories of a model, in the
engine's order, and binds what it finds to the game texture
=================
*/
void VK_FindMaterial( int texnum, const char *modelname, const char *texname, qboolean common, int texflags )
{
	const char *subdirs[2] = { modelname, "common" };
	char path[MAX_QPATH];
	int index = 0;

	if( texnum <= 0 || texnum >= VK_MAX_TEXTURES || !Mod_AllowMaterials( ))
		return;

	VK_UnbindMaterial( texnum ); // texture numbers are reused

	for( int i = 0; i < ( common ? 2 : 1 ) && !index; i++ )
	{
		if( Q_snprintf( path, sizeof( path ), "materials/%s/%s.mat", subdirs[i], texname ) < 0 )
			continue;

		if( gEngfuncs.fsapi->FileExists( path, false ))
			index = VK_LoadMaterial( path, texflags );
	}

	if( host_allow_materials->value == 2.0f )
		gEngfuncs.Con_Printf( "Looking for %s:%s material...%s\n", modelname, texname,
			index ? S_GREEN "OK" : S_YELLOW "MISS" );

	if( !index )
		return;

	vk_mat.of_texture[texnum] = index;
	vk_mat.list[index].refs++;
}

static const vk_material_t *VK_MaterialForTexture( int texnum )
{
	int index;

	if( texnum <= 0 || texnum >= VK_MAX_TEXTURES || !( index = vk_mat.of_texture[texnum] ))
		return NULL;

	return &vk_mat.list[index];
}

// what the draws sample in place of the game texture
int VK_MaterialAlbedo( int texnum )
{
	const vk_material_t *mat = VK_MaterialForTexture( texnum );

	return mat && mat->albedo ? mat->albedo : texnum;
}

// the material index the world shaders take, 0 for none
int VK_MaterialIndex( int texnum )
{
	return texnum > 0 && texnum < VK_MAX_TEXTURES ? vk_mat.of_texture[texnum] : 0;
}

// the normal map needs the light directions of the lightmaps
qboolean VK_MaterialHasNormal( int texnum )
{
	const vk_material_t *mat = VK_MaterialForTexture( texnum );

	return mat && mat->normal;
}

// a material with something to add in the material pass: specular or glow
qboolean VK_MaterialShades( int texnum )
{
	const vk_material_t *mat = VK_MaterialForTexture( texnum );

	return mat && ( mat->normal || mat->rma || mat->emissive );
}

/*
=================
VK_MaterialsBeginFrame

the storage buffer follows the list, rebuilt when it changed (map and
model loads): waiting for the GPU then is no hitch
=================
*/
static qboolean VK_UploadMaterials( void )
{
	const int count = Q_max( vk_mat.count, 1 );
	const VkDeviceSize size = count * sizeof( vk_material_gpu_t );
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;
	vk_material_gpu_t *dst;

	if( size > vk_mat.buffer_size )
	{
		vkDeviceWaitIdle( vk.device );
		VK_DestroyBuffer( &vk_mat.buffer );

		vk_mat.buffer_size = Q_max( size, 64 * sizeof( vk_material_gpu_t )) * 2;
		if( !VK_CreateBuffer( &vk_mat.buffer, "materials", vk_mat.buffer_size,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ))
		{
			vk_mat.buffer_size = 0;
			return false;
		}

		VK_WriteWorldDataDescriptor( VK_BINDING_MATERIALS, vk_mat.buffer.buffer, vk_mat.buffer_size );
	}

	dst = VK_UploadStaging( size, &staging, &offset, &cmd );
	if( !dst )
		return false;

	memset( dst, 0, size );
	for( int i = 1; i < vk_mat.count; i++ )
	{
		const vk_material_t *mat = &vk_mat.list[i];

		if( !mat->path[0] )
			continue;

		dst[i].albedo = mat->albedo;
		dst[i].normal = mat->normal;
		dst[i].rma = mat->rma;
		dst[i].emissive = mat->emissive;
		dst[i].emissive_strength = mat->emissive_strength;
		dst[i].flags = mat->reflective ? VK_MATERIAL_REFLECTIVE : 0;
	}

	VK_UploadBufferRegion( cmd, staging, offset, vk_mat.buffer.buffer, 0, size );
	return true;
}

void VK_MaterialsBeginFrame( void )
{
	if( vk_mat.dirty || !vk_mat.buffer_size )
		vk_mat.dirty = !VK_UploadMaterials();
}

void VK_InitMaterials( void )
{
	memset( &vk_mat, 0, sizeof( vk_mat ));
	vk_mat.dirty = true;
}

void VK_ShutdownMaterials( void )
{
	if( vk.device )
		VK_DestroyBuffer( &vk_mat.buffer );

	memset( &vk_mat, 0, sizeof( vk_mat ));
}
