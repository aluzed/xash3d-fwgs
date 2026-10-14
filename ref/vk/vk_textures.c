/*
vk_textures.c - texture manager
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
 * Same observable behaviour as ref_gl/gl_image.c: same texture numbers for
 * the same load sequence (first free slot, the engine and the client DLLs
 * keep them), same resampling, same CPU mipmaps and 1-bit alpha filter,
 * and the GL internal format emulated with a component swizzle (an RGB
 * texture samples alpha as 1, a luminance one replicates red). The texture
 * number is also the index in the bindless descriptor array.
 */

CVAR_DEFINE( gl_texture_anisotropy, "gl_anisotropy", "8", FCVAR_GLCONFIG, "textures anisotropic filter" );
CVAR_DEFINE_AUTO( gl_texture_lodbias, "0.0", FCVAR_GLCONFIG, "LOD bias for mipmapped textures (perfomance|quality)" );
CVAR_DEFINE_AUTO( gl_texture_nearest, "0", FCVAR_GLCONFIG, "disable texture filter" );
CVAR_DEFINE_AUTO( gl_lightmap_nearest, "0", FCVAR_GLCONFIG, "disable lightmap filter" );

#define TEXTURES_HASH_SIZE ( VK_MAX_TEXTURES >> 2 )

typedef struct vk_texture_s
{
	char name[256];
	texFlags_t flags;
	int width, height, depth; // uploaded size, depth is the layer count
	int srcWidth, srcHeight;  // size before resampling, what the engine sees
	int numMips;
	int encode;
	size_t size;              // bytes on the GPU, for texturelist
	float xscale, yscale;     // detail texture scale
	qboolean used;            // the slot holds a texture

	VkImage image;
	VkImageView view;
	vk_alloc_t alloc;
	VkFormat format;
	int samples;              // GL channel count: 1 luminance, 2 + alpha, 3 RGB, 4 RGBA

	rgbdata_t *original;      // TF_KEEP_SOURCE

	uint hashValue;
	struct vk_texture_s *nextHash;
} vk_texture_t;

static vk_texture_t vk_textures[VK_MAX_TEXTURES]; // slot 0 is "no texture"
static vk_texture_t *vk_texturesHashTable[TEXTURES_HASH_SIZE];
static int vk_numTextures;

static struct
{
	int defaultTexture;
	int whiteTexture;
	int blackTexture;
	int grayTexture;
	int particleTexture;
} vk_tex;

/*
=================
Samplers, shared by every texture with the same GL parameters
=================
*/
typedef struct vk_sampler_key_s
{
	byte filter;     // 0 nearest, 1 linear
	byte mipmaps;    // texture has more than one level
	byte address;    // 0 repeat, 1 clamp, 2 border
	byte anisotropy; // gl_anisotropy applies
} vk_sampler_key_t;

#define VK_MAX_SAMPLERS 32

static struct
{
	vk_sampler_key_t key;
	VkSampler sampler;
} vk_samplers[VK_MAX_SAMPLERS];
static int vk_num_samplers;

static void VK_ReleaseSampler( void *data )
{
	vkDestroySampler( vk.device, (VkSampler)data, NULL );
}

static void VK_FreeSamplers( qboolean deferred )
{
	for( int i = 0; i < vk_num_samplers; i++ )
	{
		if( deferred )
			VK_DeferRelease( VK_ReleaseSampler, (void *)vk_samplers[i].sampler );
		else
			vkDestroySampler( vk.device, vk_samplers[i].sampler, NULL );
	}

	vk_num_samplers = 0;
}

static VkSampler VK_GetSampler( vk_sampler_key_t key )
{
	static const VkSamplerAddressMode modes[] = {
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	};
	VkSampler sampler;

	for( int i = 0; i < vk_num_samplers; i++ )
	{
		if( !memcmp( &vk_samplers[i].key, &key, sizeof( key )))
			return vk_samplers[i].sampler;
	}

	const float anisotropy = bound( 1.0f, gl_texture_anisotropy.value, vk.props.limits.maxSamplerAnisotropy );

	VkSamplerCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = key.filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
		.minFilter = key.filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
		.mipmapMode = key.filter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = modes[key.address],
		.addressModeV = modes[key.address],
		.addressModeW = modes[key.address],
		.mipLodBias = key.mipmaps ? gl_texture_lodbias.value : 0.0f,
		.anisotropyEnable = key.anisotropy && anisotropy > 1.0f,
		.maxAnisotropy = anisotropy,
		.minLod = 0.0f,
		.maxLod = key.mipmaps ? VK_LOD_CLAMP_NONE : 0.0f, // GL_LINEAR / GL_NEAREST without mips
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	};

	if( vk_num_samplers >= VK_MAX_SAMPLERS || vkCreateSampler( vk.device, &info, NULL, &sampler ) != VK_SUCCESS )
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't create a sampler\n" );
		return vk_num_samplers ? vk_samplers[0].sampler : VK_NULL_HANDLE;
	}

	VK_SetObjectName( VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "sampler f%d m%d a%d x%d",
		key.filter, key.mipmaps, key.address, key.anisotropy );

	vk_samplers[vk_num_samplers].key = key;
	vk_samplers[vk_num_samplers].sampler = sampler;
	vk_num_samplers++;

	return sampler;
}

// GL_TextureFilteringEnabled
static qboolean VK_TextureFilteringEnabled( const vk_texture_t *tex )
{
	if( FBitSet( tex->flags, TF_NEAREST ))
		return false;

	if( FBitSet( tex->flags, TF_DEPTHMAP ))
		return true;

	if( FBitSet( tex->flags, TF_NOMIPMAP ) || tex->numMips <= 1 )
	{
		if( FBitSet( tex->flags, TF_ATLAS_PAGE ))
			return gl_lightmap_nearest.value == 0.0f;

		if( FBitSet( tex->flags, TF_ALLOW_NEAREST ))
			return gl_texture_nearest.value == 0.0f;

		return true;
	}

	return gl_texture_nearest.value == 0.0f;
}

// GL_ApplyTextureParams
static void VK_ApplyTextureParams( vk_texture_t *tex )
{
	vk_sampler_key_t key = { 0 };
	const qboolean nomipmap = tex->numMips <= 1 || FBitSet( tex->flags, TF_NOMIPMAP|TF_DEPTHMAP );

	key.filter = VK_TextureFilteringEnabled( tex );
	key.mipmaps = !nomipmap;
	key.anisotropy = !nomipmap && !FBitSet( tex->flags, TF_ALPHACONTRAST ) && vk.caps.anisotropy;

	if( FBitSet( tex->flags, TF_BORDER ))
		key.address = 2;
	else if( FBitSet( tex->flags, TF_CLAMP ))
		key.address = 1;

	VK_WriteTextureDescriptor( tex - vk_textures, tex->view, VK_GetSampler( key ));
}

/*
=================
VK_CheckTextureParams

the sampler cvars changed: rebuild the samplers and every descriptor
=================
*/
void VK_CheckTextureParams( void )
{
	if( !FBitSet( gl_texture_anisotropy.flags|gl_texture_lodbias.flags|gl_texture_nearest.flags|gl_lightmap_nearest.flags, FCVAR_CHANGED ))
		return;

	ClearBits( gl_texture_anisotropy.flags, FCVAR_CHANGED );
	ClearBits( gl_texture_lodbias.flags, FCVAR_CHANGED );
	ClearBits( gl_texture_nearest.flags, FCVAR_CHANGED );
	ClearBits( gl_lightmap_nearest.flags, FCVAR_CHANGED );

	VK_FreeSamplers( true );

	for( int i = 1; i < vk_numTextures; i++ )
	{
		if( vk_textures[i].used && vk_textures[i].view )
			VK_ApplyTextureParams( &vk_textures[i] );
	}
}

/*
=================
CPU image processing, ported from ref_gl so mipmaps match bit for bit
=================
*/
static void VK_BoxFilter3x3( byte *out, const byte *in, int w, int h, int x, int y )
{
	int r = 0, g = 0, b = 0;
	int acount = 0;

	for( int i = 0; i < 3; i++ )
	{
		int u = ( i - 1 ) + x;

		for( int j = 0; j < 3; j++ )
		{
			int v = ( j - 1 ) + y;

			if( u >= 0 && u < w && v >= 0 && v < h )
			{
				const byte *pixel = &in[( u + v * w ) * 4];

				if( pixel[3] != 0 )
				{
					r += pixel[0];
					g += pixel[1];
					b += pixel[2];
					acount++;
				}
			}
		}
	}

	if( acount == 0 )
		acount = 1;

	out[0] = r / acount;
	out[1] = g / acount;
	out[2] = b / acount;
}

// GL_ApplyFilter: fills transparent pixels of 1-bit alpha images with their
// neighbours' colour so filtering doesn't bleed black (no MSAA in ref_vk)
static byte *VK_ApplyFilter( byte *source, int width, int height )
{
	byte *in = source;

	if( FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
		return source;

	for( int i = 0; source && i < width * height; i++, in += 4 )
	{
		if( in[0] == 0 && in[1] == 0 && in[2] == 0 && in[3] == 0 )
			VK_BoxFilter3x3( in, source, width, height, i % width, i / width );
	}

	return source;
}

// GL_BuildMipMap: operates in place, quartering the size of the texture
static void VK_BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags )
{
	if( !in ) return;

	byte *out = in;
	int instride = srcWidth * 4;
	int mipWidth = Q_max( 1, ( srcWidth >> 1 ));
	int mipHeight = Q_max( 1, ( srcHeight >> 1 ));

	if( FBitSet( flags, TF_ALPHACONTRAST ))
	{
		memset( in, mipWidth, mipWidth * mipHeight * 4 );
		return;
	}

	for( int z = 0; z < srcDepth; z++ )
	{
		if( FBitSet( flags, TF_NORMALMAP ))
		{
			for( int y = 0; y < mipHeight; y++, in += instride * 2 )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				int row = 0;
				for( int x = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					vec3_t normal;
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( in[row+4] )
						+ MAKE_SIGNED( next[row+0] ) + MAKE_SIGNED( next[row+4] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( in[row+5] )
						+ MAKE_SIGNED( next[row+1] ) + MAKE_SIGNED( next[row+5] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( in[row+6] )
						+ MAKE_SIGNED( next[row+2] ) + MAKE_SIGNED( next[row+6] );
					}
					else
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( next[row+0] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( next[row+1] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( next[row+2] );
					}

					if( !VectorNormalizeLength( normal ))
						VectorSet( normal, 0.5f, 0.5f, 1.0f );

					out[0] = (byte)( 128 + (int)( 127.0f * normal[0] ));
					out[1] = (byte)( 128 + (int)( 127.0f * normal[1] ));
					out[2] = (byte)( 128 + (int)( 127.0f * normal[2] ));
					out[3] = 255;
				}
			}
		}
		else
		{
			for( int y = 0; y < mipHeight; y++, in += instride * 2 )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				int row = 0;
				for( int x = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						out[0] = (in[row+0] + in[row+4] + next[row+0] + next[row+4]) >> 2;
						out[1] = (in[row+1] + in[row+5] + next[row+1] + next[row+5]) >> 2;
						out[2] = (in[row+2] + in[row+6] + next[row+2] + next[row+6]) >> 2;
						out[3] = (in[row+3] + in[row+7] + next[row+3] + next[row+7]) >> 2;
					}
					else
					{
						out[0] = (in[row+0] + next[row+0]) >> 1;
						out[1] = (in[row+1] + next[row+1]) >> 1;
						out[2] = (in[row+2] + next[row+2]) >> 1;
						out[3] = (in[row+3] + next[row+3]) >> 1;
					}
				}
			}
		}
	}
}

/*
=================
VK_ToRGBA

expands the uncompressed formats GL accepted as upload types to RGBA8,
returns the source itself when it already is
=================
*/
static byte *VK_ToRGBA( const byte *src, pixformat_t type, int pixels )
{
	byte *out, *dst;

	if( type == PF_RGBA_32 || !src )
		return (byte *)src;

	out = dst = Mem_Malloc( r_temppool, pixels * 4 );

	for( int i = 0; i < pixels; i++, dst += 4 )
	{
		switch( type )
		{
		case PF_BGRA_32:
			dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3]; src += 4;
			break;
		case PF_RGB_24:
			dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; src += 3;
			break;
		case PF_BGR_24:
			dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255; src += 3;
			break;
		case PF_LUMINANCE:
			dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255; src += 1;
			break;
		default:
			Mem_Free( out );
			return NULL;
		}
	}

	return out;
}

static VkFormat VK_CompressedFormat( pixformat_t type )
{
	switch( type )
	{
	case PF_DXT1: return VK_FORMAT_BC1_RGB_UNORM_BLOCK; // GL never used DXT1 with 1-bit alpha
	case PF_DXT3: return VK_FORMAT_BC2_UNORM_BLOCK;
	case PF_DXT5: return VK_FORMAT_BC3_UNORM_BLOCK;
	case PF_BC6H_SIGNED: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
	case PF_BC6H_UNSIGNED: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
	case PF_BC7_SRGB:
	case PF_BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK; // ref_gl uploads both as UNORM
	case PF_ATI2: return VK_FORMAT_BC5_UNORM_BLOCK;
	default: return VK_FORMAT_UNDEFINED;
	}
}

// GL_CalcTextureSamples
static int VK_CalcTextureSamples( int flags )
{
	if( FBitSet( flags, IMAGE_HAS_COLOR ))
		return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 4 : 3;
	return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 2 : 1;
}

/*
=================
Texture table
=================
*/
static qboolean VK_CheckTexName( const char *name )
{
	if( COM_StringEmptyOrNULL( name ))
		return false;

	if( Q_strlen( name ) >= sizeof( vk_textures->name ))
	{
		gEngfuncs.Con_Printf( S_ERROR "LoadTexture: too long name %s (%d)\n", name, (int)Q_strlen( name ));
		return false;
	}

	return true;
}

static vk_texture_t *VK_TextureForName( const char *name )
{
	uint hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( vk_texture_t *tex = vk_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
			return tex;
	}

	return NULL;
}

// GL_AllocTexture: GL hands out sequential names, so the first free slot
// gives the same numbers for the same load sequence
static vk_texture_t *VK_AllocTexture( const char *name, texFlags_t flags )
{
	vk_texture_t *tex = NULL;

	for( int i = 1; i < VK_MAX_TEXTURES; i++ )
	{
		if( !vk_textures[i].used )
		{
			tex = &vk_textures[i];
			break;
		}
	}

	if( !tex )
	{
		gEngfuncs.Host_Error( "%s: MAX_TEXTURES limit exceeds\n", __func__ );
		return NULL;
	}

	memset( tex, 0, sizeof( *tex ));
	Q_strncpy( tex->name, name, sizeof( tex->name ));
	tex->flags = flags;
	tex->used = true;
	tex->xscale = tex->yscale = 1.0f;

	vk_numTextures = Q_max(( tex - vk_textures ) + 1, vk_numTextures );

	tex->hashValue = COM_HashKey( name, TEXTURES_HASH_SIZE );
	tex->nextHash = vk_texturesHashTable[tex->hashValue];
	vk_texturesHashTable[tex->hashValue] = tex;

	return tex;
}

typedef struct vk_image_release_s
{
	VkImage image;
	VkImageView view;
	vk_alloc_t alloc;
} vk_image_release_t;

static void VK_ReleaseImage( void *data )
{
	vk_image_release_t *r = data;

	if( r->view )
		vkDestroyImageView( vk.device, r->view, NULL );
	if( r->image )
		vkDestroyImage( vk.device, r->image, NULL );
	VK_Free( &r->alloc );
	Mem_Free( r );
}

// the frames in flight may still sample it
static void VK_ReleaseTextureImage( vk_texture_t *tex )
{
	vk_image_release_t *r;

	if( !tex->image )
		return;

	r = Mem_Calloc( r_temppool, sizeof( *r ));
	r->image = tex->image;
	r->view = tex->view;
	r->alloc = tex->alloc;
	VK_DeferRelease( VK_ReleaseImage, r );

	tex->image = VK_NULL_HANDLE;
	tex->view = VK_NULL_HANDLE;
	memset( &tex->alloc, 0, sizeof( tex->alloc ));
	tex->size = 0;
}

static void VK_DeleteTexture( vk_texture_t *tex )
{
	vk_texture_t **prev;

	if( !tex->used )
		return;

	prev = &vk_texturesHashTable[tex->hashValue];
	while( *prev )
	{
		if( *prev == tex )
		{
			*prev = tex->nextHash;
			break;
		}
		prev = &( *prev )->nextHash;
	}

	VK_ReleaseTextureImage( tex );

	if( tex->original )
		gEngfuncs.FS_FreeImage( tex->original );

	memset( tex, 0, sizeof( *tex ));
}

/*
=================
GPU image
=================
*/
static qboolean VK_CreateTextureImage( vk_texture_t *tex, VkFormat format, int layers, qboolean cubemap )
{
	VkMemoryRequirements req;
	VkComponentMapping swizzle = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };

	VkImageCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { tex->width, tex->height, 1 },
		.mipLevels = tex->numMips,
		.arrayLayers = layers,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	if( vkCreateImage( vk.device, &info, NULL, &tex->image ) != VK_SUCCESS )
		return false;

	vkGetImageMemoryRequirements( vk.device, tex->image, &req );

	if( !VK_Alloc( &req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &tex->alloc ))
	{
		vkDestroyImage( vk.device, tex->image, NULL );
		tex->image = VK_NULL_HANDLE;
		return false;
	}

	vkBindImageMemory( vk.device, tex->image, tex->alloc.memory, tex->alloc.offset );

	// the GL internal format decides which channels the shaders see
	if( format == VK_FORMAT_R8G8B8A8_UNORM )
	{
		switch( tex->samples )
		{
		case 1: // GL_LUMINANCE8, or GL_INTENSITY8 with TF_ALPHACONTRAST
			swizzle.r = swizzle.g = swizzle.b = VK_COMPONENT_SWIZZLE_R;
			swizzle.a = FBitSet( tex->flags, TF_ALPHACONTRAST ) ? VK_COMPONENT_SWIZZLE_R : VK_COMPONENT_SWIZZLE_ONE;
			break;
		case 2: // GL_LUMINANCE8_ALPHA8
			swizzle.r = swizzle.g = swizzle.b = VK_COMPONENT_SWIZZLE_R;
			break;
		case 3: // GL_RGB8
			swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
			break;
		}
	}

	VkImageViewCreateInfo view = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = tex->image,
		.viewType = cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : ( layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D ),
		.format = format,
		.components = swizzle,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, tex->numMips, 0, layers },
	};

	if( vkCreateImageView( vk.device, &view, NULL, &tex->view ) != VK_SUCCESS )
	{
		vkDestroyImage( vk.device, tex->image, NULL );
		VK_Free( &tex->alloc );
		tex->image = VK_NULL_HANDLE;
		return false;
	}

	tex->format = format;
	VK_SetObjectName( VK_OBJECT_TYPE_IMAGE, (uint64_t)tex->image, "texture %d %s", (int)( tex - vk_textures ), tex->name );
	VK_SetObjectName( VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)tex->view, "texture %d view", (int)( tex - vk_textures ));

	return true;
}

// copies one level of one layer from CPU memory
static void VK_UploadLevel( vk_texture_t *tex, int level, int layer, int width, int height, const byte *data, size_t size )
{
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;
	byte *dst = VK_UploadStaging( size, &staging, &offset, &cmd );

	if( !dst )
		return;

	memcpy( dst, data, size );

	VkBufferImageCopy copy = {
		.bufferOffset = offset,
		.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1 },
		.imageExtent = { width, height, 1 },
	};

	vkCmdCopyBufferToImage( cmd, staging, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
}

// the copies go to the upload command buffer, wrapped in these layout changes
static void VK_TextureToTransfer( vk_texture_t *tex, qboolean discard )
{
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;

	VK_UploadStaging( 0, &staging, &offset, &cmd );
	VK_ImageBarrierRaw( cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
		discard ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
}

static void VK_TextureToShader( vk_texture_t *tex )
{
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;

	VK_UploadStaging( 0, &staging, &offset, &cmd );
	VK_ImageBarrierRaw( cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
}

static void VK_ClearTextureImage( vk_texture_t *tex )
{
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;
	VkClearColorValue zero = { 0 };
	VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

	VK_UploadStaging( 0, &staging, &offset, &cmd );
	vkCmdClearColorImage( cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range );
}

// GL_SetTextureDimensions: NPOT is always there, only the device limit applies
static void VK_SetTextureDimensions( vk_texture_t *tex, int width, int height )
{
	const int maxsize = vk.props.limits.maxImageDimension2D;

	tex->srcWidth = width;
	tex->srcHeight = height;

	while( width > maxsize || height > maxsize )
	{
		width >>= 1;
		height >>= 1;
	}

	tex->width = Q_max( 1, width );
	tex->height = Q_max( 1, height );
}

// GL_CalcMipmapCount
static int VK_CalcMipmapCount( vk_texture_t *tex, qboolean haveBuffer )
{
	int mipcount;

	if( !haveBuffer || FBitSet( tex->flags, TF_NOMIPMAP ))
		return 1;

	for( mipcount = 0; mipcount < 16; mipcount++ )
	{
		int width = Q_max( 1, ( tex->width >> mipcount ));
		int height = Q_max( 1, ( tex->height >> mipcount ));
		if( width == 1 && height == 1 )
			break;
	}

	return mipcount + 1;
}

/*
=================
VK_UploadTexture

GL_UploadTexture
=================
*/
static qboolean VK_UploadTexture( vk_texture_t *tex, rgbdata_t *pic )
{
	const qboolean cubemap = FBitSet( pic->flags, IMAGE_CUBEMAP ) ? true : false;
	const int numSides = cubemap ? 6 : 1;
	int layers = numSides;
	VkFormat format;
	byte *buf = pic->buffer;
	int mipCount;

	if( FBitSet( tex->flags, TF_DEPTHMAP|TF_ARB_FLOAT|TF_ARB_16BIT|TF_MULTISAMPLE ))
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: %s uses a format ref_vk doesn't support yet\n", __func__, tex->name );
		return false;
	}

	pic->depth = Q_max( 1, pic->depth );
	pic->numMips = Q_max( 1, pic->numMips );

	if( FBitSet( pic->flags, IMAGE_MULTILAYER ) || ( pic->width > 1 && pic->height > 1 && pic->depth > 1 ))
		layers = pic->depth; // 2D arrays; 3D textures are sampled as layers too

	VK_SetTextureDimensions( tex, pic->width, pic->height );
	tex->samples = VK_CalcTextureSamples( pic->flags );
	tex->depth = layers;

	if( ImageCompressed( pic->type ))
	{
		format = VK_CompressedFormat( pic->type );

		if( format == VK_FORMAT_UNDEFINED || !vk.caps.texture_compression_bc )
		{
			gEngfuncs.Con_DPrintf( S_ERROR "%s: compressed format of %s is not supported\n", __func__, tex->name );
			return false;
		}

		mipCount = pic->numMips;
	}
	else
	{
		format = VK_FORMAT_R8G8B8A8_UNORM;
		mipCount = Q_max( 1, pic->numMips ) > 1 ? pic->numMips : VK_CalcMipmapCount( tex, buf != NULL );
	}

	// a new image every time: full uploads of an existing texture are rare
	// (TF_UPDATE, GL_ProcessTexture), GL_UpdateTexture has its own path
	VK_ReleaseTextureImage( tex );

	tex->numMips = mipCount;

	if( !VK_CreateTextureImage( tex, format, layers, cubemap ))
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: can't create the image for %s\n", __func__, tex->name );
		return false;
	}

	VK_TextureToTransfer( tex, true );

	if( !buf )
		VK_ClearTextureImage( tex ); // render targets: GL leaves them undefined, zero is deterministic
	else if( ImageCompressed( pic->type ) || pic->numMips > 1 )
	{
		// precomputed levels (DDS), stored side by side
		for( int i = 0; i < layers; i++ )
		{
			for( int j = 0; j < mipCount; j++ )
			{
				int width = Q_max( 1, ( tex->width >> j ));
				int height = Q_max( 1, ( tex->height >> j ));
				size_t size = gEngfuncs.Image_CalcImageSize( pic->type, width, height, 1 );
				byte *rgba = ImageCompressed( pic->type ) ? buf : VK_ToRGBA( buf, pic->type, width * height );

				if( rgba )
					VK_UploadLevel( tex, j, i, width, height, rgba, ImageCompressed( pic->type ) ? size : (size_t)width * height * 4 );
				if( rgba && rgba != buf )
					Mem_Free( rgba );

				tex->size += size;
				buf += size;
			}
		}
	}
	else
	{
		const int sideSize = gEngfuncs.Image_CalcImageSize( pic->type, pic->width, pic->height, 1 );

		for( int i = 0; i < layers; i++ )
		{
			byte *rgba = VK_ToRGBA( buf, pic->type, pic->width * pic->height );
			byte *data;

			if( !rgba )
			{
				gEngfuncs.Con_Printf( S_ERROR "%s: unsupported pixel format %d in %s\n", __func__, pic->type, tex->name );
				break;
			}

			// only single uncompressed textures are resampled, like ref_gl
			if( pic->width != tex->width || pic->height != tex->height )
				data = GL_ResampleTexture( rgba, pic->width, pic->height, tex->width, tex->height, FBitSet( tex->flags, TF_NORMALMAP ));
			else data = rgba;

			if( !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ))
				data = VK_ApplyFilter( data, tex->width, tex->height );

			for( int j = 0; j < mipCount; j++ )
			{
				int width = Q_max( 1, ( tex->width >> j ));
				int height = Q_max( 1, ( tex->height >> j ));

				VK_UploadLevel( tex, j, i, width, height, data, (size_t)width * height * 4 );
				if( mipCount > 1 )
					VK_BuildMipMap( data, width, height, 1, tex->flags );
				tex->size += (size_t)width * height * 4;
			}

			if( rgba != buf )
				Mem_Free( rgba );

			buf += sideSize;
		}
	}

	VK_TextureToShader( tex );
	SetBits( tex->flags, TF_IMG_UPLOADED );

	return true;
}

// GL_ProcessImage
static void VK_ProcessImage( vk_texture_t *tex, rgbdata_t *pic )
{
	uint img_flags = 0;

	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;
	if( FBitSet( pic->flags, IMAGE_PREMULTIPLIED ))
		SetBits( tex->flags, TF_PREMULTIPLIED );

	tex->encode = pic->encode;

	if( ImageCompressed( pic->type ))
	{
		if( !pic->numMips )
			tex->flags |= TF_NOMIPMAP;

		tex->flags &= ~TF_KEEP_SOURCE;
		return;
	}

	if( pic->flags & IMAGE_HAS_LUMA )
		tex->flags |= TF_HAS_LUMA;

	if( pic->flags & IMAGE_QUAKEPAL )
		tex->flags |= TF_QUAKEPAL;

	if( tex->flags & TF_MAKELUMA )
	{
		img_flags |= IMAGE_MAKE_LUMA;
		tex->flags &= ~TF_MAKELUMA;
	}

	if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
		tex->original = gEngfuncs.FS_CopyImage( pic );

	if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
		img_flags |= IMAGE_FORCE_RGBA;

	if( pic->buffer )
		gEngfuncs.Image_Process( &pic, 0, 0, img_flags, 0 );

	if( FBitSet( pic->flags, IMAGE_PLAYERDECAL ) && pic->buffer && pic->type == PF_RGBA_32 && FBitSet( pic->flags, IMAGE_HAS_ALPHA )
		&& !FBitSet( pic->flags, IMAGE_PREMULTIPLIED ))
	{
		int cnt = (int)( pic->width * pic->height );
		byte *p = pic->buffer;

		for( int i = 0; i < cnt; i++, p += 4 )
		{
			int a = p[3];
			p[0] = ( p[0] * a + 127 ) / 255;
			p[1] = ( p[1] * a + 127 ) / 255;
			p[2] = ( p[2] * a + 127 ) / 255;
		}
		SetBits( pic->flags, IMAGE_PREMULTIPLIED );
		SetBits( tex->flags, TF_PREMULTIPLIED );
	}

	if( FBitSet( tex->flags, TF_LUMINANCE ))
		ClearBits( pic->flags, IMAGE_HAS_COLOR );
}

/*
=================
Interface entry points, same semantics as ref_gl
=================
*/
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	uint picFlags = 0;
	vk_texture_t *tex;
	rgbdata_t *pic;

	if( !VK_CheckTexName( name ))
		return 0;

	if(( tex = VK_TextureForName( name )))
		return tex - vk_textures;

	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );

	gEngfuncs.Image_SetForceFlags( picFlags );

	pic = gEngfuncs.FS_LoadImage( name, buf, size );
	if( !pic )
		return 0;

	tex = VK_AllocTexture( name, flags );
	VK_ProcessImage( tex, pic );

	if( !VK_UploadTexture( tex, pic ))
	{
		VK_DeleteTexture( tex );
		gEngfuncs.FS_FreeImage( pic );
		return 0;
	}

	VK_ApplyTextureParams( tex );
	gEngfuncs.FS_FreeImage( pic );

	return tex - vk_textures;
}

int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	vk_texture_t *tex;

	if( !VK_CheckTexName( name ))
		return 0;

	if(( tex = VK_TextureForName( name )) && !update )
		return tex - vk_textures;

	if( !pic )
		return 0;

	if( update )
	{
		if( tex == NULL )
			gEngfuncs.Host_Error( "%s: couldn't find texture %s for update\n", __func__, name );
		SetBits( tex->flags, flags );
	}
	else
	{
		tex = VK_AllocTexture( name, flags );
	}

	VK_ProcessImage( tex, pic );

	if( !VK_UploadTexture( tex, pic ))
	{
		VK_DeleteTexture( tex );
		return 0;
	}

	VK_ApplyTextureParams( tex );
	return tex - vk_textures;
}

int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	qboolean update = FBitSet( flags, TF_UPDATE ) ? true : false;
	rgbdata_t r_empty;
	int texnum;

	ClearBits( flags, TF_UPDATE );

	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = width;
	r_empty.height = height;
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * 4;
	r_empty.buffer = (byte *)buffer;

	ClearBits( flags, TF_TEXTURE_3D );

	if( !FBitSet( flags, TF_LUMINANCE ) && !FBitSet( flags, TF_ALPHACONTRAST ))
		SetBits( r_empty.flags, IMAGE_HAS_COLOR );

	if( FBitSet( flags, TF_HAS_ALPHA ))
		SetBits( r_empty.flags, IMAGE_HAS_ALPHA );

	if( FBitSet( flags, TF_CUBEMAP ))
	{
		SetBits( r_empty.flags, IMAGE_CUBEMAP );
		r_empty.size *= 6;
	}

	texnum = GL_LoadTextureFromBuffer( name, &r_empty, flags, update );

	if( !Q_strcmp( name, REF_DEFAULT_TEXTURE ))
		vk_tex.defaultTexture = texnum;
	else if( !Q_strcmp( name, REF_PARTICLE_TEXTURE ))
		vk_tex.particleTexture = texnum;
	else if( !Q_strcmp( name, REF_WHITE_TEXTURE ))
		vk_tex.whiteTexture = texnum;
	else if( !Q_strcmp( name, REF_GRAY_TEXTURE ))
		vk_tex.grayTexture = texnum;
	else if( !Q_strcmp( name, REF_BLACK_TEXTURE ))
		vk_tex.blackTexture = texnum;

	return texnum;
}

int GL_FindTexture( const char *name )
{
	vk_texture_t *tex;

	if( !VK_CheckTexName( name ))
		return 0;

	if(( tex = VK_TextureForName( name )))
		return tex - vk_textures;

	return 0;
}

const char *GL_TextureName( unsigned int texnum )
{
	if( texnum >= VK_MAX_TEXTURES )
		return "";

	return vk_textures[texnum].name;
}

const byte *GL_TextureData( unsigned int texnum )
{
	if( texnum >= VK_MAX_TEXTURES || !vk_textures[texnum].original )
		return NULL;

	return vk_textures[texnum].original->buffer;
}

void GL_FreeTexture( unsigned int texnum )
{
	if( texnum == 0 || texnum >= VK_MAX_TEXTURES )
		return;

	VK_DeleteTexture( &vk_textures[texnum] );
}

void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	vk_texture_t *image;
	rgbdata_t *pic;
	int flags;

	if( texnum <= 0 || texnum >= VK_MAX_TEXTURES )
		return;

	image = &vk_textures[texnum];

	if( gamma != -1.0f )
		flags = IMAGE_LIGHTGAMMA;
	else if( topColor != -1 && bottomColor != -1 )
		flags = IMAGE_REMAP;
	else
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: bad operation for %s\n", __func__, image->name );
		return;
	}

	if( !image->original )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: no input data for %s\n", __func__, image->name );
		return;
	}

	if( ImageCompressed( image->original->type ))
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: can't process compressed texture %s\n", __func__, image->name );
		return;
	}

	pic = gEngfuncs.FS_CopyImage( image->original );

	if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
		flags |= IMAGE_FORCE_RGBA;

	gEngfuncs.Image_Process( &pic, topColor, bottomColor, flags, 0.0f );

	if( VK_UploadTexture( image, pic ))
		VK_ApplyTextureParams( image );

	gEngfuncs.FS_FreeImage( pic );
}

/*
=================
GL_UpdateTexture

replaces the first level, resizing the texture when needed (VGUI, video)
=================
*/
void GL_UpdateTexture( int texnum, int cols, int rows, int width, int height, const byte *buffer, pixformat_t fmt )
{
	vk_texture_t *tex;
	byte *rgba, *raw;

	if( texnum <= 0 || texnum >= VK_MAX_TEXTURES || !vk_textures[texnum].used )
		return;

	tex = &vk_textures[texnum];
	rgba = VK_ToRGBA( buffer, fmt, cols * rows );

	if( !rgba )
	{
		gEngfuncs.Con_DPrintf( S_ERROR "%s: unsupported pixel format %i\n", __func__, fmt );
		return;
	}

	if( cols != width || rows != height )
	{
		raw = GL_ResampleTexture( rgba, cols, rows, width, height, false );
		cols = width;
		rows = height;
	}
	else raw = rgba;

	if( !tex->image || cols != tex->width || rows != tex->height || tex->format != VK_FORMAT_R8G8B8A8_UNORM )
	{
		// glTexImage2D on level 0: the texture becomes a single level one
		VK_ReleaseTextureImage( tex );
		tex->width = cols;
		tex->height = rows;
		tex->depth = 1;
		tex->numMips = 1;

		if( !VK_CreateTextureImage( tex, VK_FORMAT_R8G8B8A8_UNORM, 1, false ))
		{
			if( rgba != buffer )
				Mem_Free( rgba );
			return;
		}

		VK_TextureToTransfer( tex, true );
	}
	else VK_TextureToTransfer( tex, false );

	VK_UploadLevel( tex, 0, 0, cols, rows, raw, (size_t)cols * rows * 4 );
	VK_TextureToShader( tex );
	VK_ApplyTextureParams( tex );

	if( rgba != buffer )
		Mem_Free( rgba );
}

void R_OverrideTextureSourceSize( unsigned int texnum, unsigned int srcWidth, unsigned int srcHeight )
{
	if( texnum >= VK_MAX_TEXTURES )
		return;

	vk_textures[texnum].srcWidth = srcWidth;
	vk_textures[texnum].srcHeight = srcHeight;
}

const byte *R_GetTextureOriginalBuffer( unsigned int texnum )
{
	return GL_TextureData( texnum );
}

void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	const vk_texture_t *tex = &vk_textures[bound( 0, texture, VK_MAX_TEXTURES - 1 )];

	if( xScale ) *xScale = tex->xscale;
	if( yScale ) *yScale = tex->yscale;
}

void R_SetDetailScaleForTexture( int texture, float xScale, float yScale )
{
	vk_texture_t *tex = &vk_textures[bound( 0, texture, VK_MAX_TEXTURES - 1 )];

	tex->xscale = xScale;
	tex->yscale = yScale;
}

/*
=================
VK_TextureForDraw

the descriptor index to sample, GL_Bind falls back to the default texture
=================
*/
int VK_TextureForDraw( int texnum )
{
	if( texnum <= 0 || texnum >= VK_MAX_TEXTURES || !vk_textures[texnum].view )
		texnum = vk_tex.defaultTexture;

	if( texnum <= 0 || !vk_textures[texnum].view )
		return VK_IMM_TEXTURE_NONE;

	return texnum;
}

/*
=================
VK_TextureParm

texture queries of RefGetParm
=================
*/
intptr_t VK_TextureParm( int parm, int texnum )
{
	const vk_texture_t *tex = &vk_textures[bound( 0, texnum, VK_MAX_TEXTURES - 1 )];

	switch( parm )
	{
	case PARM_TEX_WIDTH: return tex->width;
	case PARM_TEX_HEIGHT: return tex->height;
	case PARM_TEX_SRC_WIDTH: return tex->srcWidth;
	case PARM_TEX_SRC_HEIGHT: return tex->srcHeight;
	case PARM_TEX_ENCODE: return tex->encode;
	case PARM_TEX_MIPCOUNT: return tex->numMips;
	case PARM_TEX_DEPTH: return tex->depth;
	case PARM_TEX_TEXNUM: return tex->used ? texnum : 0;
	case PARM_TEX_FLAGS: return tex->flags;
	case PARM_TEX_FILTERING:
		if( texnum < 0 )
			return gl_texture_nearest.value == 0.0f;
		return VK_TextureFilteringEnabled( tex );
	case PARM_TEX_MEMORY:
	{
		size_t total = 0;
		for( int i = 0; i < vk_numTextures; i++ )
			total += vk_textures[i].size;
		return (intptr_t)total;
	}
	default:
		return 0;
	}
}

/*
=================
texturelist
=================
*/
static void VK_TextureList_f( void )
{
	size_t total = 0;
	int count = 0;

	gEngfuncs.Con_Printf( "      -w-- -h-- -layers- -mips -size- -format- -name--------\n" );

	for( int i = 1; i < vk_numTextures; i++ )
	{
		const vk_texture_t *tex = &vk_textures[i];

		if( !tex->used )
			continue;

		gEngfuncs.Con_Printf( "%4i: %4i %4i %8i %5i %5iK %8d %s\n", i, tex->width, tex->height,
			tex->depth, tex->numMips, (int)( tex->size >> 10 ), tex->format, tex->name );

		total += tex->size;
		count++;
	}

	gEngfuncs.Con_Printf( "%i textures, %.1f MB\n", count, total / 1048576.0 );
}

void VK_InitTextures( void )
{
	memset( vk_textures, 0, sizeof( vk_textures ));
	memset( vk_texturesHashTable, 0, sizeof( vk_texturesHashTable ));
	memset( &vk_tex, 0, sizeof( vk_tex ));
	vk_numTextures = 0;
	vk_num_samplers = 0;

	gEngfuncs.Cvar_RegisterVariable( &gl_texture_anisotropy );
	gEngfuncs.Cvar_RegisterVariable( &gl_texture_lodbias );
	gEngfuncs.Cvar_RegisterVariable( &gl_texture_nearest );
	gEngfuncs.Cvar_RegisterVariable( &gl_lightmap_nearest );

	gEngfuncs.Cmd_AddCommand( "texturelist", VK_TextureList_f, "display loaded textures list" );
}

void VK_ShutdownTextures( void )
{
	gEngfuncs.Cmd_RemoveCommand( "texturelist" );

	if( !vk.device )
		return;

	VK_FlushUploads();
	vkDeviceWaitIdle( vk.device );

	for( int i = 0; i < vk_numTextures; i++ )
		VK_DeleteTexture( &vk_textures[i] );

	VK_RunDeferred( UINT64_MAX );
	VK_FreeSamplers( false );
	vk_numTextures = 0;
}
