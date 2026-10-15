/*
cl_unifont.c - unicode characters the engine fonts don't have
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
The engine fonts are bitmaps of one codepage (cl_charset, con_charset): a
UTF-8 character outside of it, a Japanese player name for example, was
drawn as '?'. Such a character is drawn instead from a TrueType font of the
system (or con_unicode_font), with stb_truetype: each glyph is made once
for a font height, into its own small texture, the cell of the bitmap font
it is drawn with.
*/

#include "common.h"
#include "client.h"

#if defined( __GNUC__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#elif defined( _MSC_VER )
#pragma warning( push, 0 )
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#elif defined( _MSC_VER )
#pragma warning( pop )
#endif

static CVAR_DEFINE_AUTO( con_unicode_font, "", FCVAR_ARCHIVE, "TrueType font (path) drawing the characters the engine fonts don't have, tried before the system ones" );

#define UNIFONT_MAX_FONTS  8
#define UNIFONT_MAX_GLYPHS 1024 // power of two
#define UNIFONT_OVERSAMPLE 4    // texels of a glyph per texel of the bitmap font: mipmapped, drawn sharp at any HUD scale

// the fonts that may have the glyphs of most languages, in the order they're tried
static const char *unifont_system[] =
{
#if XASH_WIN32
	"%WINDIR%/Fonts/meiryo.ttc",
	"%WINDIR%/Fonts/msgothic.ttc",
	"%WINDIR%/Fonts/YuGothR.ttc",
	"%WINDIR%/Fonts/msyh.ttc",
	"%WINDIR%/Fonts/malgun.ttf",
	"%WINDIR%/Fonts/segoeui.ttf",
	"%WINDIR%/Fonts/seguisym.ttf",
#elif XASH_APPLE
	"/System/Library/Fonts/Hiragino Sans GB.ttc",
	"/System/Library/Fonts/AppleSDGothicNeo.ttc",
	"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
#elif XASH_ANDROID
	"/system/fonts/NotoSansCJK-Regular.ttc",
	"/system/fonts/DroidSansFallback.ttf",
	"/system/fonts/Roboto-Regular.ttf",
#else
	"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
	"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
	"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
	"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
	"/usr/share/fonts/google-droid/DroidSansFallback.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
};

typedef struct
{
	byte *data;
	stbtt_fontinfo info;
} unifont_font_t;

typedef struct
{
	uint32_t codepoint; // 0 for a free entry
	int pixels;         // height of the cell in texels of the glyph
	char name[32];      // of the texture: it is found again after a renderer restart
	cl_glyph_t glyph;   // texture 0: no font has it, drawn as '?'
} unifont_entry_t;

static struct
{
	unifont_font_t fonts[UNIFONT_MAX_FONTS];
	int num_fonts;
	int next_candidate;  // in con_unicode_font, then unifont_system
	qboolean user_tried; // con_unicode_font
	unifont_entry_t glyphs[UNIFONT_MAX_GLYPHS];
	int num_glyphs;
} unifont;

void CL_InitUnicodeFont( void )
{
	Cvar_RegisterVariable( &con_unicode_font );
}

void CL_ShutdownUnicodeFont( void )
{
	for( int i = 0; i < unifont.num_fonts; i++ )
		Mem_Free( unifont.fonts[i].data );

	memset( &unifont, 0, sizeof( unifont ));
}

static qboolean CL_LoadUnicodeFont( const char *pattern )
{
	char path[MAX_SYSPATH];
	const char *windir = getenv( "WINDIR" );
	unifont_font_t *f = &unifont.fonts[unifont.num_fonts];
	fs_offset_t size;
	int offset;

	if( unifont.num_fonts >= UNIFONT_MAX_FONTS || COM_StringEmptyOrNULL( pattern ))
		return false;

	if( !Q_strncmp( pattern, "%WINDIR%", 8 ))
		Q_snprintf( path, sizeof( path ), "%s%s", windir ? windir : "C:/Windows", pattern + 8 );
	else Q_strncpy( path, pattern, sizeof( path ));

	f->data = FS_LoadDirectFile( path, &size );
	if( !f->data )
		return false;

	// the first font of a collection
	offset = stbtt_GetFontOffsetForIndex( f->data, 0 );
	if( offset < 0 || !stbtt_InitFont( &f->info, f->data, offset ))
	{
		Con_DPrintf( S_WARN "%s: %s isn't a TrueType font\n", __func__, path );
		Mem_Free( f->data );
		f->data = NULL;
		return false;
	}

	Con_Reportf( "unicode characters from %s\n", path );
	unifont.num_fonts++;
	return true;
}

// the next font that may have the glyph, false once all were tried
static qboolean CL_LoadNextUnicodeFont( void )
{
	if( !unifont.user_tried )
	{
		unifont.user_tried = true;
		if( CL_LoadUnicodeFont( con_unicode_font.string ))
			return true;
	}

	while( unifont.next_candidate < ARRAYSIZE( unifont_system ))
	{
		if( CL_LoadUnicodeFont( unifont_system[unifont.next_candidate++] ))
			return true;
	}

	return false;
}

// the glyph drawn into a white cell of pixels height, alpha its coverage
static qboolean CL_MakeUnicodeGlyph( const unifont_font_t *f, uint32_t codepoint, int pixels, unifont_entry_t *e )
{
	const float scale = stbtt_ScaleForPixelHeight( &f->info, pixels );
	int ascent, descent, gap, advance, lsb, x0, y0, x1, y1;
	int width, baseline, bw, bh;
	byte *bitmap, *rgba;

	stbtt_GetFontVMetrics( &f->info, &ascent, &descent, &gap );
	stbtt_GetCodepointHMetrics( &f->info, codepoint, &advance, &lsb );
	stbtt_GetCodepointBitmapBox( &f->info, codepoint, scale, scale, &x0, &y0, &x1, &y1 );

	width = Q_max( 1, (int)ceilf( advance * scale ));
	width = Q_max( width, x1 + 1 );
	baseline = Q_rint( ascent * scale );
	bw = x1 - x0;
	bh = y1 - y0;

	rgba = Mem_Calloc( host.mempool, width * pixels * 4 );

	if( bw > 0 && bh > 0 )
	{
		bitmap = Mem_Calloc( host.mempool, bw * bh );
		stbtt_MakeCodepointBitmap( &f->info, bitmap, bw, bh, bw, scale, scale, codepoint );

		for( int y = 0; y < bh; y++ )
		{
			const int ty = baseline + y0 + y;

			if( ty < 0 || ty >= pixels )
				continue;

			for( int x = 0; x < bw; x++ )
			{
				const int tx = Q_max( x0, 0 ) + x;

				if( tx < width )
					rgba[( ty * width + tx ) * 4 + 3] = bitmap[y * bw + x];
			}
		}

		Mem_Free( bitmap );
	}

	for( int i = 0; i < width * pixels; i++ )
		rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;

	rgbdata_t pic = {
		.width = width,
		.height = pixels,
		.depth = 1,
		.type = PF_RGBA_32,
		.flags = IMAGE_HAS_ALPHA,
		.numMips = 1,
		.buffer = rgba,
		.size = width * pixels * 4,
	};

	e->glyph.texture = ref.dllFuncs.GL_LoadTextureFromBuffer( e->name, &pic, TF_CLAMP, false );
	e->glyph.width = (float)width / UNIFONT_OVERSAMPLE;
	e->glyph.height = (float)pixels / UNIFONT_OVERSAMPLE;
	e->glyph.advance = advance * scale / UNIFONT_OVERSAMPLE;

	Mem_Free( rgba );
	return e->glyph.texture != 0;
}

static void CL_FindUnicodeGlyph( uint32_t codepoint, int pixels, unifont_entry_t *e )
{
	for( int i = 0;; i++ )
	{
		// the glyph may be in a font not loaded yet
		if( i == unifont.num_fonts && !CL_LoadNextUnicodeFont( ))
			return;

		const unifont_font_t *f = &unifont.fonts[i];

		if( stbtt_FindGlyphIndex( &f->info, codepoint ))
		{
			CL_MakeUnicodeGlyph( f, codepoint, pixels, e );
			return;
		}
	}
}

/*
=================
CL_UnicodeGlyph

the glyph of a character the font doesn't have, at its height, NULL when
no font has it
=================
*/
const cl_glyph_t *CL_UnicodeGlyph( const cl_font_t *font, uint32_t codepoint )
{
	const int pixels = Q_max( 1, Q_rint( font->charHeight / font->scale * UNIFONT_OVERSAMPLE ));
	uint32_t hash = ( codepoint * 2654435761u ) ^ (uint32_t)pixels;
	unifont_entry_t *e;

	if( !codepoint )
		return NULL;

	for( ;; hash++ )
	{
		e = &unifont.glyphs[hash & ( UNIFONT_MAX_GLYPHS - 1 )];

		if( !e->codepoint )
			break;

		if( e->codepoint == codepoint && e->pixels == pixels )
		{
			if( !e->glyph.texture )
				return NULL;

			// the renderer restarted: made again
			if( ref.dllFuncs.GL_FindTexture( e->name ) != e->glyph.texture )
			{
				e->glyph.texture = 0;
				CL_FindUnicodeGlyph( codepoint, pixels, e );
			}

			return e->glyph.texture ? &e->glyph : NULL;
		}
	}

	// full: the characters seen are a few dozens, this never happens
	if( unifont.num_glyphs >= UNIFONT_MAX_GLYPHS / 2 )
		return NULL;

	unifont.num_glyphs++;
	e->codepoint = codepoint;
	e->pixels = pixels;
	Q_snprintf( e->name, sizeof( e->name ), "*unifont_%x_%d", codepoint, pixels );
	CL_FindUnicodeGlyph( codepoint, pixels, e );

	return e->glyph.texture ? &e->glyph : NULL;
}
