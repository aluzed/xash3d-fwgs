/*
ktx2enc.cpp - PNG to KTX2 with BC blocks and mip levels, for material packs
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
usage: ktx2enc [-t color|normal|linear] [-f bc7|bc5|bc4] [-q 0-6] [-j threads] in.png out.ktx2

  -t color   colour (albedo, emissive): mip levels averaged in linear light
     normal  tangent space normal map: averaged as vectors, renormalized
     linear  data (roughness, metalness, occlusion, height): plain average
  -f         block format; default bc7 for colour and linear, bc5 for normal
             maps (x, y; z is rebuilt by the shader), bc4 keeps red alone
  -q         BC7 quality, 0 fastest to 6 best (default 4)

Every level down to 1x1 is written, smallest first in the file as KTX2
recommends for streaming; levels under 4x4 are padded to a block. The
encoders are bc7enc and rgbcx (3rdparty/bc7enc_rdo, MIT).
*/

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "bc7enc.h"
#include "rgbcx.h"
#include "lodepng.h"

enum class Kind { Color, Normal, Linear };
enum class Format { BC7, BC5, BC4 };

struct Image
{
	uint32_t width = 0, height = 0;
	std::vector<float> rgba; // linear for colour, -1..1 for normals, 0..1 otherwise
};

static float SrgbToLinear( float v ) { return powf( v, 2.2f ); }
static float LinearToSrgb( float v ) { return powf( std::max( v, 0.0f ), 1.0f / 2.2f ); }

static Image Decode( const std::vector<unsigned char> &pixels, uint32_t w, uint32_t h, Kind kind )
{
	Image img;
	img.width = w;
	img.height = h;
	img.rgba.resize( (size_t)w * h * 4 );

	for( size_t i = 0; i < (size_t)w * h; i++ )
	{
		for( int c = 0; c < 4; c++ )
		{
			float v = pixels[i * 4 + c] / 255.0f;

			if( kind == Kind::Color && c < 3 )
				v = SrgbToLinear( v );
			else if( kind == Kind::Normal && c < 3 )
				v = v * 2.0f - 1.0f;

			img.rgba[i * 4 + c] = v;
		}
	}

	return img;
}

static std::vector<unsigned char> Encode8( const Image &img, Kind kind )
{
	std::vector<unsigned char> out( (size_t)img.width * img.height * 4 );

	for( size_t i = 0; i < (size_t)img.width * img.height; i++ )
	{
		for( int c = 0; c < 4; c++ )
		{
			float v = img.rgba[i * 4 + c];

			if( kind == Kind::Color && c < 3 )
				v = LinearToSrgb( v );
			else if( kind == Kind::Normal && c < 3 )
				v = v * 0.5f + 0.5f;

			out[i * 4 + c] = (unsigned char)std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f );
		}
	}

	return out;
}

// the next mip level: 2x2 box average, normals renormalized
static Image Downsample( const Image &src, Kind kind )
{
	Image dst;
	dst.width = std::max( src.width / 2, 1u );
	dst.height = std::max( src.height / 2, 1u );
	dst.rgba.resize( (size_t)dst.width * dst.height * 4 );

	for( uint32_t y = 0; y < dst.height; y++ )
	{
		for( uint32_t x = 0; x < dst.width; x++ )
		{
			float sum[4] = { 0 };

			for( uint32_t dy = 0; dy < 2; dy++ )
			{
				for( uint32_t dx = 0; dx < 2; dx++ )
				{
					const uint32_t sx = std::min( x * 2 + dx, src.width - 1 );
					const uint32_t sy = std::min( y * 2 + dy, src.height - 1 );

					for( int c = 0; c < 4; c++ )
						sum[c] += src.rgba[( (size_t)sy * src.width + sx ) * 4 + c];
				}
			}

			float *out = &dst.rgba[( (size_t)y * dst.width + x ) * 4];
			for( int c = 0; c < 4; c++ )
				out[c] = sum[c] * 0.25f;

			if( kind == Kind::Normal )
			{
				const float len = sqrtf( out[0] * out[0] + out[1] * out[1] + out[2] * out[2] );

				if( len > 1e-6f )
				{
					for( int c = 0; c < 3; c++ )
						out[c] /= len;
				}
			}
		}
	}

	return dst;
}

static uint32_t BlockBytes( Format f ) { return f == Format::BC4 ? 8 : 16; }

// one level, the blocks shared out between threads
static std::vector<unsigned char> CompressLevel( const std::vector<unsigned char> &pixels, uint32_t w, uint32_t h,
	Format format, const bc7enc_compress_block_params &params, unsigned threads )
{
	const uint32_t bw = ( w + 3 ) / 4, bh = ( h + 3 ) / 4;
	const uint32_t block_bytes = BlockBytes( format );
	std::vector<unsigned char> out( (size_t)bw * bh * block_bytes );
	std::atomic<uint32_t> next_row( 0 );

	auto worker = [&]()
	{
		for( uint32_t by; ( by = next_row++ ) < bh; )
		{
			for( uint32_t bx = 0; bx < bw; bx++ )
			{
				unsigned char block[16 * 4];

				// edges of levels under 4 pixels: repeat the last ones
				for( uint32_t y = 0; y < 4; y++ )
				{
					for( uint32_t x = 0; x < 4; x++ )
					{
						const uint32_t sx = std::min( bx * 4 + x, w - 1 );
						const uint32_t sy = std::min( by * 4 + y, h - 1 );

						memcpy( &block[( y * 4 + x ) * 4], &pixels[( (size_t)sy * w + sx ) * 4], 4 );
					}
				}

				unsigned char *dst = &out[( (size_t)by * bw + bx ) * block_bytes];

				if( format == Format::BC7 )
					bc7enc_compress_block( dst, block, &params );
				else if( format == Format::BC5 )
					rgbcx::encode_bc5_hq( dst, block, 0, 1, 4 );
				else rgbcx::encode_bc4_hq( dst, block, 4 );
			}
		}
	};

	std::vector<std::thread> pool;
	for( unsigned i = 0; i < threads; i++ )
		pool.emplace_back( worker );
	for( auto &t : pool )
		t.join();

	return out;
}

/*
KTX2 container, uncompressed (no supercompression), with a basic data
format descriptor for the block format
*/
static void Put32( std::vector<unsigned char> &v, size_t at, uint32_t x )
{
	memcpy( &v[at], &x, 4 );
}

static void Put64( std::vector<unsigned char> &v, size_t at, uint64_t x )
{
	memcpy( &v[at], &x, 8 );
}

static std::vector<unsigned char> DataFormatDescriptor( Format format, Kind kind )
{
	const int samples = format == Format::BC5 ? 2 : 1;
	const uint32_t block_size = 24 + 16 * samples;
	std::vector<unsigned char> dfd( 4 + block_size, 0 );

	Put32( dfd, 0, (uint32_t)dfd.size());          // dfdTotalSize
	Put32( dfd, 4, 0 );                             // vendor Khronos, basic descriptor
	Put32( dfd, 8, 2 | ( block_size << 16 ));       // version 2, block size
	dfd[12] = format == Format::BC7 ? 134 : format == Format::BC5 ? 132 : 131; // KHR_DF_MODEL_BC7 / BC5 / BC4
	dfd[13] = 1;                                    // primaries BT709
	dfd[14] = kind == Kind::Color && format == Format::BC7 ? 2 : 1; // sRGB or linear transfer
	dfd[15] = 0;                                    // straight alpha
	dfd[16] = 3; dfd[17] = 3;                       // 4x4 texel blocks
	dfd[20] = (unsigned char)BlockBytes( format );  // bytesPlane0

	for( int i = 0; i < samples; i++ )
	{
		const size_t s = 28 + 16 * i;
		const uint32_t bits = format == Format::BC7 ? 128 : 64;

		dfd[s + 0] = (unsigned char)(( i * 64 ) & 0xFF); // bitOffset
		dfd[s + 1] = (unsigned char)(( i * 64 ) >> 8 );
		dfd[s + 2] = (unsigned char)( bits - 1 );         // bitLength
		dfd[s + 3] = (unsigned char)i;                    // channel: data, or red then green for BC5
		Put32( dfd, s + 8, 0 );                           // sampleLower
		Put32( dfd, s + 12, 0xFFFFFFFF );                 // sampleUpper
	}

	return dfd;
}

static bool WriteKTX2( const char *path, Format format, Kind kind, uint32_t width, uint32_t height,
	const std::vector<std::vector<unsigned char>> &levels )
{
	static const unsigned char identifier[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };
	const uint32_t vk_format = format == Format::BC7 ? 145 : format == Format::BC5 ? 141 : 139; // *_UNORM_BLOCK
	const uint32_t num_levels = (uint32_t)levels.size();
	const std::vector<unsigned char> dfd = DataFormatDescriptor( format, kind );
	const size_t header_size = 12 + 9 * 4 + 4 * 4 + 2 * 8;
	const size_t index_size = num_levels * 3 * 8;
	std::vector<unsigned char> file( header_size + index_size + dfd.size(), 0 );

	memcpy( &file[0], identifier, 12 );
	Put32( file, 12, vk_format );
	Put32( file, 16, 1 );              // typeSize
	Put32( file, 20, width );
	Put32( file, 24, height );
	Put32( file, 28, 0 );              // pixelDepth
	Put32( file, 32, 0 );              // layerCount
	Put32( file, 36, 1 );              // faceCount
	Put32( file, 40, num_levels );
	Put32( file, 44, 0 );              // supercompressionScheme
	Put32( file, 48, (uint32_t)( header_size + index_size )); // dfdByteOffset
	Put32( file, 52, (uint32_t)dfd.size());
	// no key / value data, no supercompression global data
	memcpy( &file[header_size + index_size], dfd.data(), dfd.size());

	// the smallest level first, each aligned to its block size
	for( int i = (int)num_levels - 1; i >= 0; i-- )
	{
		const size_t align = BlockBytes( format );
		size_t offset = ( file.size() + align - 1 ) / align * align;

		file.resize( offset + levels[i].size());
		memcpy( &file[offset], levels[i].data(), levels[i].size());

		const size_t entry = header_size + i * 3 * 8;
		Put64( file, entry, offset );
		Put64( file, entry + 8, levels[i].size());
		Put64( file, entry + 16, levels[i].size());
	}

	FILE *f = fopen( path, "wb" );
	if( !f )
		return false;

	const bool ok = fwrite( file.data(), 1, file.size(), f ) == file.size();
	return fclose( f ) == 0 && ok;
}

static void Usage( void )
{
	fprintf( stderr, "usage: ktx2enc [-t color|normal|linear] [-f bc7|bc5|bc4] [-q 0-6] [-j threads] in.png out.ktx2\n" );
	exit( 1 );
}

int main( int argc, char **argv )
{
	Kind kind = Kind::Color;
	int format_arg = -1, quality = 4;
	unsigned threads = std::max( 1u, std::thread::hardware_concurrency());
	const char *in = nullptr, *out = nullptr;

	for( int i = 1; i < argc; i++ )
	{
		const std::string arg = argv[i];

		if( arg == "-t" && i + 1 < argc )
		{
			const std::string v = argv[++i];
			if( v == "color" ) kind = Kind::Color;
			else if( v == "normal" ) kind = Kind::Normal;
			else if( v == "linear" ) kind = Kind::Linear;
			else Usage();
		}
		else if( arg == "-f" && i + 1 < argc )
		{
			const std::string v = argv[++i];
			if( v == "bc7" ) format_arg = (int)Format::BC7;
			else if( v == "bc5" ) format_arg = (int)Format::BC5;
			else if( v == "bc4" ) format_arg = (int)Format::BC4;
			else Usage();
		}
		else if( arg == "-q" && i + 1 < argc )
			quality = std::clamp( atoi( argv[++i] ), 0, 6 );
		else if( arg == "-j" && i + 1 < argc )
			threads = std::max( 1, atoi( argv[++i] ));
		else if( !in )
			in = argv[i];
		else if( !out )
			out = argv[i];
		else Usage();
	}

	if( !in || !out )
		Usage();

	const Format format = format_arg >= 0 ? (Format)format_arg : kind == Kind::Normal ? Format::BC5 : Format::BC7;

	std::vector<unsigned char> pixels;
	unsigned w, h;

	if( lodepng::decode( pixels, w, h, in, LCT_RGBA, 8 ))
	{
		fprintf( stderr, "%s: can't read the PNG\n", in );
		return 1;
	}

	bc7enc_compress_block_init();
	rgbcx::init();

	bc7enc_compress_block_params params;
	bc7enc_compress_block_params_init( &params );
	params.m_uber_level = std::min( quality, (int)BC7ENC_MAX_UBER_LEVEL );
	params.m_max_partitions = quality >= 3 ? BC7ENC_MAX_PARTITIONS : 16;
	if( kind != Kind::Color )
		bc7enc_compress_block_params_init_linear_weights( &params );

	std::vector<std::vector<unsigned char>> levels;
	Image level = Decode( pixels, w, h, kind );

	for( ;; )
	{
		levels.push_back( CompressLevel( Encode8( level, kind ), level.width, level.height, format, params, threads ));

		if( level.width == 1 && level.height == 1 )
			break;

		level = Downsample( level, kind );
	}

	if( !WriteKTX2( out, format, kind, w, h, levels ))
	{
		fprintf( stderr, "%s: can't write\n", out );
		return 1;
	}

	return 0;
}
