#!/usr/bin/env python3
# make_throwing_knife.py - writes the thrown knife of stories 062 and 063,
# w_throwknife.mdl: a combat knife made here from nothing, lying flat along
# +x (the tip forward), its balance point on the origin and its underside
# on z 0, so that it flies point first on the entity's angles and lies on
# the ground where the entity rests. Valve's w_knife.mdl is a knife held by
# a hand 50 units above its origin, upright: drawn in flight and on the
# ground it floated over the player's head.
#
#   python make_throwing_knife.py <models directory> [preview.png]
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert( 0, os.path.dirname( os.path.abspath( __file__ )))
from make_molotov_models import write_mdl, fractal, value_noise

ITEX = 1024

# lengths in units (an inch each): a blade of 7, a guard, a handle of 4.6
TIP_X = 7.6
GUARD_X0, GUARD_X1 = -0.15, 0.25
HANDLE_X0, HANDLE_X1 = -4.6, -0.15
BALANCE_X = 0.6 # moved onto the origin

# texture regions (v): the blade's top half, the handle, the guard and pommel
V_BLADE = ( 0.0, 0.48 )
V_HANDLE = ( 0.52, 0.86 )
V_METAL = ( 0.88, 1.0 )

def loft( rings, us, v0, v1, cap_start = True ):
	# rings: [(x, [(y, z), ...])] closed sections along x; us: the u of each
	# section point (and one more to close it)
	verts, norms, uvs, tris = [], [], [], []
	n = len( rings[0][1] )
	xs = [ r[0] for r in rings ]
	for i, ( x, pts ) in enumerate( rings ):
		v = v0 + ( v1 - v0 ) * ( x - xs[0] ) / ( xs[-1] - xs[0] )
		cy = sum( p[0] for p in pts ) / n
		cz = sum( p[1] for p in pts ) / n
		for j in range( n + 1 ):
			y, z = pts[j % n]
			verts.append(( x, y, z ))
			d = np.array([ 0.0, y - cy, z - cz ])
			ln = np.linalg.norm( d )
			norms.append( tuple( d / ln ) if ln > 1e-6 else ( 1.0, 0.0, 0.0 ))
			uvs.append(( us[j], v ))
	for i in range( len( rings ) - 1 ):
		for j in range( n ):
			a = i * ( n + 1 ) + j
			b = a + n + 1
			tris.append(( a, a + 1, b + 1 ))
			tris.append(( a, b + 1, b ))
	if cap_start:
		c = len( verts )
		x, pts = rings[0]
		verts.append(( x, sum( p[0] for p in pts ) / n, sum( p[1] for p in pts ) / n ))
		norms.append(( -1.0, 0.0, 0.0 ))
		uvs.append(( 0.5, v0 ))
		for j in range( n ):
			tris.append(( c, j + 1, j ))
	return verts, norms, uvs, tris

def box( x0, x1, y0, y1, z0, z1, v0, v1 ):
	pts = [( y0, z0 ), ( y1, z0 ), ( y1, z1 ), ( y0, z1 )]
	return loft([( x0, pts ), ( x1, pts )], [ 0, 0.25, 0.5, 0.75, 1.0 ], v0, v1 )

def merge( parts ):
	verts, norms, uvs, tris = [], [], [], []
	for v, n, u, t in parts:
		o = len( verts )
		verts += v; norms += n; uvs += u
		tris += [( a + o, b + o, c + o ) for a, b, c in t ]
	return np.array( verts, float ), np.array( norms, float ), uvs, tris

def knife_mesh():
	# the blade: the edge along -y, the spine along +y, a flat grind to the
	# edge, a clip towards the tip; sections of six points
	rings = []
	steps = 18
	for i in range( steps + 1 ):
		t = i / steps
		x = GUARD_X1 + ( TIP_X - GUARD_X1 ) * t
		w = 0.62 * ( 1 - t ** 2.2 ) + 0.02          # half its width
		spine = 0.55 * ( 1 - max( 0.0, ( t - 0.62 ) / 0.38 ) ** 1.4 ) + 0.02
		c = ( spine - w ) * 0.5 * t                 # the tip rises towards the spine
		th = 0.09 * ( 1 - t ) + 0.012               # half its thickness
		pts = [
			( -w + c, 0.0 ),                        # the edge
			( -w * 0.15 + c, -th ),                 # grind line, under
			( spine * 0.9 + c * 0.2, -th * 0.9 ),   # spine, under
			( spine + c * 0.2, 0.0 ),               # spine
			( spine * 0.9 + c * 0.2, th * 0.9 ),    # spine, over
			( -w * 0.15 + c, th ),                  # grind line, over
		]
		rings.append(( x, pts ))
	blade = loft( rings, [ 0.0, 0.18, 0.40, 0.5, 0.60, 0.82, 1.0 ], *V_BLADE )

	# the guard and the pommel, dark steel
	guard = box( GUARD_X0, GUARD_X1, -0.95, 0.8, -0.22, 0.22, *V_METAL )
	pommel = box( HANDLE_X0 - 0.35, HANDLE_X0, -0.5, 0.42, -0.3, 0.3, *V_METAL )

	# the handle: a rounded section, swelling in the palm, grooves for the
	# fingers on the edge's side
	hr = []
	for i in range( 25 ):
		t = i / 24
		x = HANDLE_X0 + ( HANDLE_X1 - HANDLE_X0 ) * t
		hw = 0.42 + 0.08 * math.sin( math.pi * t )
		groove = 0.06 * max( 0.0, math.sin( t * math.pi * 4 )) if t > 0.45 else 0.0
		ht = 0.34 + 0.04 * math.sin( math.pi * t )
		pts = []
		for k in range( 10 ):
			a = 2 * math.pi * k / 10
			y = hw * math.cos( a )
			z = ht * math.sin( a )
			if y < 0:
				y += groove
			pts.append(( y - 0.02, z ))
		hr.append(( x, pts ))
	handle = loft( hr, [ k / 10 for k in range( 11 )], *V_HANDLE, cap_start = False )

	v, n, u, t = merge([ blade, guard, pommel, handle ])
	v[:, 0] -= BALANCE_X
	v[:, 2] -= v[:, 2].min()   # the underside on the ground
	return v, n, u, t

def knife_texture():
	S = ITEX
	rng = np.random.default_rng( 62 )
	px = np.zeros(( S, S, 3 ))
	u = np.arange( S )[None, :] / S

	# the blade: satin steel brushed along its length, the grind brighter,
	# a bright edge
	r0, r1 = int( V_BLADE[0] * S ), int( V_BLADE[1] * S )
	rows = r1 - r0
	brush = value_noise( rng, rows, S, 3 ).mean( 0, keepdims = True )
	streak = np.repeat( value_noise( rng, 1, S, 2 ), rows, 0 )
	grain = fractal( rng, rows, S, [ 64, 20, 6 ])
	base = 0.50 + 0.10 * ( streak - 0.5 ) + 0.06 * ( grain - 0.5 )
	grind = ( np.abs( u - 0.09 ) < 0.09 ) | ( np.abs( u - 0.91 ) < 0.09 )
	edge = ( u < 0.02 ) | ( u > 0.98 )
	spine = np.abs( u - 0.5 ) < 0.1
	lum = base * np.ones(( rows, 1 ))
	lum = np.where( grind, lum * 1.25, lum )
	lum = np.where( edge, 0.85, lum )
	lum = np.where( spine, lum * 0.8, lum )
	# a blackened finish on the flat, worn bright on the grind
	steel = np.array([ 0.86, 0.88, 0.90 ])
	dark = np.array([ 0.20, 0.21, 0.22 ])
	flat = ~( grind | edge )
	col = np.where( flat[..., None], dark[None, None, :] * ( 0.8 + 0.4 * grain )[..., None], steel[None, None, :] * lum[..., None] )
	# scratches through the coating
	for _ in range( 120 ):
		r = int( rng.integers( 0, rows )); c = int( rng.integers( 0, S ))
		ln = int( rng.integers( 6, 40 ))
		for k in range( ln ):
			rr = r + k
			if rr < rows:
				col[rr, c] = col[rr, c] * 0.4 + steel * 0.6 * 0.8
	px[r0:r1] = col

	# the handle: black polymer, a fine grip pattern
	h0, h1 = int( V_HANDLE[0] * S ), int( V_HANDLE[1] * S )
	hrows = h1 - h0
	y = np.arange( hrows )[:, None]
	x = np.arange( S )[None, :]
	diamond = (( np.sin( ( x + y ) * 0.35 ) > 0.6 ) | ( np.sin( ( x - y ) * 0.35 ) > 0.6 ))
	g = fractal( rng, hrows, S, [ 40, 10, 3 ])
	hl = 0.11 + 0.05 * g + 0.04 * diamond
	px[h0:h1] = np.array([ 1.0, 1.0, 1.05 ])[None, None, :] * hl[..., None]

	# guard and pommel: dark parkerized steel
	m0 = int( V_METAL[0] * S )
	mrows = S - m0
	gm = fractal( rng, mrows, S, [ 24, 8, 3 ])
	px[m0:] = np.array([ 0.24, 0.24, 0.23 ])[None, None, :] * ( 0.8 + 0.4 * gm )[..., None]
	px[r1:h0] = px[r1 - 1]
	px[h1:m0] = px[h1 - 1]
	return Image.fromarray(( np.clip( px, 0, 1 ) * 255 ).astype( np.uint8 ))

KINDS = {
	'throwknife': dict( mesh = knife_mesh, texture = knife_texture ),
}

def main():
	out = sys.argv[1]
	mesh = knife_mesh()
	tex = knife_texture()
	write_mdl( f'{out}/w_throwknife.mdl', 'w_throwknife.mdl', [( 'root', -1, ( 0, 0, 0, 0, 0, 0 ))], 0, mesh, tex, [ 'idle' ])
	if len( sys.argv ) > 2:
		tex.resize(( 512, 512 )).save( sys.argv[2] )

if __name__ == '__main__':
	main()
