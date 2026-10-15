/*
crosshair_geom.h -- geometry of the customizable crosshair

Turns the crosshair parameters into the exact coverage of every screen
pixel, as rectangles of whole pixels with a constant alpha. Sizes can be
fractional: a pixel half covered by a bar gets half its alpha. Drawing
whole pixel rectangles needs nothing from the renderer state, and the HUD
and the options preview share this code, so they draw the same pixels.
*/

#pragma once
#ifndef CROSSHAIR_GEOM_H
#define CROSSHAIR_GEOM_H

enum
{
	XHAIR_STYLE_CROSS = 0,
	XHAIR_STYLE_CIRCLE = 1,
};

struct xhair_params_t
{
	int style;        // XHAIR_STYLE_*
	bool no_top;      // cross without its top bar (T)
	float gap;        // centre to the inner end of the bars, minus half the thickness (CS:GO convention)
	float length;     // bar length
	float thickness;  // bar or ring thickness
	bool dot;         // centre dot
	float dot_size;   // 0: the thickness
	float outline;    // outline width around every shape, 0: none
	bool antialias;   // fractional coverage; off: shapes snapped to whole pixels
};

// pixels x .. x + w - 1, y .. y + h - 1, covered by alpha (0-1)
struct xhair_rect_t
{
	int x, y, w, h;
	float alpha;
};

#define XHAIR_MAX_RECTS 4096

// the xhair_* cvars that shape the crosshair, in their own units
struct xhair_settings_t
{
	int style;        // xhair_style
	bool t;           // xhair_t
	float size;       // xhair_size, 640x480 units
	float thick;      // xhair_thick, 640x480 units
	bool dot;         // xhair_dot
	float dot_size;   // xhair_dot_size, 640x480 units
	float pad;        // xhair_pad, screen pixels
	bool aa;          // xhair_aa
	float scale;      // xhair_scale
};

#define XHAIR_MAX_GAP 15.0f // dynamic gap ceiling, 640x480 units

// Parameters of a crosshair drawn on a screen of the given height in
// pixels, gap being the distance in 640x480 units (dynamic part and
// xhair_gap included).
void XHair_SetupParams( xhair_params_t *p, const xhair_settings_t *s, float gap, int height );

// xhair_color and xhair_pad_color ("r g b [a]", 0-255) into 0-1 floats; the
// outline takes the crosshair's alpha unless it gives its own.
void XHair_ParseColors( const char *color, const char *outline, float rgba[4], float outline_rgba[4] );

// One frame of the dynamic gap: it grows by delta when a shot was fired,
// otherwise decays as if the game ran at 100 fps, never below min_gap.
float XHair_StepGap( float gap, float min_gap, float delta, bool fired, float frametime );

// Fills out with the pixel rectangles of the crosshair centred on cx, cy
// (screen pixels, pixel i spanning [i, i + 1]): the outline layer when
// outline is true, the crosshair itself otherwise. Rectangles of a layer
// don't overlap. Returns how many were written.
int XHair_Build( const xhair_params_t *p, float cx, float cy, bool outline, xhair_rect_t *out, int max_rects );

// Screen centre for a crosshair: the middle of a pixel, so that an odd
// thickness lands on whole pixels instead of straddling two.
float XHair_Centre( int size );

#endif // CROSSHAIR_GEOM_H
