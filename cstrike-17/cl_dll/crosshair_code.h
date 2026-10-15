/*
crosshair_code.h -- crosshair settings, share codes and presets

A crosshair is the set of xhair_* cvars. It travels as a share code: a
short text safe to paste in chat ("XH-" then base32 groups), versioned and
checksummed, holding only the values that differ from the defaults. Every
value comes back exactly: numbers with up to three decimals are stored as
such, anything else as the raw float. Presets are named share codes, kept
in crosshair_presets.cfg in the game directory.

Used by the client (crosshair_export, crosshair_import) and by the
crosshair options of the menu, so both read and write the same codes.
*/

#pragma once
#ifndef CROSSHAIR_CODE_H
#define CROSSHAIR_CODE_H

#include <stddef.h>

struct xhair_config_t
{
	bool enable;          // xhair_enable: false is the classic 1.6 crosshair
	int style;            // xhair_style
	bool t;               // xhair_t
	float gap;            // xhair_gap
	float size;           // xhair_size
	float thick;          // xhair_thick
	bool dot;             // xhair_dot
	float dot_size;       // xhair_dot_size
	float pad;            // xhair_pad
	int color[4];         // xhair_color, 0-255
	int pad_color[4];     // xhair_pad_color, 0-255
	bool pad_alpha;       // xhair_pad_color has its own alpha (pad_color[3])
	bool additive;        // xhair_additive
	bool aa;              // xhair_aa
	float scale;          // xhair_scale
	bool dynamic_move;    // xhair_dynamic_move
	float dynamic_scale;  // xhair_dynamic_scale
	bool weapon_gap;      // xhair_gap_useweaponvalue
};

// the client's defaults, enhanced crosshair on
void XHair_DefaultConfig( xhair_config_t *cfg );

// from and to the cvars, through the caller's cvar access
typedef const char *( *xhair_cvar_get_t )( const char *name );
typedef void ( *xhair_cvar_set_t )( const char *name, const char *value );
void XHair_ReadConfig( xhair_config_t *cfg, xhair_cvar_get_t get );
void XHair_WriteConfig( const xhair_config_t *cfg, xhair_cvar_set_t set );

bool XHair_SameConfig( const xhair_config_t *a, const xhair_config_t *b );

#define XHAIR_CODE_VERSION 1
#define XHAIR_CODE_MAX 192 // longest text code, terminator included

// Writes the share code of cfg into out, returns false if it doesn't fit.
bool XHair_Encode( const xhair_config_t *cfg, char *out, size_t size );

enum
{
	XHAIR_CODE_OK = 0,
	XHAIR_CODE_BAD_TEXT,     // not a crosshair code
	XHAIR_CODE_BAD_CHECKSUM, // mistyped or truncated
	XHAIR_CODE_TRUNCATED,    // values cut short
};

struct xhair_import_t
{
	int version;   // format version of the code
	int unknown;   // fields of a newer version that were skipped
	bool partial;  // a newer field could not be skipped: the rest was lost
};

// Fills cfg from a share code; fields the code doesn't hold get their
// default. Returns XHAIR_CODE_*; info may be NULL.
int XHair_Decode( const char *code, xhair_config_t *cfg, xhair_import_t *info );

const char *XHair_CodeError( int error );

// built-in presets, read only
int XHair_NumBuiltinPresets( void );
const char *XHair_BuiltinPreset( int index, xhair_config_t *cfg );

// crosshair_presets.cfg: one preset per line, "name" "code"
#define XHAIR_PRESETS_FILE "crosshair_presets.cfg"
#define XHAIR_PRESET_NAME 32
#define XHAIR_MAX_PRESETS 64

struct xhair_preset_t
{
	char name[XHAIR_PRESET_NAME];
	char code[XHAIR_CODE_MAX];
};

// Parses the file contents (text, not necessarily terminated), returns the
// number of presets read.
int XHair_ParsePresets( const char *text, size_t length, xhair_preset_t *presets, int max );
// Formats presets for the file, returns the length written (0 if too small).
size_t XHair_FormatPresets( const xhair_preset_t *presets, int count, char *out, size_t size );

// Puts text on the system clipboard through the SDL the engine loaded;
// false when there is none.
bool XHair_SetClipboard( const char *text );

#endif // CROSSHAIR_CODE_H
