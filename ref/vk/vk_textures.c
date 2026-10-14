/*
vk_textures.c - texture registry
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
 * Only names and handles for now: client DLLs treat a 0 handle as a
 * failed load (cs16-client aborts the map without its scope textures), so
 * every load must hand out a real handle before images exist on the GPU.
 * Uploads come with the texture manager (story 009).
 */

#define VK_MAX_TEXTURES 8192 // same limit as ref_gl

typedef struct vk_texture_s
{
	char name[64];
	int width;
	int height;
	texFlags_t flags;
} vk_texture_t;

static vk_texture_t vk_textures[VK_MAX_TEXTURES]; // index 0 is never used

static int VK_FindTextureByName( const char *name )
{
	for( int i = 1; i < VK_MAX_TEXTURES; i++ )
	{
		if( vk_textures[i].name[0] && !Q_stricmp( vk_textures[i].name, name ))
			return i;
	}

	return 0;
}

static int VK_RegisterTexture( const char *name, int width, int height, texFlags_t flags )
{
	int texnum;

	if( COM_StringEmptyOrNULL( name ))
		return 0;

	texnum = VK_FindTextureByName( name );

	if( !texnum )
	{
		for( int i = 1; i < VK_MAX_TEXTURES; i++ )
		{
			if( !vk_textures[i].name[0] )
			{
				texnum = i;
				break;
			}
		}

		if( !texnum )
		{
			gEngfuncs.Con_Printf( S_ERROR "ref_vk: texture table full, can't add %s\n", name );
			return 0;
		}

		Q_strncpy( vk_textures[texnum].name, name, sizeof( vk_textures[texnum].name ));
	}

	vk_textures[texnum].width = width;
	vk_textures[texnum].height = height;
	vk_textures[texnum].flags = flags;

	return texnum;
}

void VK_InitTextures( void )
{
	memset( vk_textures, 0, sizeof( vk_textures ));
}

void VK_ShutdownTextures( void )
{
	memset( vk_textures, 0, sizeof( vk_textures ));
}

int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	return VK_RegisterTexture( name, 0, 0, flags );
}

int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	if( !pic )
		return 0;

	return VK_RegisterTexture( name, pic->width, pic->height, flags );
}

int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	return VK_RegisterTexture( name, width, height, flags );
}

int GL_FindTexture( const char *name )
{
	if( COM_StringEmptyOrNULL( name ))
		return 0;

	return VK_FindTextureByName( name );
}

const char *GL_TextureName( unsigned int texnum )
{
	if( texnum == 0 || texnum >= VK_MAX_TEXTURES || !vk_textures[texnum].name[0] )
		return NULL;

	return vk_textures[texnum].name;
}

void GL_FreeTexture( unsigned int texnum )
{
	if( texnum == 0 || texnum >= VK_MAX_TEXTURES )
		return;

	memset( &vk_textures[texnum], 0, sizeof( vk_textures[texnum] ));
}
