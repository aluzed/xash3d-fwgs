/*
fonts.cpp - VGUI2 fonts, rasterized with stb_truetype into atlas pages
Copyright (C) 2026 Xash3D FWGS contributors

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
Schemes ask for Windows fonts by name (Verdana, Tahoma, Trebuchet MS...).
GoldSrc on Linux maps them to the fonts Steam ships in
platform/resource/linux_fonts, the same mapping is used here; Marlett, the
font of VGUI2's check marks and arrows, is valve/resource/marlett.ttf.

A font's size is its line height in pixels, like the Windows fonts it
stands for: ascent plus descent. Glyphs are rendered the first time they
are drawn or measured, with the font's effects (outline, drop shadow,
blur), into 512x512 pages uploaded when they change.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "vgui2_local.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#define PAGE_SIZE 512

struct font_alias_t
{
	const char *names[3]; // the Windows name and the names of the replacement
	const char *files[4]; // regular, bold, italic, bold italic
	int style; // added to the asked style: 1 bold, 2 italic
};

static const font_alias_t font_aliases[] =
{
	{ { "Verdana", "DejaVu Sans" }, { "DejaVuSans", "DejaVuSans-Bold", "DejaVuSans-Oblique", "DejaVuSans-BoldOblique" } },
	{ { "Verdana Bold" }, { "DejaVuSans", "DejaVuSans-Bold", "DejaVuSans-Oblique", "DejaVuSans-BoldOblique" }, 1 },
	{ { "Verdana Italic" }, { "DejaVuSans", "DejaVuSans-Bold", "DejaVuSans-Oblique", "DejaVuSans-BoldOblique" }, 2 },
	{ { "Verdana Bold Italic" }, { "DejaVuSans", "DejaVuSans-Bold", "DejaVuSans-Oblique", "DejaVuSans-BoldOblique" }, 3 },
	{ { "Arial Bold" }, { "LiberationSans-Regular", "LiberationSans-Bold" }, 1 },
	{ { "Tahoma", "Arial", "Liberation Sans" }, { "LiberationSans-Regular", "LiberationSans-Bold" } },
	{ { "Trebuchet MS", "Fira Sans" }, { "FiraSans-Regular", "FiraSans-Medium" } },
	{ { "Courier New", "Courier", "Liberation Mono" }, { "LiberationMono-Regular" } },
};

struct font_file_t
{
	std::vector<unsigned char> data;
	stbtt_fontinfo info;
	bool symbol; // a Windows symbol font: characters are looked up at 0xF000 + code
	std::string family; // from its name table, for AddCustomFontFile
};

struct glyph_set_t
{
	font_file_t *file;
	float scale;
	int ascent;
	int low, high; // characters this set draws, 0 and 0 for all
};

struct font_t
{
	std::vector<glyph_set_t> sets;
	std::unordered_map<int, glyph_t> glyphs;
	int tall, ascent;
	int flags, blur, outline, shadow;
};

struct page_t
{
	std::vector<unsigned char> rgba;
	int texnum;
	bool dirty;
	int x, y, row; // packing: the current row's position and height
};

static std::vector<font_t> fonts; // HFont is the index + 1
static std::unordered_map<std::string, font_file_t *> font_files; // by path, NULL when it couldn't be read
static std::vector<font_file_t *> custom_files;
static std::vector<page_t> pages;

void Font_Shutdown( void )
{
	for( page_t &p : pages )
	{
		if( p.texnum )
			g_engine->FreeTexture( p.texnum );
	}

	for( auto &f : font_files )
		delete f.second;

	fonts.clear();
	font_files.clear();
	custom_files.clear();
	pages.clear();
}

static std::string Font_FamilyName( const stbtt_fontinfo *info )
{
	int len = 0;
	const char *name = stbtt_GetFontNameString( info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 1 );
	std::string out;

	if( !name ) // a symbol font names itself in its own encoding
		name = stbtt_GetFontNameString( info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_SYMBOL, STBTT_MS_LANG_ENGLISH, 1 );

	for( int i = 0; name && i + 1 < len; i += 2 ) // UTF-16 big endian, the names we compare are ASCII
		out += (char)name[i + 1];

	return out;
}

// stb_truetype only knows Unicode cmaps, a symbol font has a (3, 0) one
static bool Font_InitSymbol( stbtt_fontinfo *info, unsigned char *data )
{
	stbtt_uint32 cmap = stbtt__find_table( data, info->fontstart, "cmap" );

	if( !cmap )
		return false;

	int count = ttUSHORT( data + cmap + 2 );

	for( int i = 0; i < count; i++ )
	{
		stbtt_uint32 record = cmap + 4 + 8 * i;

		if( ttUSHORT( data + record ) == STBTT_PLATFORM_ID_MICROSOFT && ttUSHORT( data + record + 2 ) == STBTT_MS_EID_SYMBOL )
		{
			// stbtt_InitFont gave up before reading the glyph offsets' format
			info->index_map = cmap + ttULONG( data + record + 4 );
			info->indexToLocFormat = ttUSHORT( data + info->head + 50 );
			return true;
		}
	}

	return false;
}

static font_file_t *Font_LoadData( const std::string &key, std::vector<unsigned char> &&data )
{
	font_file_t *file = new font_file_t;

	file->data = std::move( data );
	file->symbol = false;

	if( !stbtt_InitFont( &file->info, file->data.data(), stbtt_GetFontOffsetForIndex( file->data.data(), 0 )))
	{
		if( !Font_InitSymbol( &file->info, file->data.data()))
		{
			Con_Printf( "vgui2: %s is not a font stb_truetype can read\n", key.c_str());
			delete file;
			return NULL;
		}

		file->symbol = true;
	}

	file->family = Font_FamilyName( &file->info );
	return file;
}

static bool Font_ReadDiskFile( const char *path, std::vector<unsigned char> &data )
{
	FILE *f = fopen( path, "rb" );

	if( !f )
		return false;

	fseek( f, 0, SEEK_END );
	long len = ftell( f );
	fseek( f, 0, SEEK_SET );

	data.resize( len > 0 ? len : 0 );
	bool ok = len > 0 && fread( data.data(), 1, len, f ) == (size_t)len;
	fclose( f );
	return ok;
}

// a path of the game's filesystem, or one under the install's root when it starts with '/'
static font_file_t *Font_LoadFile( const char *path, bool from_root )
{
	std::string key = from_root ? std::string( "/" ) + path : path;
	auto it = font_files.find( key );

	if( it != font_files.end())
		return it->second;

	std::vector<unsigned char> data;
	bool found = false;

	if( from_root )
	{
		const char *roots[2] = { g_engine->ReadOnlyRootDir(), g_engine->RootDir() };

		for( int i = 0; i < 2 && !found; i++ )
		{
			if( roots[i] && roots[i][0] )
				found = Font_ReadDiskFile(( std::string( roots[i] ) + "/" + path ).c_str(), data );
		}
	}
	else
	{
		int len = 0;
		unsigned char *buf = g_engine->LoadFile( path, &len );

		if( buf )
		{
			data.assign( buf, buf + len );
			g_engine->FreeFile( buf );
			found = len > 0;
		}
	}

	font_file_t *file = found ? Font_LoadData( key, std::move( data )) : NULL;
	font_files[key] = file;
	return file;
}

static font_file_t *Font_Find( const char *name, int weight, int flags )
{
	const int style = ( weight >= 600 ? 1 : 0 ) + (( flags & vgui2::FONTFLAG_ITALIC ) ? 2 : 0 );
	font_file_t *file;

	if( !strcasecmp( name, "Marlett" ) && ( file = Font_LoadFile( "resource/marlett.ttf", false )))
		return file;

	for( font_file_t *custom : custom_files )
	{
		if( !strcasecmp( custom->family.c_str(), name ))
			return custom;
	}

	for( const font_alias_t &alias : font_aliases )
	{
		bool match = false;

		for( const char *n : alias.names )
			match |= n && !strcasecmp( n, name );

		if( !match )
			continue;

		// the style asked for, else the regular one
		const int asked = style | alias.style;

		for( int s : { asked, asked & 1, 0 } )
		{
			if( alias.files[s] && ( file = Font_LoadFile(( std::string( "platform/resource/linux_fonts/" ) + alias.files[s] + ".ttf" ).c_str(), true )))
				return file;
		}
	}

	// not in a Steam install: the engine's own fonts, then the system's
	if(( file = Font_LoadFile( "platform/resource/linux_fonts/DejaVuSans.ttf", true )))
		return file;

	if(( file = Font_LoadFile( "gfx/fonts/tahoma.ttf", false )))
		return file;

	return Font_LoadFile( "usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", true );
}

vgui2::HFont Font_Create( void )
{
	fonts.emplace_back();
	font_t &f = fonts.back();
	f.tall = f.ascent = 0;
	f.flags = f.blur = f.outline = f.shadow = 0;
	return fonts.size();
}

static font_t *Font_Get( vgui2::HFont font )
{
	if( font < 1 || font > fonts.size())
		return NULL;

	return &fonts[font - 1];
}

bool Font_AddGlyphSet( vgui2::HFont handle, const char *name, int tall, int weight, int blur, int scanlines, int flags, int low, int high )
{
	font_t *f = Font_Get( handle );
	font_file_t *file;

	if( !f || !name || tall <= 0 || !( file = Font_Find( name, weight, flags )))
	{
		Con_DPrintf( "vgui2: no font for %s\n", name ? name : "(null)" );
		return false;
	}

	Con_DPrintf( "vgui2: font %s %d, weight %d, flags 0x%x: %s%s\n", name, tall, weight, flags, file->family.c_str(), file->symbol ? " (symbol)" : "" );

	glyph_set_t set;
	int ascent, descent, gap;

	set.file = file;
	set.scale = stbtt_ScaleForPixelHeight( &file->info, tall );
	stbtt_GetFontVMetrics( &file->info, &ascent, &descent, &gap );
	set.ascent = (int)ceilf( ascent * set.scale );
	set.low = low;
	set.high = high;

	// the first set gives the font its size and effects
	if( f->sets.empty())
	{
		f->flags = flags;
		f->blur = blur;
		f->outline = ( flags & vgui2::FONTFLAG_OUTLINE ) ? 1 : 0;
		f->shadow = ( flags & vgui2::FONTFLAG_DROPSHADOW ) ? 1 : 0;
		f->tall = tall + f->shadow + 2 * f->outline;
		f->ascent = set.ascent;
	}

	f->sets.push_back( set );
	f->glyphs.clear();
	return true;
}

bool Font_AddCustomFile( const char *path )
{
	font_file_t *file = Font_LoadFile( path, false );

	if( !file )
		return false;

	for( font_file_t *custom : custom_files )
	{
		if( custom == file )
			return true;
	}

	custom_files.push_back( file );
	return true;
}

int Font_Tall( vgui2::HFont font )
{
	font_t *f = Font_Get( font );
	return f ? f->tall : 0;
}

int Font_Ascent( vgui2::HFont font )
{
	font_t *f = Font_Get( font );
	return f ? f->ascent : 0;
}

bool Font_IsAdditive( vgui2::HFont font )
{
	font_t *f = Font_Get( font );
	return f && ( f->flags & vgui2::FONTFLAG_ADDITIVE );
}

int Font_Blur( vgui2::HFont font )
{
	font_t *f = Font_Get( font );
	return f ? f->blur : 0;
}

// room for a w x h glyph in the pages
static int Font_PlaceGlyph( int w, int h, int &x, int &y )
{
	if( w > PAGE_SIZE || h > PAGE_SIZE )
		return -1;

	if( !pages.empty())
	{
		page_t &p = pages.back();

		if( p.x + w > PAGE_SIZE ) // next row
		{
			p.x = 0;
			p.y += p.row + 1;
			p.row = 0;
		}

		if( p.y + h <= PAGE_SIZE )
		{
			x = p.x;
			y = p.y;
			p.x += w + 1;
			p.row = h > p.row ? h : p.row;
			return pages.size() - 1;
		}
	}

	pages.emplace_back();
	page_t &p = pages.back();
	p.rgba.assign( PAGE_SIZE * PAGE_SIZE * 4, 0 );
	p.texnum = 0;
	p.dirty = true;
	p.x = w + 1;
	p.y = 0;
	p.row = h;
	x = y = 0;
	return pages.size() - 1;
}

static void Font_BlurAlpha( std::vector<int> &alpha, int w, int h, int radius )
{
	std::vector<int> tmp( alpha.size());

	for( int pass = 0; pass < 2; pass++ ) // horizontally, then vertically
	{
		for( int y = 0; y < h; y++ )
		{
			for( int x = 0; x < w; x++ )
			{
				int sum = 0, n = 0;

				for( int k = -radius; k <= radius; k++ )
				{
					int sx = pass ? x : x + k, sy = pass ? y + k : y;

					if( sx >= 0 && sx < w && sy >= 0 && sy < h )
						sum += alpha[sy * w + sx];
					n++;
				}

				tmp[y * w + x] = sum / n;
			}
		}

		alpha.swap( tmp );
	}
}

static void Font_RenderGlyph( font_t *f, const glyph_set_t &set, int ch, glyph_t &g )
{
	const stbtt_fontinfo *info = &set.file->info;
	int index = 0, advance, lsb, x0, y0, x1, y1;

	// a symbol font (Marlett) keeps its characters at 0xF000 + code, in a
	// symbol or a Unicode table
	if( set.file->symbol )
		index = stbtt_FindGlyphIndex( info, 0xF000 + ch );
	if( !index )
		index = stbtt_FindGlyphIndex( info, ch );
	if( !index && ch < 0x100 )
		index = stbtt_FindGlyphIndex( info, 0xF000 + ch );

	stbtt_GetGlyphHMetrics( info, index, &advance, &lsb );
	stbtt_GetGlyphBitmapBox( info, index, set.scale, set.scale, &x0, &y0, &x1, &y1 );

	const int pad = f->outline + f->blur;
	const int gw = x1 - x0, gh = y1 - y0;
	const int full = (int)floorf( advance * set.scale + 0.5f );

	g.page = -1;
	g.x = g.y = 0;

	if( gw <= 0 || gh <= 0 ) // nothing to draw, a space
	{
		g.a = 0;
		g.b = 0;
		g.c = full;
		g.w = g.h = 0;
		g.top = 0;
		return;
	}

	// the glyph with room around it for its effects
	const int w = gw + pad * 2 + f->shadow, h = gh + pad * 2 + f->shadow;
	std::vector<unsigned char> mono( gw * gh );
	std::vector<int> glyph( w * h, 0 );

	stbtt_MakeGlyphBitmap( info, mono.data(), gw, gh, gw, set.scale, set.scale, index );

	for( int y = 0; y < gh; y++ )
	{
		for( int x = 0; x < gw; x++ )
		{
			int a = mono[y * gw + x];

			if( !( f->flags & vgui2::FONTFLAG_ANTIALIAS ) && !set.file->symbol )
				a = a >= 128 ? 255 : 0;

			glyph[( y + pad ) * w + x + pad] = a;
		}
	}

	if( f->blur > 0 )
		Font_BlurAlpha( glyph, w, h, f->blur );

	// what is drawn black under the glyph: its outline or its shadow
	std::vector<int> under( w * h, 0 );

	for( int y = 0; y < h; y++ )
	{
		for( int x = 0; x < w; x++ )
		{
			int a = 0;

			if( f->outline )
			{
				for( int dy = -1; dy <= 1; dy++ )
				{
					for( int dx = -1; dx <= 1; dx++ )
					{
						int sx = x + dx, sy = y + dy;

						if( sx >= 0 && sx < w && sy >= 0 && sy < h && glyph[sy * w + sx] > a )
							a = glyph[sy * w + sx];
					}
				}
			}

			if( f->shadow && x >= 1 && y >= 1 && glyph[( y - 1 ) * w + x - 1] > a )
				a = glyph[( y - 1 ) * w + x - 1];

			under[y * w + x] = a;
		}
	}

	g.page = Font_PlaceGlyph( w, h, g.x, g.y );
	g.w = w;
	g.h = h;
	g.a = x0 - pad;
	g.b = w;
	g.c = full - g.a - g.b;
	g.top = f->ascent + f->outline + y0 - pad; // every set on the first one's baseline

	if( g.page < 0 )
		return;

	page_t &p = pages[g.page];

	for( int y = 0; y < h; y++ )
	{
		unsigned char *out = &p.rgba[(( g.y + y ) * PAGE_SIZE + g.x ) * 4];

		for( int x = 0; x < w; x++, out += 4 )
		{
			// the white glyph over its black outline or shadow
			int fg = glyph[y * w + x], bg = under[y * w + x];
			int a = fg + bg * ( 255 - fg ) / 255;
			int c = a ? fg * 255 / a : 0;

			out[0] = out[1] = out[2] = c;
			out[3] = a;
		}
	}

	p.dirty = true;
}

const glyph_t *Font_Glyph( vgui2::HFont font, int ch )
{
	font_t *f = Font_Get( font );

	if( !f || f->sets.empty())
		return NULL;

	auto it = f->glyphs.find( ch );

	if( it != f->glyphs.end())
		return &it->second;

	const glyph_set_t *set = &f->sets[0];

	for( const glyph_set_t &s : f->sets )
	{
		if(( !s.low && !s.high ) || ( ch >= s.low && ch <= s.high ))
		{
			set = &s;
			break;
		}
	}

	glyph_t &g = f->glyphs[ch];
	Font_RenderGlyph( f, *set, ch, g );
	return &g;
}

int Font_PageTexture( int page )
{
	if( page < 0 || page >= (int)pages.size())
		return 0;

	page_t &p = pages[page];

	if( p.dirty )
	{
		char name[32];

		snprintf( name, sizeof( name ), "*vgui2_font%d", page );
		p.texnum = g_engine->LoadTextureRGBA( name, p.rgba.data(), PAGE_SIZE, PAGE_SIZE, p.texnum != 0 );
		p.dirty = false;
	}

	return p.texnum;
}
