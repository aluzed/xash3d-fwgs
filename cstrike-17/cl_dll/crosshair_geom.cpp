/*
crosshair_geom.cpp -- geometry of the customizable crosshair

Every shape of a layer is rasterized into a coverage grid: for a bar, the
covered fraction of a pixel is the product of the bar's overlap with the
pixel along each axis, which is exact; for a ring, pixels that the ring
edges cross are supersampled. Overlapping shapes add up, clamped to a full
pixel. The grid then becomes runs of equal coverage on each row, merged
downwards into rectangles while the rows below repeat them, which keeps a
bar to a handful of rectangles.
*/

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "crosshair_geom.h"

#define XHAIR_GRID 512 // largest crosshair layer, in pixels per side
#define XHAIR_RING_SAMPLES 8

static float xhair_grid[XHAIR_GRID * XHAIR_GRID];

struct xhair_layer_t
{
	int x0, y0; // grid origin on the screen
	int w, h;
};

// covered length of pixel [i, i + 1] by [a, b]
static float XHair_Overlap( int i, float a, float b )
{
	const float lo = fmaxf( a, (float)i );
	const float hi = fminf( b, (float)( i + 1 ));

	return hi > lo ? hi - lo : 0.0f;
}

static void XHair_AddRect( const xhair_layer_t *l, float x0, float y0, float x1, float y1, bool antialias )
{
	if( x1 <= x0 || y1 <= y0 )
		return;

	if( !antialias )
	{
		// whole pixels, never less than one
		x0 = floorf( x0 + 0.5f ); x1 = floorf( x1 + 0.5f );
		y0 = floorf( y0 + 0.5f ); y1 = floorf( y1 + 0.5f );
		if( x1 <= x0 ) x1 = x0 + 1.0f;
		if( y1 <= y0 ) y1 = y0 + 1.0f;
	}

	const int i0 = (int)floorf( x0 ), i1 = (int)ceilf( x1 );
	const int j0 = (int)floorf( y0 ), j1 = (int)ceilf( y1 );

	for( int j = j0; j < j1; j++ )
	{
		const int gy = j - l->y0;
		const float cy = XHair_Overlap( j, y0, y1 );

		if( gy < 0 || gy >= l->h || cy <= 0.0f )
			continue;

		for( int i = i0; i < i1; i++ )
		{
			const int gx = i - l->x0;

			if( gx >= 0 && gx < l->w )
				xhair_grid[gy * XHAIR_GRID + gx] += XHair_Overlap( i, x0, x1 ) * cy;
		}
	}
}

static float XHair_RingCoverage( int i, int j, float cx, float cy, float r0, float r1, bool antialias )
{
	// nearest and farthest points of the pixel from the centre
	const float dx0 = fabsf( i - cx ), dx1 = fabsf( i + 1 - cx );
	const float dy0 = fabsf( j - cy ), dy1 = fabsf( j + 1 - cy );
	const float nx = ( cx >= i && cx <= i + 1 ) ? 0.0f : fminf( dx0, dx1 );
	const float ny = ( cy >= j && cy <= j + 1 ) ? 0.0f : fminf( dy0, dy1 );
	const float fx = fmaxf( dx0, dx1 ), fy = fmaxf( dy0, dy1 );
	const float dmin = sqrtf( nx * nx + ny * ny );
	const float dmax = sqrtf( fx * fx + fy * fy );

	if( dmax <= r0 || dmin >= r1 )
		return 0.0f;

	if( !antialias )
	{
		// the pixel centre decides
		const float ex = i + 0.5f - cx, ey = j + 0.5f - cy;
		const float d = sqrtf( ex * ex + ey * ey );
		return ( d >= r0 && d < r1 ) ? 1.0f : 0.0f;
	}

	if( dmin >= r0 && dmax <= r1 )
		return 1.0f;

	int inside = 0;

	for( int sy = 0; sy < XHAIR_RING_SAMPLES; sy++ )
	{
		for( int sx = 0; sx < XHAIR_RING_SAMPLES; sx++ )
		{
			const float ex = i + ( sx + 0.5f ) / XHAIR_RING_SAMPLES - cx;
			const float ey = j + ( sy + 0.5f ) / XHAIR_RING_SAMPLES - cy;
			const float d2 = ex * ex + ey * ey;

			if( d2 >= r0 * r0 && d2 < r1 * r1 )
				inside++;
		}
	}

	return (float)inside / ( XHAIR_RING_SAMPLES * XHAIR_RING_SAMPLES );
}

static void XHair_AddRing( const xhair_layer_t *l, float cx, float cy, float r0, float r1, bool antialias )
{
	if( r1 <= r0 || r1 <= 0.0f )
		return;

	r0 = fmaxf( r0, 0.0f );

	if( !antialias && r1 - r0 < 1.0f )
	{
		// at least one pixel wide around the middle radius
		const float mid = ( r0 + r1 ) * 0.5f;
		r0 = fmaxf( mid - 0.5f, 0.0f );
		r1 = mid + 0.5f;
	}

	const int i0 = (int)floorf( cx - r1 ), i1 = (int)ceilf( cx + r1 );
	const int j0 = (int)floorf( cy - r1 ), j1 = (int)ceilf( cy + r1 );

	for( int j = j0; j < j1; j++ )
	{
		const int gy = j - l->y0;

		if( gy < 0 || gy >= l->h )
			continue;

		for( int i = i0; i < i1; i++ )
		{
			const int gx = i - l->x0;

			if( gx >= 0 && gx < l->w )
				xhair_grid[gy * XHAIR_GRID + gx] += XHair_RingCoverage( i, j, cx, cy, r0, r1, antialias );
		}
	}
}

static int XHair_Level( float coverage )
{
	return (int)( fminf( coverage, 1.0f ) * 255.0f + 0.5f );
}

// the grid as rectangles of equal coverage
static int XHair_Emit( const xhair_layer_t *l, xhair_rect_t *out, int max_rects )
{
	static int open[XHAIR_MAX_RECTS], next_open[XHAIR_MAX_RECTS];
	int num_open = 0;
	int count = 0;

	for( int gy = 0; gy < l->h; gy++ )
	{
		const float *row = &xhair_grid[gy * XHAIR_GRID];
		int num_next = 0;

		for( int gx = 0; gx < l->w; )
		{
			const int level = XHair_Level( row[gx] );
			int run = 1;

			if( level <= 0 )
			{
				gx++;
				continue;
			}

			while( gx + run < l->w && XHair_Level( row[gx + run] ) == level )
				run++;

			// the same run ended on the row above: that rectangle grows
			int index = -1;
			for( int k = 0; k < num_open; k++ )
			{
				const xhair_rect_t *r = &out[open[k]];

				if( r->x == l->x0 + gx && r->w == run && XHair_Level( r->alpha ) == level )
				{
					index = open[k];
					break;
				}
			}

			if( index >= 0 )
				out[index].h++;
			else if( count < max_rects )
			{
				index = count++;
				out[index].x = l->x0 + gx;
				out[index].y = l->y0 + gy;
				out[index].w = run;
				out[index].h = 1;
				out[index].alpha = level / 255.0f;
			}

			if( index >= 0 )
				next_open[num_next++] = index;

			gx += run;
		}

		// only rectangles that reached this row can grow on the next one
		memcpy( open, next_open, num_next * sizeof( open[0] ));
		num_open = num_next;
	}

	return count;
}

int XHair_Build( const xhair_params_t *p, float cx, float cy, bool outline, xhair_rect_t *out, int max_rects )
{
	const float grow = outline ? p->outline : 0.0f;
	const float t = fmaxf( p->thickness, 0.0f );
	const float dot_half = ( p->dot_size > 0.0f ? p->dot_size : t ) * 0.5f;
	xhair_layer_t l;
	float extent;

	if( outline && p->outline <= 0.0f )
		return 0;

	// a square around the centre holding every shape of the layer
	if( p->style == XHAIR_STYLE_CIRCLE )
		extent = p->gap + t + grow;
	else extent = fmaxf( p->gap, 0.0f ) + t * 0.5f + p->length + grow;
	if( p->dot )
		extent = fmaxf( extent, dot_half + grow );
	extent = fmaxf( extent, t * 0.5f + grow ) + 2.0f;

	l.x0 = (int)floorf( cx - extent );
	l.y0 = (int)floorf( cy - extent );
	l.w = (int)ceilf( cx + extent ) - l.x0;
	l.h = (int)ceilf( cy + extent ) - l.y0;
	if( l.w > XHAIR_GRID ) l.w = XHAIR_GRID;
	if( l.h > XHAIR_GRID ) l.h = XHAIR_GRID;

	for( int gy = 0; gy < l.h; gy++ )
		memset( &xhair_grid[gy * XHAIR_GRID], 0, l.w * sizeof( float ));

	if( p->style == XHAIR_STYLE_CIRCLE )
	{
		// the inner edge of the ring is at the gap, like the inner end of the bars
		XHair_AddRing( &l, cx, cy, p->gap - grow, p->gap + t + grow, p->antialias );
	}
	else if( p->length > 0.0f )
	{
		const float half = t * 0.5f;
		const float inner = p->gap + half; // centre to the inner end of a bar
		const float outer = inner + p->length;

		// left, right, bottom, then top unless T
		XHair_AddRect( &l, cx - outer - grow, cy - half - grow, cx - inner + grow, cy + half + grow, p->antialias );
		XHair_AddRect( &l, cx + inner - grow, cy - half - grow, cx + outer + grow, cy + half + grow, p->antialias );
		XHair_AddRect( &l, cx - half - grow, cy + inner - grow, cx + half + grow, cy + outer + grow, p->antialias );

		if( !p->no_top )
			XHair_AddRect( &l, cx - half - grow, cy - outer - grow, cx + half + grow, cy - inner + grow, p->antialias );
	}

	if( p->dot )
		XHair_AddRect( &l, cx - dot_half - grow, cy - dot_half - grow, cx + dot_half + grow, cy + dot_half + grow, p->antialias );

	return XHair_Emit( &l, out, max_rects );
}

float XHair_Centre( int size )
{
	return floorf( size * 0.5f ) + 0.5f;
}

void XHair_SetupParams( xhair_params_t *p, const xhair_settings_t *s, float gap, int height )
{
	// "default" resolution is 640x480, no rounding: sizes can be fractional
	const float units = (float)height / 480.0f * ( s->scale > 0.0f ? s->scale : 1.0f );

	p->style = s->style == XHAIR_STYLE_CIRCLE ? XHAIR_STYLE_CIRCLE : XHAIR_STYLE_CROSS;
	p->no_top = s->t;
	p->gap = gap * units;
	p->length = s->size * units;
	p->thickness = fmaxf( s->thick * units, s->aa ? 0.1f : 1.0f );
	p->dot = s->dot;
	p->dot_size = s->dot_size * units;
	p->outline = s->pad; // pixels, not scaled
	p->antialias = s->aa;
}

// "r g b [a]", 0-255 components, into 0-1 floats; a stays unchanged when absent
static void XHair_ParseColor( const char *string, float rgba[4] )
{
	float c[4] = { rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f };

	sscanf( string, "%f %f %f %f", &c[0], &c[1], &c[2], &c[3] );

	for( int i = 0; i < 4; i++ )
		rgba[i] = fminf( fmaxf( c[i], 0.0f ), 255.0f ) / 255.0f;
}

void XHair_ParseColors( const char *color, const char *outline, float rgba[4], float outline_rgba[4] )
{
	rgba[0] = 0.0f; rgba[1] = 1.0f; rgba[2] = 0.0f; rgba[3] = 1.0f;
	XHair_ParseColor( color, rgba );

	outline_rgba[0] = outline_rgba[1] = outline_rgba[2] = 0.0f;
	outline_rgba[3] = rgba[3];
	XHair_ParseColor( outline, outline_rgba );
}

float XHair_StepGap( float gap, float min_gap, float delta, bool fired, float frametime )
{
	if( fired )
		gap = fminf( gap + delta, XHAIR_MAX_GAP );
	else gap -= ( 100.0f * frametime ) * ( 0.013f * gap + 0.1f );

	return fmaxf( gap, min_gap );
}
