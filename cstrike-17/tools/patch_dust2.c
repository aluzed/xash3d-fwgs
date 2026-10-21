/*
patch_dust2.c - Counter-Strike 1.7's de_dust2 from Counter-Strike's own (the
player's Steam install), for the installer (story 052) where Python isn't
there: the same map with the small crates of story 065. A port of
scripts/maps/patch_dust2.py and bsp30.py, which stay the reference: both
write the same bytes (computed in double, stored as float, rounded half to
even as Python's round).

  patch_dust2 <Steam cstrike/maps/de_dust2.bsp> <out de_dust2.bsp>
*/
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { L_ENTITIES, L_PLANES, L_TEXTURES, L_VERTICES, L_VISIBILITY, L_NODES, L_TEXINFO,
	L_FACES, L_LIGHTING, L_CLIPNODES, L_LEAVES, L_MARKSURFACES, L_EDGES, L_SURFEDGES, L_MODELS, L_COUNT };

#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2

/* Dust2's small crates are 48 units */
#define SMALL 48.0

typedef struct
{
	uint8_t *data;
	size_t len, cap;
} lump_t;

static lump_t lumps[L_COUNT];

#if defined( _MSC_VER )
#define NORETURN __declspec( noreturn )
#else
#define NORETURN __attribute__(( noreturn ))
#endif

NORETURN static void fail( const char *fmt, ... )
{
	va_list args;
	fputs( "patch_dust2: ", stderr );
	va_start( args, fmt );
	vfprintf( stderr, fmt, args );
	va_end( args );
	fputc( '\n', stderr );
	exit( 1 );
}

/* -- the lumps, little endian as the file ---------------------------------- */

static int32_t rd_i32( const lump_t *l, size_t ofs ) { int32_t v; memcpy( &v, l->data + ofs, 4 ); return v; }
static uint16_t rd_u16( const lump_t *l, size_t ofs ) { uint16_t v; memcpy( &v, l->data + ofs, 2 ); return v; }
static double rd_f32( const lump_t *l, size_t ofs ) { float v; memcpy( &v, l->data + ofs, 4 ); return v; }

static size_t count( int lump, size_t size ) { return lumps[lump].len / size; }

static size_t add( int lump, const void *data, size_t len )
{
	lump_t *l = &lumps[lump];
	size_t index = l->len;
	if( l->len + len > l->cap )
	{
		l->cap = ( l->len + len ) * 2;
		if( !( l->data = realloc( l->data, l->cap ))) fail( "out of memory" );
	}
	memcpy( l->data + l->len, data, len );
	l->len += len;
	return index;
}

/* a record being packed */
typedef struct { uint8_t b[64]; size_t n; } rec_t;
static void put_i32( rec_t *r, int32_t v ) { memcpy( r->b + r->n, &v, 4 ); r->n += 4; }
static void put_i16( rec_t *r, int v )
{
	int16_t s = (int16_t)v;
	if( v < -32768 || v > 32767 ) fail( "a short out of range (%d)", v );
	memcpy( r->b + r->n, &s, 2 ); r->n += 2;
}
static void put_u16( rec_t *r, long v )
{
	uint16_t s = (uint16_t)v;
	if( v < 0 || v > 65535 ) fail( "an unsigned short out of range (%ld)", v );
	memcpy( r->b + r->n, &s, 2 ); r->n += 2;
}
static void put_u8( rec_t *r, int v ) { r->b[r->n++] = (uint8_t)v; }
static void put_f32( rec_t *r, double v ) { float f = (float)v; memcpy( r->b + r->n, &f, 4 ); r->n += 4; }
static size_t add_rec( int lump, const rec_t *r ) { return add( lump, r->b, r->n ); }

static void load( const char *path )
{
	FILE *f = fopen( path, "rb" );
	long size;
	uint8_t *data;
	int32_t version;
	int i;

	if( !f ) fail( "can't open %s", path );
	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );
	if( size < 4 + 8 * L_COUNT ) fail( "%s is not a GoldSrc BSP", path );
	data = malloc( size );
	if( !data || fread( data, 1, size, f ) != (size_t)size ) fail( "can't read %s", path );
	fclose( f );

	memcpy( &version, data, 4 );
	if( version != 30 ) fail( "not a GoldSrc BSP (version %d)", version );
	for( i = 0; i < L_COUNT; i++ )
	{
		int32_t ofs, len;
		memcpy( &ofs, data + 4 + 8 * i, 4 );
		memcpy( &len, data + 8 + 8 * i, 4 );
		if( ofs < 0 || len < 0 || (long)ofs + len > size ) fail( "%s: a lump out of the file", path );
		lumps[i].len = 0;
		add( i, data + ofs, len );
	}
	free( data );
}

static void save( const char *path )
{
	FILE *f = fopen( path, "wb" );
	uint8_t header[4 + 8 * L_COUNT];
	int32_t pos = sizeof( header ), v = 30;
	static const uint8_t zero[4] = { 0 };
	int i;

	if( !f ) fail( "can't write %s", path );
	memcpy( header, &v, 4 );
	for( i = 0; i < L_COUNT; i++ )
	{
		int32_t len = (int32_t)lumps[i].len;
		pos = ( pos + 3 ) & ~3;
		memcpy( header + 4 + 8 * i, &pos, 4 );
		memcpy( header + 8 + 8 * i, &len, 4 );
		pos += len;
	}
	fwrite( header, 1, sizeof( header ), f );
	pos = sizeof( header );
	for( i = 0; i < L_COUNT; i++ )
	{
		fwrite( zero, 1, (( pos + 3 ) & ~3 ) - pos, f );
		pos = ( pos + 3 ) & ~3;
		fwrite( lumps[i].data, 1, lumps[i].len, f );
		pos += (int32_t)lumps[i].len;
	}
	if( fclose( f )) fail( "can't write %s", path );
}

/* -- reading ---------------------------------------------------------------- */

typedef struct { double s[4], t[4]; int miptex; } texinfo_t;
typedef struct { double v[3]; } vec_t3;

static texinfo_t texinfo( int i )
{
	texinfo_t ti;
	int k;
	for( k = 0; k < 4; k++ )
	{
		ti.s[k] = rd_f32( &lumps[L_TEXINFO], i * 40 + k * 4 );
		ti.t[k] = rd_f32( &lumps[L_TEXINFO], i * 40 + 16 + k * 4 );
	}
	ti.miptex = rd_i32( &lumps[L_TEXINFO], i * 40 + 32 );
	return ti;
}

typedef struct { int plane, side, firstedge, numedges, texinfo, lightofs; } face_t;

static face_t face( int i )
{
	const lump_t *l = &lumps[L_FACES];
	face_t f;
	f.plane = rd_u16( l, i * 20 );
	f.side = rd_u16( l, i * 20 + 2 );
	f.firstedge = rd_i32( l, i * 20 + 4 );
	f.numedges = rd_u16( l, i * 20 + 8 );
	f.texinfo = rd_u16( l, i * 20 + 10 );
	f.lightofs = rd_i32( l, i * 20 + 16 );
	return f;
}

static int face_vertices( int i, vec_t3 *verts, int max )
{
	face_t f = face( i );
	int k, n = 0;
	for( k = f.firstedge; k < f.firstedge + f.numedges && n < max; k++ )
	{
		int32_t e = rd_i32( &lumps[L_SURFEDGES], k * 4 );
		int a = rd_u16( &lumps[L_EDGES], abs( e ) * 4 ), b = rd_u16( &lumps[L_EDGES], abs( e ) * 4 + 2 );
		int v = e >= 0 ? a : b, c;
		for( c = 0; c < 3; c++ )
			verts[n].v[c] = rd_f32( &lumps[L_VERTICES], v * 12 + c * 4 );
		n++;
	}
	return n;
}

static double dot4( const double *p, const double *vec )
{
	return p[0] * vec[0] + p[1] * vec[1] + p[2] * vec[2] + vec[3];
}

/* the lowest sample of a face's extents along vec, as the engine computes it */
static int extent_min( const vec_t3 *verts, int n, const double *vec )
{
	double lo = dot4( verts[0].v, vec );
	int k;
	for( k = 1; k < n; k++ ) if( dot4( verts[k].v, vec ) < lo ) lo = dot4( verts[k].v, vec );
	return (int)floor( lo / 16 );
}

static void lightmap_size( const vec_t3 *verts, int n, const texinfo_t *ti, int *w, int *h )
{
	const double *vecs[2] = { ti->s, ti->t };
	int size[2], i, k;
	for( i = 0; i < 2; i++ )
	{
		double lo = dot4( verts[0].v, vecs[i] ), hi = lo;
		for( k = 1; k < n; k++ )
		{
			double v = dot4( verts[k].v, vecs[i] );
			if( v < lo ) lo = v;
			if( v > hi ) hi = v;
		}
		size[i] = (int)ceil( hi / 16 ) - (int)floor( lo / 16 ) + 1;
	}
	*w = size[0];
	*h = size[1];
}

/* the world's face of a texture (NULL: any), facing normal, whose plane
   holds point and whose bounds contain it */
static int find_face( const char *texture, double nx, double ny, double nz, double x, double y, double z )
{
	const lump_t *tex = &lumps[L_TEXTURES];
	const double normal[3] = { nx, ny, nz }, point[3] = { x, y, z };
	int i, nfaces = (int)count( L_FACES, 20 ), ntex = rd_i32( tex, 0 );

	for( i = 0; i < nfaces; i++ )
	{
		face_t f = face( i );
		texinfo_t ti = texinfo( f.texinfo );
		double p[3], dist, n[3];
		vec_t3 verts[64];
		int nv, k, inside = 1;

		if( texture )
		{
			char name[17] = "";
			int32_t ofs = ti.miptex < ntex ? rd_i32( tex, 4 + 4 * ti.miptex ) : -1;
			if( ofs >= 0 )
				memcpy( name, tex->data + ofs, 16 );
			if( strcmp( name, texture ))
				continue;
		}
		for( k = 0; k < 3; k++ )
		{
			p[k] = rd_f32( &lumps[L_PLANES], f.plane * 20 + k * 4 );
			n[k] = f.side ? -p[k] : p[k];
		}
		dist = rd_f32( &lumps[L_PLANES], f.plane * 20 + 12 );
		if( fabs( n[0] - normal[0] ) > 1e-3 || fabs( n[1] - normal[1] ) > 1e-3 || fabs( n[2] - normal[2] ) > 1e-3 )
			continue;
		if( fabs( p[0] * point[0] + p[1] * point[1] + p[2] * point[2] - dist ) > 0.5 )
			continue;
		nv = face_vertices( i, verts, 64 );
		for( k = 0; k < 3 && inside; k++ )
		{
			double lo = verts[0].v[k], hi = lo;
			int j;
			for( j = 1; j < nv; j++ )
			{
				if( verts[j].v[k] < lo ) lo = verts[j].v[k];
				if( verts[j].v[k] > hi ) hi = verts[j].v[k];
			}
			inside = lo - 0.5 <= point[k] && point[k] <= hi + 0.5;
		}
		if( inside )
			return i;
	}
	fail( "no %s face at (%g, %g, %g): is this Counter-Strike's de_dust2?", texture ? texture : "world", x, y, z );
}

/* -- light ------------------------------------------------------------------ */

/* a face's light at a world point (bilinear, clamped to the face), times scale */
typedef struct
{
	texinfo_t ti;
	int smin, tmin, w, h;
	const uint8_t *rgb;
	double scale;
} sampler_t;

static sampler_t light_sampler( int i )
{
	sampler_t sm;
	face_t f = face( i );
	vec_t3 verts[64];
	int nv = face_vertices( i, verts, 64 );

	sm.ti = texinfo( f.texinfo );
	sm.smin = extent_min( verts, nv, sm.ti.s );
	sm.tmin = extent_min( verts, nv, sm.ti.t );
	lightmap_size( verts, nv, &sm.ti, &sm.w, &sm.h );
	sm.rgb = lumps[L_LIGHTING].data + f.lightofs;
	sm.scale = 1.0;
	return sm;
}

static sampler_t scaled( sampler_t sm, double k )
{
	sm.scale = k;
	return sm;
}

static double mix( double x, double y, double k ) { return x + ( y - x ) * k; }

static void sample( const sampler_t *sm, const double *p, double out[3] )
{
	double u = dot4( p, sm->ti.s ) / 16 - sm->smin;
	double v = dot4( p, sm->ti.t ) / 16 - sm->tmin;
	int u0, v0, u1, v1, c;
	double fu, fv;

	u = u < 0 ? 0 : u; if( u > sm->w - 1 ) u = sm->w - 1;
	v = v < 0 ? 0 : v; if( v > sm->h - 1 ) v = sm->h - 1;
	u0 = (int)u; v0 = (int)v;
	u1 = u0 + 1 < sm->w - 1 ? u0 + 1 : sm->w - 1;
	v1 = v0 + 1 < sm->h - 1 ? v0 + 1 : sm->h - 1;
	fu = u - u0; fv = v - v0;
	for( c = 0; c < 3; c++ )
	{
#define AT( x, y ) ( sm->rgb[(( y ) * sm->w + ( x )) * 3 + c] )
		double top = mix( AT( u0, v0 ), AT( u1, v0 ), fu );
		double bottom = mix( AT( u0, v1 ), AT( u1, v1 ), fu );
#undef AT
		out[c] = mix( top, bottom, fv );
		if( sm->scale != 1.0 )
			out[c] = out[c] * sm->scale;
	}
}

/* where a luxel of a new face takes its light from: each coordinate either
   a constant (src < 0) or the luxel's, moved by add, then clamped */
typedef struct { int src; double add, lo, hi; } coord_t;
#define C_( v ) { -1, ( v ), -HUGE_VAL, HUGE_VAL }
#define P_( k, d ) { ( k ), ( d ), -HUGE_VAL, HUGE_VAL }
#define MIN_( k, hi ) { ( k ), 0, -HUGE_VAL, ( hi ) }
#define MAX_( k, lo ) { ( k ), 0, ( lo ), HUGE_VAL }
#define CLAMP_( k, lo, hi ) { ( k ), 0, ( lo ), ( hi ) }

typedef struct
{
	const char *side; /* "-x", "+x", "-y", "+y", "+z" */
	sampler_t sampler;
	coord_t where[3];
} lit_t;

static void where( const coord_t *w, const double *p, double out[3] )
{
	int k;
	for( k = 0; k < 3; k++ )
	{
		double v;
		if( w[k].src < 0 )
		{
			out[k] = w[k].add;
			continue;
		}
		v = w[k].add != 0 ? p[w[k].src] + w[k].add : p[w[k].src];
		if( v < w[k].lo ) v = w[k].lo; /* max( v, lo ) */
		if( v > w[k].hi ) v = w[k].hi; /* min( v, hi ) */
		out[k] = v;
	}
}

/* Python's round() for the light: half to even */
static int light_byte( double c )
{
	double r = nearbyint( c );
	return r < 0 ? 0 : r > 255 ? 255 : (int)r;
}

/* -- writing ---------------------------------------------------------------- */

static int add_plane( int axis, double dist )
{
	rec_t r = { { 0 }, 0 };
	int k;
	for( k = 0; k < 3; k++ ) put_f32( &r, k == axis ? 1.0 : 0.0 );
	put_f32( &r, dist );
	put_i32( &r, axis );
	return (int)( add_rec( L_PLANES, &r ) / 20 );
}

static int add_texinfo( const double *s, const double *t, int miptex )
{
	rec_t r = { { 0 }, 0 };
	int k;
	for( k = 0; k < 4; k++ ) put_f32( &r, s[k] );
	for( k = 0; k < 4; k++ ) put_f32( &r, t[k] );
	put_i32( &r, miptex );
	put_i32( &r, 0 );
	return (int)( add_rec( L_TEXINFO, &r ) / 40 );
}

static void add_entity( const char *keys )
{
	lump_t *l = &lumps[L_ENTITIES];
	while( l->len && l->data[l->len - 1] == 0 ) l->len--;
	add( L_ENTITIES, keys, strlen( keys ) + 1 );
}

/* a side of a box: its axis, its plane's distance taken from mins (-1) or
   maxs (1), its corners counter-clockwise seen from outside */
static const struct { const char *key; int axis, sign, idx[4]; } geometry[6] =
{
	{ "-x", 0, -1, { 0, 4, 6, 2 } }, { "+x", 0, 1, { 1, 3, 7, 5 } },
	{ "-y", 1, -1, { 0, 1, 5, 4 } }, { "+y", 1, 1, { 2, 6, 7, 3 } },
	{ "-z", 2, -1, { 0, 2, 3, 1 } }, { "+z", 2, 1, { 4, 5, 7, 6 } },
};

static int side_index( const char *key )
{
	int i;
	for( i = 0; i < 6; i++ ) if( !strcmp( geometry[i].key, key )) return i;
	fail( "no side %s", key );
}

static void corner( const double *mins, const double *maxs, int i, double *out )
{
	out[0] = i & 1 ? maxs[0] : mins[0];
	out[1] = i & 2 ? maxs[1] : mins[1];
	out[2] = i & 4 ? maxs[2] : mins[2];
}

/* a side's texture vectors moved with the face by delta */
static void moved( const texinfo_t *ti, const double *delta, double *s, double *t )
{
	int k;
	for( k = 0; k < 3; k++ ) { s[k] = ti->s[k]; t[k] = ti->t[k]; }
	s[3] = ti->s[3] - ( ti->s[0] * delta[0] + ti->s[1] * delta[1] + ti->s[2] * delta[2] );
	t[3] = ti->t[3] - ( ti->t[0] * delta[0] + ti->t[1] * delta[1] + ti->t[2] * delta[2] );
}

typedef struct { texinfo_t side[6]; double at[3]; } crate_textures_t;

/* a small crate at mins as a new brush model, built as the map compilers
   build a crate: a node per side holding its face, empty leaves around, the
   shared solid leaf inside, clip nodes for the player's hulls. lit gives
   the drawn sides; the others touch a wall or a crate and stay solid */
static int add_crate( double x, double y, double z, const lit_t *lit, int nlit, const crate_textures_t *tex )
{
	static const char *order[6] = { "+x", "-x", "+z", "+y", "-z", "-y" };
	static const double hulls[3][2][3] =
	{
		{ { -16, -16, -36 }, { 16, 16, 36 } },
		{ { -32, -32, -32 }, { 32, 32, 32 } },
		{ { -16, -16, -18 }, { 16, 16, 18 } },
	};
	const double mins[3] = { x, y, z }, maxs[3] = { x + SMALL, y + SMALL, z + SMALL };
	const double delta[3] = { x - tex->at[0], y - tex->at[1], z - tex->at[2] };
	int planes[6], face_of[6], heads[4], first_face, num_faces, first_node, first_leaf, i, k, h, model;
	char entity[128];
	rec_t r;

	first_face = (int)count( L_FACES, 20 );
	for( i = 0; i < 6; i++ )
	{
		const lit_t *side = NULL;
		double s[4], t[4], dist = geometry[i].sign < 0 ? mins[geometry[i].axis] : maxs[geometry[i].axis];
		vec_t3 verts[4], fv[4];
		int ti, first_vert, first_edge, w, hh, lightofs, j, c;
		uint8_t *light;

		face_of[i] = -1;
		planes[i] = add_plane( geometry[i].axis, dist );
		for( j = 0; j < nlit; j++ ) if( !strcmp( lit[j].side, geometry[i].key )) side = &lit[j];
		if( !side )
			continue;

		moved( &tex->side[i], delta, s, t );
		ti = add_texinfo( s, t, tex->side[i].miptex );

		/* wound clockwise seen from outside, as the compilers write them */
		for( k = 0; k < 4; k++ )
		{
			corner( mins, maxs, geometry[i].idx[3 - k], verts[k].v );
			corner( mins, maxs, geometry[i].idx[k], fv[k].v );
		}
		first_vert = (int)count( L_VERTICES, 12 );
		for( k = 0; k < 4; k++ )
		{
			r.n = 0;
			for( c = 0; c < 3; c++ ) put_f32( &r, verts[k].v[c] );
			add_rec( L_VERTICES, &r );
		}
		first_edge = (int)count( L_SURFEDGES, 4 );
		for( k = 0; k < 4; k++ )
		{
			int e;
			r.n = 0;
			put_u16( &r, first_vert + k );
			put_u16( &r, first_vert + ( k + 1 ) % 4 );
			e = (int)( add_rec( L_EDGES, &r ) / 4 );
			r.n = 0;
			put_i32( &r, e );
			add_rec( L_SURFEDGES, &r );
		}

		/* the lightmap's size from the texinfo as stored, its luxels from
		   the moved vectors */
		{
			texinfo_t stored = texinfo( ti );
			lightmap_size( verts, 4, &stored, &w, &hh );
		}
		light = malloc( w * hh * 3 );
		{
			int smin, tmin, fixed = -1, ks = -1, kt = -1;
			smin = extent_min( fv, 4, s );
			tmin = extent_min( fv, 4, t );
			for( k = 0; k < 3; k++ )
			{
				double lo = fv[0].v[k], hi = lo;
				for( j = 1; j < 4; j++ ) { if( fv[j].v[k] < lo ) lo = fv[j].v[k]; if( fv[j].v[k] > hi ) hi = fv[j].v[k]; }
				if( fixed < 0 && hi - lo < 1e-3 ) fixed = k;
				if( ks < 0 && fabs( s[k] ) > 1e-6 ) ks = k;
				if( kt < 0 && fabs( t[k] ) > 1e-6 ) kt = k;
			}
			for( j = 0; j < hh; j++ )
			{
				for( k = 0; k < w; k++ )
				{
					double p[3] = { 0, 0, 0 }, q[3], rgb[3];
					p[fixed] = fv[0].v[fixed];
					p[ks] = (( smin + k ) * 16 - s[3] ) / s[ks];
					p[kt] = (( tmin + j ) * 16 - t[3] ) / t[kt];
					where( side->where, p, q );
					sample( &side->sampler, q, rgb );
					for( c = 0; c < 3; c++ )
						light[( j * w + k ) * 3 + c] = (uint8_t)light_byte( rgb[c] );
				}
			}
		}
		lightofs = (int)add( L_LIGHTING, light, w * hh * 3 );
		free( light );

		r.n = 0;
		put_u16( &r, planes[i] );
		put_u16( &r, geometry[i].sign < 0 ? 1 : 0 );
		put_i32( &r, first_edge );
		put_u16( &r, 4 );
		put_u16( &r, ti );
		put_u8( &r, 0 ); put_u8( &r, 255 ); put_u8( &r, 255 ); put_u8( &r, 255 );
		put_i32( &r, lightofs );
		face_of[i] = (int)( add_rec( L_FACES, &r ) / 20 );
	}
	num_faces = (int)count( L_FACES, 20 ) - first_face;

	/* hull 0: a node per side, an empty leaf beyond each, the solid leaf
	   (0, shared by every model) inside */
	first_node = (int)count( L_NODES, 24 );
	first_leaf = (int)count( L_LEAVES, 28 );
	for( k = 0; k < 6; k++ )
	{
		int s = side_index( order[k] ), marks = (int)count( L_MARKSURFACES, 2 ), nmarks = 0;
		if( face_of[s] >= 0 )
		{
			r.n = 0;
			put_u16( &r, face_of[s] );
			add_rec( L_MARKSURFACES, &r );
			nmarks = 1;
		}
		r.n = 0;
		put_i32( &r, CONTENTS_EMPTY );
		put_i32( &r, -1 );
		for( i = 0; i < 3; i++ ) put_i16( &r, (int)mins[i] );
		for( i = 0; i < 3; i++ ) put_i16( &r, (int)maxs[i] );
		put_u16( &r, marks );
		put_u16( &r, nmarks );
		for( i = 0; i < 4; i++ ) put_u8( &r, 0 );
		add_rec( L_LEAVES, &r );
	}
	for( k = 0; k < 6; k++ )
	{
		int s = side_index( order[k] );
		int outside = -( first_leaf + k ) - 1, inside = k + 1 < 6 ? first_node + k + 1 : -1;
		r.n = 0;
		put_i32( &r, planes[s] );
		put_i16( &r, geometry[s].sign > 0 ? outside : inside );
		put_i16( &r, geometry[s].sign > 0 ? inside : outside );
		for( i = 0; i < 3; i++ ) put_i16( &r, (int)mins[i] );
		for( i = 0; i < 3; i++ ) put_i16( &r, (int)maxs[i] );
		put_u16( &r, face_of[s] >= 0 ? face_of[s] : 0 );
		put_u16( &r, face_of[s] >= 0 ? 1 : 0 );
		add_rec( L_NODES, &r );
	}

	/* hulls 1-3: the box grown by the hull, the same chain */
	heads[0] = first_node;
	for( h = 0; h < 3; h++ )
	{
		int first_clip = (int)count( L_CLIPNODES, 8 );
		heads[h + 1] = first_clip;
		for( k = 0; k < 6; k++ )
		{
			int s = side_index( order[k] ), axis = geometry[s].axis, p, inside;
			double grown = geometry[s].sign < 0 ? mins[axis] - hulls[h][1][axis] : maxs[axis] - hulls[h][0][axis];
			p = add_plane( axis, grown );
			inside = k + 1 < 6 ? first_clip + k + 1 : CONTENTS_SOLID;
			r.n = 0;
			put_i32( &r, p );
			put_i16( &r, geometry[s].sign > 0 ? CONTENTS_EMPTY : inside );
			put_i16( &r, geometry[s].sign > 0 ? inside : CONTENTS_EMPTY );
			add_rec( L_CLIPNODES, &r );
		}
	}

	model = (int)count( L_MODELS, 64 );
	r.n = 0;
	for( i = 0; i < 3; i++ ) put_f32( &r, mins[i] );
	for( i = 0; i < 3; i++ ) put_f32( &r, maxs[i] );
	for( i = 0; i < 3; i++ ) put_f32( &r, 0 );
	for( i = 0; i < 4; i++ ) put_i32( &r, heads[i] );
	put_i32( &r, 6 );
	put_i32( &r, first_face );
	put_i32( &r, num_faces );
	add_rec( L_MODELS, &r );

	snprintf( entity, sizeof( entity ), "{\n\"classname\" \"func_wall\"\n\"model\" \"*%d\"\n}\n", model );
	add_entity( entity );
	return model;
}

/* -- the crates (patch_dust2.py) ---------------------------------------------- */

/* the textures as a small crate of the map has them (at -1200 2048 0): its
   sides along x and y, its top */
static crate_textures_t crate_textures( void )
{
	crate_textures_t ct;
	texinfo_t side_x = texinfo( face( find_face( "SandCrtSmSd", -1, 0, 0, -1200, 2072, 24 )).texinfo );
	texinfo_t side_y = texinfo( face( find_face( "SandCrtSmSd", 0, 1, 0, -1176, 2096, 24 )).texinfo );
	texinfo_t top = texinfo( face( find_face( "SandCrtSmTp", 0, 0, 1, -1176, 2072, 48 )).texinfo );
	ct.side[0] = ct.side[1] = side_x;
	ct.side[2] = ct.side[3] = side_y;
	ct.side[4] = ct.side[5] = top; /* -z is never drawn */
	ct.at[0] = -1200; ct.at[1] = 2048; ct.at[2] = 0;
	return ct;
}

#define LIT( n ) ( n ), (int)( sizeof( n ) / sizeof( n[0] ))

static void add_mid_crates( int *models )
{
	crate_textures_t tex = crate_textures();
	/* the light of the place: the big crate's sides and top, the floor
	   beside it, the catwalk's wall above it */
	sampler_t big_west = light_sampler( find_face( "SandCrtLrgSd", -1, 0, 0, -352, 1424, -80 ));
	sampler_t big_south = light_sampler( find_face( "SandCrtLrgSd", 0, -1, 0, -304, 1376, -80 ));
	sampler_t big_top = light_sampler( find_face( "SandCrtLrgTp", 0, 0, 1, -304, 1424, -32 ));
	sampler_t floor_ = light_sampler( find_face( NULL, 0, 0, 1, -280, 1352, -128 ));
	sampler_t wall = light_sampler( find_face( NULL, -1, 0, 0, -256, 1424, -16 ));

	/* on the floor, on the terrorists' side of the big crate */
	lit_t low[] =
	{
		{ "-x", big_west, { C_( -352 ), P_( 1, 48 ), P_( 2, 0 ) } },
		{ "-y", big_south, { P_( 0, 0 ), C_( 1376 ), P_( 2, 0 ) } },
		{ "+z", floor_, { P_( 0, -48 ), P_( 1, 0 ), C_( -128 ) } },
	};
	/* on the big crate, against the wall, at its front */
	lit_t high[] =
	{
		{ "-x", wall, { C_( -256 ), P_( 1, 0 ), MIN_( 2, -1 ) } },
		{ "-y", scaled( wall, 1.25 ), { C_( -256 ), C_( 1448 ), MIN_( 2, -1 ) } },
		{ "+y", scaled( wall, 0.6 ), { C_( -256 ), C_( 1448 ), MIN_( 2, -1 ) } },
		{ "+z", big_top, { P_( 0, -48 ), P_( 1, 0 ), C_( -32 ) } },
	};
	models[0] = add_crate( -304, 1328, -128, LIT( low ), &tex );
	models[1] = add_crate( -304, 1424, -32, LIT( high ), &tex );
}

static int add_elevator_crate( void )
{
	crate_textures_t tex = crate_textures();
	sampler_t wall = light_sampler( find_face( NULL, 0, -1, 0, 608, 2400, 64 ));
	sampler_t shortf = light_sampler( find_face( "SandCCrete", 0, 0, 1, 600, 2480, 96 ));
	lit_t lit[] =
	{
		{ "-x", scaled( wall, 0.85 ), { C_( 600 ), C_( 2400 ), MIN_( 2, 95 ) } },
		{ "+x", wall, { C_( 620 ), C_( 2400 ), MIN_( 2, 95 ) } },
		{ "-y", scaled( wall, 1.1 ), { P_( 0, 0 ), C_( 2400 ), MIN_( 2, 95 ) } },
		{ "+z", shortf, { P_( 0, 0 ), P_( 1, 96 ), C_( 96 ) } },
	};
	return add_crate( 576, 2352, 40, LIT( lit ), &tex );
}

static void add_t_crates( int *models )
{
	crate_textures_t tex = crate_textures();
	sampler_t big_west = light_sampler( find_face( "SandCrtLrgSd", -1, 0, 0, -1536, -80, 56 ));
	sampler_t big_top = light_sampler( find_face( "SandCrtLrgTp", 0, 0, 1, -1488, -80, 96 ));
	sampler_t low_wall = light_sampler( find_face( NULL, 0, 1, 0, -1568, -128, 64 ));
	sampler_t high_wall = light_sampler( find_face( NULL, 0, 1, 0, -1488, -128, 112 ));
	sampler_t east_wall = light_sampler( find_face( "SandTrim", -1, 0, 0, -1440, -80, 144 ));
	lit_t low[] =
	{
		{ "-x", big_west, { C_( -1536 ), P_( 1, 0 ), MAX_( 2, 17 ) } },
		{ "+y", low_wall, { P_( 0, 0 ), C_( -128 ), MAX_( 2, 25 ) } },
		{ "+z", big_top, { P_( 0, 96 ), P_( 1, 0 ), C_( 96 ) } },
	};
	lit_t high[] =
	{
		{ "-x", east_wall, { C_( -1440 ), P_( 1, 0 ), CLAMP_( 2, 129, 159 ) } },
		{ "+y", high_wall, { P_( 0, 0 ), C_( -128 ), MIN_( 2, 127 ) } },
		{ "+z", big_top, { P_( 0, 0 ), P_( 1, 48 ), C_( 96 ) } },
	};
	models[0] = add_crate( -1584, -128, 16, LIT( low ), &tex );
	models[1] = add_crate( -1488, -128, 96, LIT( high ), &tex );
}

static int add_long_crate( void )
{
	crate_textures_t tex = crate_textures();
	sampler_t medium_east = light_sampler( find_face( "SandCrtSmSd", 1, 0, 0, 960, 1184, 32 ));
	sampler_t medium_south = light_sampler( find_face( "SandCrtSmSd", 0, -1, 0, 928, 1152, 32 ));
	sampler_t medium_top = light_sampler( find_face( "SandCrtSmTp", 0, 0, 1, 928, 1184, 64 ));
	lit_t lit[] =
	{
		{ "+x", medium_east, { C_( 960 ), P_( 1, 0 ), P_( 2, 0 ) } },
		{ "-y", medium_south, { P_( 0, -64 ), C_( 1152 ), P_( 2, 0 ) } },
		{ "+z", medium_top, { P_( 0, -48 ), P_( 1, 0 ), C_( 64 ) } },
	};
	return add_crate( 960, 1168, 0, LIT( lit ), &tex );
}

int main( int argc, char **argv )
{
	int models[6];

	if( argc != 3 )
	{
		fprintf( stderr, "usage: patch_dust2 <Steam cstrike/maps/de_dust2.bsp> <out de_dust2.bsp>\n" );
		return 2;
	}
	load( argv[1] );
	add_mid_crates( models );
	models[2] = add_elevator_crate();
	add_t_crates( models + 3 );
	models[5] = add_long_crate();
	save( argv[2] );
	printf( "patch_dust2: the crates as *%d, *%d, *%d, *%d, *%d, *%d -> %s\n",
		models[0], models[1], models[2], models[3], models[4], models[5], argv[2] );
	return 0;
}
