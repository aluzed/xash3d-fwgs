/*
vk_stream.c - mip level streaming of the KTX2 material maps
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
A 4K material map is 16 MB of BC7 or BC5 with its mip levels, and a map
has dozens of them: they can't all stay on the GPU, nor be read at load
time. The KTX2 maps of materials are read by this file instead of the
engine image loader:

- at load, only the levels up to VK_STREAM_BASE_SIZE are read, so the
  texture shows at once, blurred;
- every frame, the draws say how many screen pixels a game texel covers
  where the material is seen (VK_StreamNoteUse), which gives the level
  each map needs;
- the missing levels are read by a worker thread (Job_Submit), only their
  bytes, straight into a staging buffer kept for the next reads; a new
  image takes the levels already there (a GPU copy) then the new ones,
  at r_vk_texture_upload megabytes a second shared out between frames,
  and replaces the old one once complete: no frame waits for a whole 4K
  level to cross the bus;
- under r_vk_texture_budget megabytes: past it, the maps unseen for a
  while go back to their small levels before others grow, and when none
  is, a map grows less than it would like: the maps on screen never take
  turns.

A map changes images by switching between two texture slots: a frame in
flight samples the slot it was recorded with, the other one gets the new
image, and the materials point at it (their buffer is updated before the
next frame). A slot is only reused once the frames that may sample it are
done.
*/

#include "vk_local.h"

CVAR_DEFINE_AUTO( r_vk_texture_budget, "1024", FCVAR_GLCONFIG, "megabytes of GPU memory for streamed material maps" );
CVAR_DEFINE_AUTO( r_vk_texture_stream, "1", FCVAR_GLCONFIG, "stream the mip levels of KTX2 material maps, 0 loads them whole" );
CVAR_DEFINE_AUTO( r_vk_texture_upload, "512", FCVAR_GLCONFIG, "megabytes a second of streamed material maps sent to the GPU" );

#define VK_STREAM_MAX         1024
#define VK_STREAM_MAX_LEVELS  16
#define VK_STREAM_BASE_SIZE   256  // levels up to this size are loaded at once and stay
#define VK_STREAM_MAX_LOADS   4    // reads in flight
#define VK_STREAM_UNUSED      3.0  // seconds without a draw before a map wants no more than its base
#define VK_STREAM_IDLE        2.0  // seconds without a draw before a map may shrink for another
#define VK_STREAM_NEED_DECAY  1.0  // seconds for the need of a map to halve once it's no longer seen
#define VK_STREAM_STAGING     ( 24u << 20 ) // a 4K BC7 level 0 and 1 fit
#define KTX2_HEADER_SIZE      80

typedef struct
{
	char path[MAX_QPATH];
	int refs;            // materials using it, 0 for a free entry
	int generation;      // bumped when the entry is reused, jobs check it

	VkFormat format;
	int width, height, levels;
	uint32_t block_bytes;
	uint64_t offset[VK_STREAM_MAX_LEVELS];
	uint64_t size[VK_STREAM_MAX_LEVELS];
	int base;            // the levels from it are always there

	int slots[2];        // texture numbers, the active one is sampled
	int active;
	uint64_t swap_frame; // when the active slot last changed
	int resident;        // first level of the active image
	size_t bytes;        // of the active image

	float need;          // screen pixels per game texel, the most seen this frame
	float need_hold;     // the same, held and slowly decaying: a map out of sight a moment keeps its level
	int game_width;      // width of the game texture the need is for
	double last_used;    // time of the last draw
	qboolean loading;
} vk_stream_t;

// host visible buffers the reads land in, one per read in flight
typedef struct
{
	vk_buffer_t buffer;
	qboolean used;
	uint64_t busy_until; // the upload reading it is done from this frame
} vk_stream_staging_t;

typedef struct vk_stream_job_s
{
	int stream, generation;
	int first, last;     // levels read: first .. last - 1
	uint64_t offset[VK_STREAM_MAX_LEVELS]; // copies: the entry may be reused meanwhile
	uint64_t size[VK_STREAM_MAX_LEVELS];
	file_t *file;
	vk_stream_staging_t *staging;
	byte *data;          // its mapped memory
	qboolean failed;
	int slot;            // being filled, -1 before the image exists
	int level, row;      // next rows of blocks to send
	VkDeviceSize sent;   // bytes of the staging buffer sent so far
	size_t growth;       // reserved in the budget until the image replaces the old one
	struct vk_stream_job_s *next; // read, waiting for its image
} vk_stream_job_t;

static struct
{
	vk_stream_t list[VK_STREAM_MAX];
	int count;
	int loads;           // jobs in flight, read or not
	size_t total;        // bytes of the active images
	size_t reserved;     // what the loads in flight will add
	vk_stream_staging_t staging[VK_STREAM_MAX_LOADS];
	vk_stream_job_t *ready, *ready_tail;
	double last_upload;  // time of the last frame's uploads
} vk_str;

void VK_RegisterStreamCvars( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_vk_texture_budget );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_texture_stream );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_texture_upload );
}

static size_t VK_StreamLevelsSize( const vk_stream_t *st, int first )
{
	size_t size = 0;

	for( int i = first; i < st->levels; i++ )
		size += st->size[i];

	return size;
}

/*
=================
KTX2
=================
*/
static VkFormat VK_StreamFormat( uint32_t vk_format, uint32_t *block_bytes )
{
	switch( vk_format )
	{
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC4_UNORM_BLOCK:
	case VK_FORMAT_BC4_SNORM_BLOCK:
		*block_bytes = 8;
		return vk_format;
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		*block_bytes = 8;
		return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		*block_bytes = 8;
		return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC5_UNORM_BLOCK:
	case VK_FORMAT_BC5_SNORM_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
		*block_bytes = 16;
		return vk_format;
	// the game's colours are gamma space values, sampled as they are like ref_gl does
	case VK_FORMAT_BC3_SRGB_BLOCK:
		*block_bytes = 16;
		return VK_FORMAT_BC3_UNORM_BLOCK;
	case VK_FORMAT_BC7_SRGB_BLOCK:
		*block_bytes = 16;
		return VK_FORMAT_BC7_UNORM_BLOCK;
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

static uint32_t Read32( const byte *p ) { uint32_t v; memcpy( &v, p, 4 ); return LittleLong( v ); }
static uint64_t Read64( const byte *p ) { return (uint64_t)Read32( p ) | ((uint64_t)Read32( p + 4 ) << 32 ); }

static qboolean VK_StreamParseHeader( vk_stream_t *st, file_t *f )
{
	static const byte identifier[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };
	byte header[KTX2_HEADER_SIZE], index[VK_STREAM_MAX_LEVELS * 24];

	if( gEngfuncs.fsapi->Read( f, header, sizeof( header )) != sizeof( header ) || memcmp( header, identifier, 12 ))
		return false;

	st->format = VK_StreamFormat( Read32( header + 12 ), &st->block_bytes );
	st->width = Read32( header + 20 );
	st->height = Read32( header + 24 );
	st->levels = Read32( header + 40 );

	if( st->format == VK_FORMAT_UNDEFINED || Read32( header + 28 ) > 1 || Read32( header + 32 ) > 1 || Read32( header + 36 ) != 1
		|| Read32( header + 44 ) != 0 || st->levels < 1 || st->levels > VK_STREAM_MAX_LEVELS || st->width <= 0 || st->height <= 0 )
		return false;

	if( gEngfuncs.fsapi->Read( f, index, st->levels * 24 ) != st->levels * 24 )
		return false;

	for( int i = 0; i < st->levels; i++ )
	{
		const uint32_t w = Q_max( st->width >> i, 1 ), h = Q_max( st->height >> i, 1 );

		st->offset[i] = Read64( index + i * 24 );
		st->size[i] = Read64( index + i * 24 + 8 );

		if( st->size[i] != (uint64_t)(( w + 3 ) / 4 ) * (( h + 3 ) / 4 ) * st->block_bytes )
			return false;
	}

	return true;
}

// levels first .. last - 1, back to back, from an open file
static qboolean VK_StreamReadLevels( const uint64_t *offset, const uint64_t *size, file_t *f, int first, int last, byte *out )
{
	for( int i = first; i < last; i++ )
	{
		if( gEngfuncs.fsapi->Seek( f, offset[i], SEEK_SET ) != 0
			|| gEngfuncs.fsapi->Read( f, out, size[i] ) != (fs_offset_t)size[i] )
			return false;

		out += size[i];
	}

	return true;
}

/*
=================
Images
=================
*/
// the slots switch: the materials follow
static void VK_StreamSetActive( vk_stream_t *st, int slot, int resident, size_t bytes )
{
	// the slot the frames in flight sampled: its image goes once they are done
	if( slot != st->active )
		VK_StreamSlotRelease( st->slots[st->active] );

	vk_str.total += bytes - st->bytes;
	st->active = slot;
	st->resident = resident;
	st->bytes = bytes;
	st->swap_frame = VK_CurrentFrame();
	VK_MaterialsChanged();
}

/*
=================
VK_StreamBeginImage

a new image for levels first .. levels - 1 in the slot that isn't sampled,
the levels from keep copied from the active image on the GPU: the caller
sends first .. keep - 1, then VK_StreamFinishImage makes it the active one
=================
*/
static int VK_StreamBeginImage( vk_stream_t *st, int first, int keep )
{
	const int slot = st->slots[st->active] ? !st->active : st->active;
	const int old = st->slots[st->active];
	char name[MAX_QPATH + 4];
	int texnum;

	Q_snprintf( name, sizeof( name ), "%s#%d", st->path, slot );
	texnum = VK_StreamSlotCreate( st->slots[slot], name, st->format,
		Q_max( st->width >> first, 1 ), Q_max( st->height >> first, 1 ), st->levels - first, 0 );

	if( !texnum )
		return -1;

	st->slots[slot] = texnum;

	if( keep < st->levels && old )
	{
		VkBuffer staging;
		VkDeviceSize offset;
		VkCommandBuffer cmd;
		VkImageCopy copies[VK_STREAM_MAX_LEVELS];
		const VkImage src = VK_StreamSlotImage( old ), dst = VK_StreamSlotImage( texnum );
		int num_copies = 0;

		VK_UploadStaging( 0, &staging, &offset, &cmd );

		for( int i = keep; i < st->levels; i++ )
		{
			const uint32_t w = Q_max( st->width >> i, 1 ), h = Q_max( st->height >> i, 1 );

			// BC images copy whole blocks
			copies[num_copies++] = (VkImageCopy){
				.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - st->resident, 0, 1 },
				.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - first, 0, 1 },
				.extent = { w, h, 1 },
			};
			VK_StreamSlotAddSize( texnum, st->size[i] );
		}

		VK_ImageBarrierRaw( cmd, src, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
		vkCmdCopyImage( cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_copies, copies );
		VK_ImageBarrierRaw( cmd, src, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	}

	return slot;
}

static void VK_StreamFinishImage( vk_stream_t *st, int slot, int first )
{
	VK_StreamSlotReady( st->slots[slot] );
	VK_StreamSetActive( st, slot, first, VK_StreamLevelsSize( st, first ));
}

// in one go, from CPU memory: the levels at load, or none when shrinking
static qboolean VK_StreamBuild( vk_stream_t *st, int first, int keep, const byte *data )
{
	const int slot = VK_StreamBeginImage( st, first, keep );
	VkDeviceSize offset = 0;

	if( slot < 0 )
		return false;

	for( int i = first; i < keep; i++ )
	{
		VK_StreamSlotUpload( st->slots[slot], i - first, Q_max( st->width >> i, 1 ), Q_max( st->height >> i, 1 ), data + offset, st->size[i] );
		offset += st->size[i];
	}

	VK_StreamFinishImage( st, slot, first );
	return true;
}

// the other slot is free when the frames that may sample it are done
static qboolean VK_StreamCanSwap( const vk_stream_t *st )
{
	return VK_CurrentFrame() >= st->swap_frame + VK_FRAMES_IN_FLIGHT + 1;
}

// back to the smaller levels, the larger ones dropped
static void VK_StreamShrink( vk_stream_t *st, int first )
{
	if( first <= st->resident || st->loading || !VK_StreamCanSwap( st ))
		return;

	VK_StreamBuild( st, first, first, NULL );
}

/*
=================
Loading jobs
=================
*/
// on a worker: only the job's own data
static void VK_StreamJobRun( void *data )
{
	vk_stream_job_t *job = data;

	job->failed = !VK_StreamReadLevels( job->offset, job->size, job->file, job->first, job->last, job->data );
}

// main thread: the read is done, its image waits for VK_StreamBeginFrame
static void VK_StreamJobDone( void *data )
{
	vk_stream_job_t *job = data;

	gEngfuncs.fsapi->Close( job->file );
	job->file = NULL;

	if( vk_str.ready_tail )
		vk_str.ready_tail->next = job;
	else vk_str.ready = job;
	vk_str.ready_tail = job;
}

static void VK_StreamEndJob( vk_stream_job_t *job )
{
	vk_str.loads--;
	vk_str.reserved -= job->growth;

	// the uploads read it until the frames recorded until then are done
	job->staging->used = false;
	job->staging->busy_until = VK_CurrentFrame() + VK_FRAMES_IN_FLIGHT + 1;
	Mem_Free( job );
}

/*
=================
VK_StreamUpload

sends up to budget bytes of a read job's levels to its new image, makes
the image active once they are all there; false when the job is done
=================
*/
static qboolean VK_StreamUpload( vk_stream_job_t *job, size_t *budget )
{
	vk_stream_t *st = &vk_str.list[job->stream];

	// gone, or failed: nothing more to do
	if( st->generation != job->generation || !st->refs || job->failed )
	{
		if( job->failed )
			gEngfuncs.Con_Printf( S_WARN "ref_vk: can't read the levels of %s\n", st->path );
		if( st->generation == job->generation )
			st->loading = false;
		VK_StreamEndJob( job );
		return false;
	}

	if( job->slot < 0 )
	{
		if(( job->slot = VK_StreamBeginImage( st, job->first, job->last )) < 0 )
		{
			st->loading = false;
			VK_StreamEndJob( job );
			return false;
		}

		job->level = job->first;
		job->row = 0;
		job->sent = 0;
	}

	while( job->level < job->last && *budget > 0 )
	{
		const int w = Q_max( st->width >> job->level, 1 ), h = Q_max( st->height >> job->level, 1 );
		const int block_rows = ( h + 3 ) / 4;
		const size_t row_bytes = st->size[job->level] / block_rows;
		const int rows = Q_max( 1, Q_min( block_rows - job->row, (int)( *budget / row_bytes )));
		const int y = job->row * 4;
		const int height = Q_min( rows * 4, h - y );

		VK_StreamSlotCopyBuffer( st->slots[job->slot], job->level - job->first, y, w, height,
			job->staging->buffer.buffer, job->sent, rows * row_bytes );

		job->sent += rows * row_bytes;
		job->row += rows;
		*budget = *budget > rows * row_bytes ? *budget - rows * row_bytes : 0;

		if( job->row >= block_rows )
		{
			job->level++;
			job->row = 0;
		}
	}

	if( job->level < job->last )
		return true;

	st->loading = false;
	VK_StreamFinishImage( st, job->slot, job->first );
	VK_StreamEndJob( job );
	return false;
}

// a staging buffer free and big enough, grown when none is
static vk_stream_staging_t *VK_StreamStaging( size_t size )
{
	vk_stream_staging_t *spare = NULL;

	for( int i = 0; i < VK_STREAM_MAX_LOADS; i++ )
	{
		vk_stream_staging_t *s = &vk_str.staging[i];

		if( s->used || VK_CurrentFrame() < s->busy_until )
			continue;

		if( s->buffer.size >= size )
			return s;

		spare = s;
	}

	if( !spare )
		return NULL;

	VK_DestroyBuffer( &spare->buffer ); // free: the uploads reading it are done
	if( !VK_CreateBuffer( &spare->buffer, "stream staging", Q_max( size, VK_STREAM_STAGING ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ))
		return NULL;

	return spare;
}

static void VK_StreamLoad( vk_stream_t *st, int first )
{
	const size_t size = VK_StreamLevelsSize( st, first ) - VK_StreamLevelsSize( st, st->resident );
	vk_stream_staging_t *staging;
	vk_stream_job_t *job;
	file_t *f;

	if( !( staging = VK_StreamStaging( size )))
		return;

	if( !( f = gEngfuncs.fsapi->Open( st->path, "rb", false )))
		return;

	job = Mem_Calloc( r_temppool, sizeof( *job ));
	job->stream = st - vk_str.list;
	job->generation = st->generation;
	job->first = first;
	job->last = st->resident;
	job->file = f;
	memcpy( job->offset, st->offset, sizeof( job->offset ));
	memcpy( job->size, st->size, sizeof( job->size ));
	job->staging = staging;
	job->data = staging->buffer.alloc.mapped;
	job->slot = -1;
	job->growth = VK_StreamLevelsSize( st, first ) - st->bytes;
	vk_str.reserved += job->growth;

	staging->used = true;
	st->loading = true;
	vk_str.loads++;

	if( !gEngfuncs.Job_Submit( VK_StreamJobRun, VK_StreamJobDone, job ))
	{
		VK_StreamJobRun( job );
		VK_StreamJobDone( job );
	}
}

/*
=================
VK_StreamOpen

a streamed map for a material, 0 when the file can't be streamed (the
caller loads it the usual way). Shared between materials naming the file.
=================
*/
int VK_StreamOpen( const char *path )
{
	vk_stream_t *st = NULL;
	file_t *f;
	byte *data;

	if( !vk.caps.texture_compression_bc )
		return 0;

	for( int i = 0; i < vk_str.count; i++ )
	{
		if( vk_str.list[i].refs && !Q_stricmp( vk_str.list[i].path, path ))
		{
			vk_str.list[i].refs++;
			return i + 1;
		}
	}

	for( int i = 0; i < vk_str.count && !st; i++ )
	{
		if( !vk_str.list[i].refs )
			st = &vk_str.list[i];
	}

	if( !st )
	{
		if( vk_str.count >= VK_STREAM_MAX )
			return 0;
		st = &vk_str.list[vk_str.count++];
	}

	if( !( f = gEngfuncs.fsapi->Open( path, "rb", false )))
		return 0;

	const int generation = st->generation + 1;
	memset( st, 0, sizeof( *st ));
	st->generation = generation;
	Q_strncpy( st->path, path, sizeof( st->path ));

	if( !VK_StreamParseHeader( st, f ))
	{
		gEngfuncs.fsapi->Close( f );
		return 0;
	}

	// the small levels now, the others when they are seen
	st->base = 0;
	if( r_vk_texture_stream.value )
	{
		while( st->base < st->levels - 1 && Q_max( st->width >> st->base, st->height >> st->base ) > VK_STREAM_BASE_SIZE )
			st->base++;
	}

	data = Mem_Malloc( r_temppool, VK_StreamLevelsSize( st, st->base ));
	st->resident = st->levels;

	if( !VK_StreamReadLevels( st->offset, st->size, f, st->base, st->levels, data ) || !VK_StreamBuild( st, st->base, st->levels, data ))
	{
		Mem_Free( data );
		gEngfuncs.fsapi->Close( f );
		st->path[0] = 0;
		return 0;
	}

	Mem_Free( data );
	gEngfuncs.fsapi->Close( f );
	st->refs = 1;
	st->last_used = gEngfuncs.pfnTime();
	return ( st - vk_str.list ) + 1;
}

void VK_StreamRelease( int handle )
{
	vk_stream_t *st;

	if( handle <= 0 || handle > vk_str.count )
		return;

	st = &vk_str.list[handle - 1];
	if( --st->refs > 0 )
		return;

	VK_StreamSlotFree( st->slots[0] );
	VK_StreamSlotFree( st->slots[1] );
	vk_str.total -= st->bytes;
	st->slots[0] = st->slots[1] = 0;
	st->bytes = 0;
	st->path[0] = 0;
	st->generation++; // a job in flight finds it gone
}

int VK_StreamTexture( int handle )
{
	if( handle <= 0 || handle > vk_str.count || !vk_str.list[handle - 1].refs )
		return 0;

	return vk_str.list[handle - 1].slots[vk_str.list[handle - 1].active];
}

/*
=================
VK_StreamNoteUse

a draw of the map: need is how many screen pixels a texel of the game
texture covers there, game_width the width of that texture
=================
*/
void VK_StreamNoteUse( int handle, float need, int game_width )
{
	vk_stream_t *st;

	if( handle <= 0 || handle > vk_str.count )
		return;

	st = &vk_str.list[handle - 1];
	st->need = Q_max( st->need, need );
	st->game_width = game_width;
	st->last_used = gEngfuncs.pfnTime();
}

// the level the recent draws need
static int VK_StreamWanted( const vk_stream_t *st, double now )
{
	float texels_per_game_texel, level;

	if( now > st->last_used + VK_STREAM_UNUSED || st->need_hold <= 0.0f || st->game_width <= 0 )
		return st->base;

	// one map texel per screen pixel is enough
	texels_per_game_texel = (float)st->width / st->game_width;
	level = floorf( log2f( texels_per_game_texel / st->need_hold ));

	return bound( 0, (int)level, st->base );
}

/*
=================
VK_StreamBeginFrame

before the frame records: what grows, what shrinks
=================
*/
void VK_StreamBeginFrame( void )
{
	const size_t budget = (size_t)Q_max( r_vk_texture_budget.value, 64.0f ) * 1024 * 1024;
	const double now = gEngfuncs.pfnTime();
	static double last_time;
	const float decay = last_time ? powf( 0.5f, (float)( now - last_time ) / VK_STREAM_NEED_DECAY ) : 1.0f;

	last_time = now;

	// nothing streamed: nothing to do, no staging memory
	if( !vk_str.count )
		return;

	// the staging buffers once, so no frame waits for their memory
	for( int i = 0; i < VK_STREAM_MAX_LOADS; i++ )
	{
		if( !vk_str.staging[i].buffer.buffer )
			VK_CreateBuffer( &vk_str.staging[i].buffer, "stream staging", VK_STREAM_STAGING, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	}

	// the finished reads go to their images, at the upload rate
	const double frame_time = bound( 0.0005, now - vk_str.last_upload, 0.1 );
	size_t upload = (size_t)( Q_max( r_vk_texture_upload.value, 16.0f ) * 1024.0 * 1024.0 * frame_time );
	vk_str.last_upload = now;
	while( vk_str.ready && upload > 0 )
	{
		vk_stream_job_t *job = vk_str.ready;

		if( VK_StreamUpload( job, &upload ))
			break; // continues next frame

		if( !( vk_str.ready = job->next ))
			vk_str.ready_tail = NULL;
	}

	for( int i = 0; i < vk_str.count; i++ )
	{
		vk_stream_t *st = &vk_str.list[i];

		st->need_hold = Q_max( st->need, st->need_hold * decay );
		st->need = 0.0f;
	}

	for( int pass = 0; pass < VK_STREAM_MAX_LOADS && vk_str.loads < VK_STREAM_MAX_LOADS; pass++ )
	{
		vk_stream_t *best = NULL;
		int best_wanted = 0;

		// the most recently seen map that needs more, the biggest gap first
		for( int i = 0; i < vk_str.count; i++ )
		{
			vk_stream_t *st = &vk_str.list[i];
			const int wanted = VK_StreamWanted( st, now );

			if( !st->refs || st->loading || wanted >= st->resident || !VK_StreamCanSwap( st ))
				continue;

			if( !best || st->last_used > best->last_used + 0.5 || st->resident - wanted > best->resident - best_wanted )
			{
				best = st;
				best_wanted = wanted;
			}
		}

		if( !best )
			break;

		// room under the budget: the maps unseen for a while shrink first
		while( vk_str.total + vk_str.reserved + VK_StreamLevelsSize( best, best_wanted ) - best->bytes > budget )
		{
			vk_stream_t *victim = NULL;

			for( int i = 0; i < vk_str.count; i++ )
			{
				vk_stream_t *st = &vk_str.list[i];

				if( st == best || !st->refs || st->loading || st->resident >= st->base
					|| now < st->last_used + VK_STREAM_IDLE || !VK_StreamCanSwap( st ))
					continue;

				if( !victim || st->last_used < victim->last_used )
					victim = st;
			}

			if( !victim )
				break;

			VK_StreamShrink( victim, victim->base );
		}

		// what fits: a smaller level than wanted rather than nothing
		while( best_wanted < best->resident && vk_str.total + vk_str.reserved + VK_StreamLevelsSize( best, best_wanted ) - best->bytes > budget )
			best_wanted++;

		if( best_wanted >= best->resident )
		{
			best->need_hold = 0.0f; // stays as it is until seen closer again
			continue;
		}

		VK_StreamLoad( best, best_wanted );
	}
}

static void VK_StreamInfo_f( void )
{
	int count = 0, full = 0;

	for( int i = 0; i < vk_str.count; i++ )
	{
		const vk_stream_t *st = &vk_str.list[i];

		if( !st->refs )
			continue;

		count++;
		if( st->resident == 0 )
			full++;

		if( gEngfuncs.Cmd_Argc() > 1 )
			gEngfuncs.Con_Printf( "%5.1f MB  level %d of %d (%dx%d)  %s\n", st->bytes / ( 1024.0 * 1024.0 ),
				st->resident, st->levels, Q_max( st->width >> st->resident, 1 ), Q_max( st->height >> st->resident, 1 ), st->path );
	}

	gEngfuncs.Con_Printf( "%d streamed maps, %d at full size, %.1f of %.0f MB, %d reads in flight\n",
		count, full, vk_str.total / ( 1024.0 * 1024.0 ), r_vk_texture_budget.value, vk_str.loads );
}

void VK_InitStream( void )
{
	memset( &vk_str, 0, sizeof( vk_str ));
	gEngfuncs.Cmd_AddCommand( "r_vk_streaminfo", VK_StreamInfo_f, "streamed material maps, with an argument the list of them" );
}

void VK_ShutdownStream( void )
{
	// after Job_WaitIdle: every read is in the ready list
	while( vk_str.ready )
	{
		vk_stream_job_t *job = vk_str.ready;
		vk_str.ready = job->next;
		Mem_Free( job );
	}

	if( vk.device )
	{
		vkDeviceWaitIdle( vk.device );
		for( int i = 0; i < VK_STREAM_MAX_LOADS; i++ )
			VK_DestroyBuffer( &vk_str.staging[i].buffer );
	}

	gEngfuncs.Cmd_RemoveCommand( "r_vk_streaminfo" );
	memset( &vk_str, 0, sizeof( vk_str ));
}
