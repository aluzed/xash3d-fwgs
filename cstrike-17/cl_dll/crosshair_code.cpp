/*
crosshair_code.cpp -- crosshair settings, share codes and presets

Share code layout, before the text encoding:

	version     1 byte, XHAIR_CODE_VERSION
	fields      key, value; key = field id << 2 | value type
	checksum    2 bytes, CRC-16/CCITT of everything before

Value types: 0 varint (zigzag; a number is in thousandths), 1 raw float
(4 bytes, little endian), 2 bytes (varint length, then the bytes). The type
is in the key so that a reader skips the fields it doesn't know, which is
what lets a code from a newer version import. Fields equal to their
default are left out. The bytes become Crockford base32 (no I, L, O, U:
nothing to confuse when typed), in groups of five after "XH-".
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crosshair_code.h"

#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

enum
{
	FIELD_ENABLE = 1,
	FIELD_STYLE,
	FIELD_T,
	FIELD_GAP,
	FIELD_SIZE,
	FIELD_THICK,
	FIELD_DOT,
	FIELD_DOT_SIZE,
	FIELD_PAD,
	FIELD_COLOR,
	FIELD_PAD_COLOR,
	FIELD_ADDITIVE,
	FIELD_AA,
	FIELD_SCALE,
	FIELD_DYNAMIC_MOVE,
	FIELD_DYNAMIC_SCALE,
	FIELD_WEAPON_GAP,
};

enum
{
	TYPE_VARINT = 0,
	TYPE_FLOAT,
	TYPE_BYTES,
};

#define CODE_PREFIX "XH-"
#define CODE_GROUP 5
#define CODE_MAX_BYTES 128

static const char base32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

void XHair_DefaultConfig( xhair_config_t *cfg )
{
	memset( cfg, 0, sizeof( *cfg ));
	cfg->enable = true;
	cfg->size = 4.0f;
	cfg->color[1] = 255;
	cfg->color[3] = 255;
	cfg->pad_color[3] = 255;
	cfg->aa = true;
	cfg->scale = 1.0f;
	cfg->dynamic_move = true;
}

/*
=================
cvars
=================
*/
// the shortest text that reads back as exactly v
static void XHair_FormatFloat( float v, char *out, size_t size )
{
	for( int precision = 1; precision <= 9; precision++ )
	{
		snprintf( out, size, "%.*g", precision, v );
		if( strtof( out, NULL ) == v )
			return;
	}
}

static int XHair_Clamp255( int v )
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

void XHair_ReadConfig( xhair_config_t *cfg, xhair_cvar_get_t get )
{
	int c[4];
	int n;

	XHair_DefaultConfig( cfg );

	cfg->enable = atof( get( "xhair_enable" )) != 0.0;
	cfg->style = atoi( get( "xhair_style" ));
	cfg->t = atof( get( "xhair_t" )) != 0.0;
	cfg->gap = strtof( get( "xhair_gap" ), NULL );
	cfg->size = strtof( get( "xhair_size" ), NULL );
	cfg->thick = strtof( get( "xhair_thick" ), NULL );
	cfg->dot = atof( get( "xhair_dot" )) != 0.0;
	cfg->dot_size = strtof( get( "xhair_dot_size" ), NULL );
	cfg->pad = strtof( get( "xhair_pad" ), NULL );
	cfg->additive = atof( get( "xhair_additive" )) != 0.0;
	cfg->aa = atof( get( "xhair_aa" )) != 0.0;
	cfg->scale = strtof( get( "xhair_scale" ), NULL );
	cfg->dynamic_move = atof( get( "xhair_dynamic_move" )) != 0.0;
	cfg->dynamic_scale = strtof( get( "xhair_dynamic_scale" ), NULL );
	cfg->weapon_gap = atof( get( "xhair_gap_useweaponvalue" )) != 0.0;

	c[0] = 0; c[1] = 255; c[2] = 0; c[3] = 255;
	sscanf( get( "xhair_color" ), "%d %d %d %d", &c[0], &c[1], &c[2], &c[3] );
	for( int i = 0; i < 4; i++ )
		cfg->color[i] = XHair_Clamp255( c[i] );

	c[0] = c[1] = c[2] = 0; c[3] = 255;
	n = sscanf( get( "xhair_pad_color" ), "%d %d %d %d", &c[0], &c[1], &c[2], &c[3] );
	cfg->pad_alpha = n == 4;
	for( int i = 0; i < 4; i++ )
		cfg->pad_color[i] = XHair_Clamp255( c[i] );
	if( !cfg->pad_alpha )
		cfg->pad_color[3] = 255;
}

void XHair_WriteConfig( const xhair_config_t *cfg, xhair_cvar_set_t set )
{
	char str[64];

#define SET_FLOAT( name, v ) ( XHair_FormatFloat( v, str, sizeof( str )), set( name, str ))
#define SET_INT( name, v ) ( snprintf( str, sizeof( str ), "%d", (int)( v )), set( name, str ))
	SET_INT( "xhair_enable", cfg->enable );
	SET_INT( "xhair_style", cfg->style );
	SET_INT( "xhair_t", cfg->t );
	SET_FLOAT( "xhair_gap", cfg->gap );
	SET_FLOAT( "xhair_size", cfg->size );
	SET_FLOAT( "xhair_thick", cfg->thick );
	SET_INT( "xhair_dot", cfg->dot );
	SET_FLOAT( "xhair_dot_size", cfg->dot_size );
	SET_FLOAT( "xhair_pad", cfg->pad );
	SET_INT( "xhair_additive", cfg->additive );
	SET_INT( "xhair_aa", cfg->aa );
	SET_FLOAT( "xhair_scale", cfg->scale );
	SET_INT( "xhair_dynamic_move", cfg->dynamic_move );
	SET_FLOAT( "xhair_dynamic_scale", cfg->dynamic_scale );
	SET_INT( "xhair_gap_useweaponvalue", cfg->weapon_gap );
#undef SET_FLOAT
#undef SET_INT

	snprintf( str, sizeof( str ), "%d %d %d %d", cfg->color[0], cfg->color[1], cfg->color[2], cfg->color[3] );
	set( "xhair_color", str );

	if( cfg->pad_alpha )
		snprintf( str, sizeof( str ), "%d %d %d %d", cfg->pad_color[0], cfg->pad_color[1], cfg->pad_color[2], cfg->pad_color[3] );
	else snprintf( str, sizeof( str ), "%d %d %d", cfg->pad_color[0], cfg->pad_color[1], cfg->pad_color[2] );
	set( "xhair_pad_color", str );
}

bool XHair_SameConfig( const xhair_config_t *a, const xhair_config_t *b )
{
	if( a->pad_alpha != b->pad_alpha || ( a->pad_alpha && a->pad_color[3] != b->pad_color[3] ))
		return false;

	for( int i = 0; i < 4; i++ )
	{
		if( a->color[i] != b->color[i] || ( i < 3 && a->pad_color[i] != b->pad_color[i] ))
			return false;
	}

	return a->enable == b->enable && a->style == b->style && a->t == b->t && a->gap == b->gap
		&& a->size == b->size && a->thick == b->thick && a->dot == b->dot && a->dot_size == b->dot_size
		&& a->pad == b->pad && a->additive == b->additive && a->aa == b->aa && a->scale == b->scale
		&& a->dynamic_move == b->dynamic_move && a->dynamic_scale == b->dynamic_scale
		&& a->weapon_gap == b->weapon_gap;
}

/*
=================
bytes
=================
*/
struct xhair_writer_t
{
	unsigned char data[CODE_MAX_BYTES];
	size_t len;
	bool overflow;
};

static void XHair_PutByte( xhair_writer_t *w, unsigned int b )
{
	if( w->len < sizeof( w->data ))
		w->data[w->len++] = (unsigned char)b;
	else w->overflow = true;
}

static void XHair_PutVarint( xhair_writer_t *w, unsigned int v )
{
	while( v >= 0x80 )
	{
		XHair_PutByte( w, ( v & 0x7f ) | 0x80 );
		v >>= 7;
	}
	XHair_PutByte( w, v );
}

static unsigned int XHair_ZigZag( int v )
{
	return ( (unsigned int)v << 1 ) ^ (unsigned int)( v >> 31 );
}

static int XHair_UnZigZag( unsigned int v )
{
	return (int)( v >> 1 ) ^ -(int)( v & 1 );
}

static void XHair_PutInt( xhair_writer_t *w, int id, int v )
{
	XHair_PutVarint( w, id << 2 | TYPE_VARINT );
	XHair_PutVarint( w, XHair_ZigZag( v ));
}

// q thousandths as decimal text, the form both ends turn into a float
static void XHair_Thousandths( int q, char *out, size_t size )
{
	const unsigned int a = q < 0 ? -(unsigned int)q : (unsigned int)q;
	snprintf( out, size, "%s%u.%03u", q < 0 ? "-" : "", a / 1000, a % 1000 );
}

static void XHair_PutFloat( xhair_writer_t *w, int id, float v )
{
	char str[32];

	if( fabsf( v ) < 1000000.0f )
	{
		const int q = (int)lroundf( v * 1000.0f );

		XHair_Thousandths( q, str, sizeof( str ));
		if( strtof( str, NULL ) == v )
		{
			XHair_PutVarint( w, id << 2 | TYPE_VARINT );
			XHair_PutVarint( w, XHair_ZigZag( q ));
			return;
		}
	}

	unsigned int bits;
	memcpy( &bits, &v, sizeof( bits ));
	XHair_PutVarint( w, id << 2 | TYPE_FLOAT );
	for( int i = 0; i < 4; i++ )
		XHair_PutByte( w, ( bits >> ( i * 8 )) & 0xff );
}

static void XHair_PutBytes( xhair_writer_t *w, int id, const int *v, int count )
{
	XHair_PutVarint( w, id << 2 | TYPE_BYTES );
	XHair_PutVarint( w, count );
	for( int i = 0; i < count; i++ )
		XHair_PutByte( w, v[i] );
}

static unsigned int XHair_Crc16( const unsigned char *data, size_t len )
{
	unsigned int crc = 0xffff;

	for( size_t i = 0; i < len; i++ )
	{
		crc ^= data[i] << 8;
		for( int b = 0; b < 8; b++ )
			crc = ( crc & 0x8000 ) ? ( crc << 1 ) ^ 0x1021 : crc << 1;
		crc &= 0xffff;
	}

	return crc;
}

/*
=================
encoding
=================
*/
bool XHair_Encode( const xhair_config_t *cfg, char *out, size_t size )
{
	xhair_config_t def;
	xhair_writer_t w;

	XHair_DefaultConfig( &def );
	memset( &w, 0, sizeof( w ));

	XHair_PutByte( &w, XHAIR_CODE_VERSION );

#define PUT_INT( id, f ) if( cfg->f != def.f ) XHair_PutInt( &w, id, (int)cfg->f )
#define PUT_FLOAT( id, f ) if( cfg->f != def.f ) XHair_PutFloat( &w, id, cfg->f )
	PUT_INT( FIELD_ENABLE, enable );
	PUT_INT( FIELD_STYLE, style );
	PUT_INT( FIELD_T, t );
	PUT_FLOAT( FIELD_GAP, gap );
	PUT_FLOAT( FIELD_SIZE, size );
	PUT_FLOAT( FIELD_THICK, thick );
	PUT_INT( FIELD_DOT, dot );
	PUT_FLOAT( FIELD_DOT_SIZE, dot_size );
	PUT_FLOAT( FIELD_PAD, pad );
	PUT_INT( FIELD_ADDITIVE, additive );
	PUT_INT( FIELD_AA, aa );
	PUT_FLOAT( FIELD_SCALE, scale );
	PUT_INT( FIELD_DYNAMIC_MOVE, dynamic_move );
	PUT_FLOAT( FIELD_DYNAMIC_SCALE, dynamic_scale );
	PUT_INT( FIELD_WEAPON_GAP, weapon_gap );
#undef PUT_INT
#undef PUT_FLOAT

	if( memcmp( cfg->color, def.color, sizeof( def.color )))
		XHair_PutBytes( &w, FIELD_COLOR, cfg->color, 4 );

	// three bytes: the outline takes the crosshair's alpha
	if( cfg->pad_alpha || memcmp( cfg->pad_color, def.pad_color, 3 * sizeof( int )))
		XHair_PutBytes( &w, FIELD_PAD_COLOR, cfg->pad_color, cfg->pad_alpha ? 4 : 3 );

	const unsigned int crc = XHair_Crc16( w.data, w.len );
	XHair_PutByte( &w, crc >> 8 );
	XHair_PutByte( &w, crc & 0xff );

	if( w.overflow )
		return false;

	// base32, five bits at a time, most significant first
	size_t pos = 0, chars = 0;
	unsigned int acc = 0;
	int bits = 0;

#define PUT_CHAR( c ) do { if( pos + 1 >= size ) return false; out[pos++] = ( c ); } while( 0 )
	for( const char *p = CODE_PREFIX; *p; p++ )
		PUT_CHAR( *p );

	for( size_t i = 0; i < w.len || bits > 0; )
	{
		if( bits < 5 && i < w.len )
		{
			acc = ( acc << 8 ) | w.data[i++];
			bits += 8;
		}

		int index;
		if( bits >= 5 )
		{
			index = ( acc >> ( bits - 5 )) & 31;
			bits -= 5;
		}
		else
		{
			index = ( acc << ( 5 - bits )) & 31;
			bits = 0;
		}

		if( chars && chars % CODE_GROUP == 0 )
			PUT_CHAR( '-' );
		PUT_CHAR( base32[index] );
		chars++;
	}
#undef PUT_CHAR

	out[pos] = 0;
	return true;
}

/*
=================
decoding
=================
*/
struct xhair_reader_t
{
	const unsigned char *data;
	size_t len, pos;
	bool error;
};

static unsigned int XHair_GetByte( xhair_reader_t *r )
{
	if( r->pos < r->len )
		return r->data[r->pos++];
	r->error = true;
	return 0;
}

static unsigned int XHair_GetVarint( xhair_reader_t *r )
{
	unsigned int v = 0;

	for( int shift = 0; shift < 35; shift += 7 )
	{
		const unsigned int b = XHair_GetByte( r );
		v |= ( b & 0x7f ) << shift;
		if( !( b & 0x80 ))
			return v;
	}

	r->error = true;
	return v;
}

static int XHair_CharValue( char c )
{
	if( c >= 'a' && c <= 'z' )
		c -= 'a' - 'A';

	// the letters Crockford's base32 leaves out read as their look-alikes
	if( c == 'O' ) c = '0';
	if( c == 'I' || c == 'L' ) c = '1';

	const char *p = strchr( base32, c );
	return c && p ? (int)( p - base32 ) : -1;
}

int XHair_Decode( const char *code, xhair_config_t *cfg, xhair_import_t *info )
{
	unsigned char data[CODE_MAX_BYTES];
	size_t len = 0;
	unsigned int acc = 0;
	int bits = 0;
	xhair_import_t local;

	if( !info )
		info = &local;
	memset( info, 0, sizeof( *info ));
	XHair_DefaultConfig( cfg );

	while( *code == ' ' || *code == '\t' || *code == '"' )
		code++;
	if( strncmp( code, CODE_PREFIX, 3 ) && strncmp( code, "xh-", 3 ))
		return XHAIR_CODE_BAD_TEXT;

	for( const char *p = code + 3; *p; p++ )
	{
		if( *p == '-' || *p == ' ' || *p == '\t' || *p == '"' || *p == '\r' || *p == '\n' )
			continue;

		const int v = XHair_CharValue( *p );
		if( v < 0 )
			return XHAIR_CODE_BAD_TEXT;

		acc = ( acc << 5 ) | v;
		bits += 5;
		if( bits >= 8 )
		{
			if( len >= sizeof( data ))
				return XHAIR_CODE_BAD_TEXT;
			data[len++] = ( acc >> ( bits - 8 )) & 0xff;
			bits -= 8;
		}
	}

	// version and checksum at least
	if( len < 3 )
		return XHAIR_CODE_BAD_TEXT;

	if( XHair_Crc16( data, len - 2 ) != ( (unsigned int)data[len - 2] << 8 | data[len - 1] ))
		return XHAIR_CODE_BAD_CHECKSUM;

	xhair_reader_t r = { data, len - 2, 1, false };
	info->version = data[0];

	while( r.pos < r.len && !r.error )
	{
		const unsigned int key = XHair_GetVarint( &r );
		const int id = key >> 2, type = key & 3;
		unsigned int v = 0;
		int bytes[4] = { 0 };
		int count = 0;
		float f = 0.0f;

		switch( type )
		{
		case TYPE_VARINT:
			v = XHair_GetVarint( &r );
			break;
		case TYPE_FLOAT:
			for( int i = 0; i < 4; i++ )
				v |= XHair_GetByte( &r ) << ( i * 8 );
			memcpy( &f, &v, sizeof( f ));
			break;
		case TYPE_BYTES:
			count = XHair_GetVarint( &r );
			for( int i = 0; i < count && !r.error; i++ )
			{
				const unsigned int b = XHair_GetByte( &r );
				if( i < 4 )
					bytes[i] = b;
			}
			break;
		default:
			// a newer value type: its length is unknown, nothing after it can be read
			info->partial = true;
			info->unknown++;
			return XHAIR_CODE_OK;
		}

		if( r.error )
			break;

		if( type == TYPE_VARINT )
		{
			char str[32];
			XHair_Thousandths( XHair_UnZigZag( v ), str, sizeof( str ));
			f = strtof( str, NULL );
		}

		const int i = XHair_UnZigZag( v );
		bool known = type == TYPE_VARINT;

		switch( id )
		{
		case FIELD_ENABLE: cfg->enable = i != 0; break;
		case FIELD_STYLE: cfg->style = i; break;
		case FIELD_T: cfg->t = i != 0; break;
		case FIELD_DOT: cfg->dot = i != 0; break;
		case FIELD_ADDITIVE: cfg->additive = i != 0; break;
		case FIELD_AA: cfg->aa = i != 0; break;
		case FIELD_DYNAMIC_MOVE: cfg->dynamic_move = i != 0; break;
		case FIELD_WEAPON_GAP: cfg->weapon_gap = i != 0; break;
		case FIELD_GAP: cfg->gap = f; known = type != TYPE_BYTES; break;
		case FIELD_SIZE: cfg->size = f; known = type != TYPE_BYTES; break;
		case FIELD_THICK: cfg->thick = f; known = type != TYPE_BYTES; break;
		case FIELD_DOT_SIZE: cfg->dot_size = f; known = type != TYPE_BYTES; break;
		case FIELD_PAD: cfg->pad = f; known = type != TYPE_BYTES; break;
		case FIELD_SCALE: cfg->scale = f; known = type != TYPE_BYTES; break;
		case FIELD_DYNAMIC_SCALE: cfg->dynamic_scale = f; known = type != TYPE_BYTES; break;
		case FIELD_COLOR:
			known = type == TYPE_BYTES && count >= 4;
			if( known )
				memcpy( cfg->color, bytes, sizeof( cfg->color ));
			break;
		case FIELD_PAD_COLOR:
			known = type == TYPE_BYTES && count >= 3;
			if( known )
			{
				cfg->pad_alpha = count >= 4;
				memcpy( cfg->pad_color, bytes, sizeof( cfg->pad_color ));
				if( !cfg->pad_alpha )
					cfg->pad_color[3] = 255;
			}
			break;
		default:
			known = false;
			break;
		}

		if( !known )
			info->unknown++;
	}

	return r.error ? XHAIR_CODE_TRUNCATED : XHAIR_CODE_OK;
}

const char *XHair_CodeError( int error )
{
	switch( error )
	{
	case XHAIR_CODE_OK: return "ok";
	case XHAIR_CODE_BAD_TEXT: return "not a crosshair code";
	case XHAIR_CODE_BAD_CHECKSUM: return "wrong checksum, the code was mistyped or cut";
	case XHAIR_CODE_TRUNCATED: return "the code is cut short";
	}
	return "unknown error";
}

/*
=================
presets
=================
*/
int XHair_NumBuiltinPresets( void )
{
	return 4;
}

const char *XHair_BuiltinPreset( int index, xhair_config_t *cfg )
{
	XHair_DefaultConfig( cfg );

	switch( index )
	{
	case 0:
		cfg->enable = false;
		return "Classic 1.6";
	case 1:
		cfg->size = 2.5f;
		cfg->gap = -1.0f;
		cfg->thick = 0.6f;
		cfg->pad = 1.0f;
		cfg->dynamic_move = false;
		return "Small static cross";
	case 2:
		cfg->t = true;
		cfg->dot = true;
		cfg->dot_size = 0.8f;
		cfg->size = 3.5f;
		cfg->gap = 0.5f;
		cfg->thick = 0.6f;
		cfg->pad = 1.0f;
		cfg->color[0] = 0; cfg->color[1] = 220; cfg->color[2] = 255;
		return "T with dot";
	case 3:
		cfg->style = 1;
		cfg->gap = 4.0f;
		cfg->thick = 0.5f;
		cfg->dot = true;
		cfg->dot_size = 0.8f;
		cfg->pad = 1.0f;
		cfg->color[0] = cfg->color[1] = cfg->color[2] = 255;
		return "Circle with dot";
	}

	return NULL;
}

// next quoted (or bare) token of the line, false at the end of the line
static bool XHair_Token( const char **text, const char *end, char *out, size_t size )
{
	const char *p = *text;
	size_t n = 0;

	while( p < end && ( *p == ' ' || *p == '\t' || *p == '\r' ))
		p++;
	if( p >= end || *p == '\n' )
	{
		*text = p;
		return false;
	}

	const bool quoted = *p == '"';
	if( quoted )
		p++;

	while( p < end && *p != '\n' && ( quoted ? *p != '"' : ( *p != ' ' && *p != '\t' && *p != '\r' )))
	{
		if( n + 1 < size )
			out[n++] = *p;
		p++;
	}

	if( quoted && p < end && *p == '"' )
		p++;

	out[n] = 0;
	*text = p;
	return true;
}

int XHair_ParsePresets( const char *text, size_t length, xhair_preset_t *presets, int max )
{
	const char *p = text, *end = text + length;
	int count = 0;

	while( p < end && count < max )
	{
		xhair_preset_t *preset = &presets[count];

		while( p < end && ( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ))
			p++;

		if( end - p >= 2 && p[0] == '/' && p[1] == '/' )
		{
			// comment line
		}
		else if( XHair_Token( &p, end, preset->name, sizeof( preset->name ))
			&& XHair_Token( &p, end, preset->code, sizeof( preset->code )))
		{
			count++;
		}

		while( p < end && *p != '\n' )
			p++;
	}

	return count;
}

size_t XHair_FormatPresets( const xhair_preset_t *presets, int count, char *out, size_t size )
{
	size_t len = 0;
	int n = snprintf( out, size, "// crosshair presets: \"name\" \"share code\"\n" );

	if( n < 0 || (size_t)n >= size )
		return 0;
	len = n;

	for( int i = 0; i < count; i++ )
	{
		char name[XHAIR_PRESET_NAME];
		size_t j = 0;

		// a quote would end the name early
		for( const char *s = presets[i].name; *s && j + 1 < sizeof( name ); s++ )
			name[j++] = *s == '"' ? '\'' : *s;
		name[j] = 0;

		n = snprintf( out + len, size - len, "\"%s\" \"%s\"\n", name, presets[i].code );
		if( n < 0 || (size_t)n >= size - len )
			return 0;
		len += n;
	}

	return len;
}

/*
=================
clipboard
=================
*/
typedef int ( *sdl_set_clipboard_t )( const char *text );

bool XHair_SetClipboard( const char *text )
{
	sdl_set_clipboard_t set = NULL;

#if defined( _WIN32 )
	static const char *libs[] = { "SDL2.dll", "SDL3.dll" };
	for( size_t i = 0; i < sizeof( libs ) / sizeof( libs[0] ) && !set; i++ )
	{
		HMODULE module = GetModuleHandleA( libs[i] );
		if( module )
			set = (sdl_set_clipboard_t)GetProcAddress( module, "SDL_SetClipboardText" );
	}
#else
	// the engine may have loaded SDL privately: ask for the loaded library by name
	static const char *libs[] = { "libSDL2-2.0.so.0", "libSDL2.so", "libSDL3.so.0", "libSDL2-2.0.0.dylib", "libSDL3.0.dylib" };
	set = (sdl_set_clipboard_t)dlsym( RTLD_DEFAULT, "SDL_SetClipboardText" );
	for( size_t i = 0; i < sizeof( libs ) / sizeof( libs[0] ) && !set; i++ )
	{
		void *lib = dlopen( libs[i], RTLD_LAZY | RTLD_NOLOAD );
		if( lib )
		{
			set = (sdl_set_clipboard_t)dlsym( lib, "SDL_SetClipboardText" );
			dlclose( lib ); // RTLD_NOLOAD took a reference
		}
	}
#endif

	if( !set )
		return false;

	set( text );
	return true;
}
