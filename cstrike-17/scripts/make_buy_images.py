#!/usr/bin/env python3
# make_buy_images.py - renders gfx/vgui/molotov.tga and incgrenade.tga, the
# buy menu pictures of the molotov and the incendiary grenade (story 054),
# 256x128 with alpha as Valve's: a small ray marcher over the shapes of the
# two grenades, lit from the top left.
#
#   python make_buy_images.py <gfx/vgui directory> [preview.png]
import sys

import numpy as np
from PIL import Image

W, H = 256, 128
SS = 3

def normalize( v ):
	return v / np.maximum( np.linalg.norm( v, axis = -1, keepdims = True ), 1e-9 )

def rot_z( a ):
	c, s = np.cos( a ), np.sin( a )
	return np.array([[ c, -s, 0 ], [ s, c, 0 ], [ 0, 0, 1 ]])

def rot_x( a ):
	c, s = np.cos( a ), np.sin( a )
	return np.array([[ 1, 0, 0 ], [ 0, c, -s ], [ 0, s, c ]])

def lathe( p, ys, rs ):
	# a solid of revolution around y: the radius rs at the heights ys
	q = np.sqrt( p[..., 0] ** 2 + p[..., 2] ** 2 )
	y = p[..., 1]
	r = np.interp( y, ys, rs )
	d = q - r
	d = np.maximum( d, ys[0] - y )
	d = np.maximum( d, y - ys[-1] )
	return d * 0.7

def capsule( p, a, b, r ):
	pa, ba = p - a, b - a
	h = np.clip(( pa * ba ).sum( -1 ) / ( ba * ba ).sum(), 0, 1 )
	return np.linalg.norm( pa - ba * h[..., None], axis = -1 ) - r

def torus_y( p, c, R, r ):
	q = p - c
	x = np.sqrt( q[..., 0] ** 2 + q[..., 2] ** 2 ) - R
	return np.sqrt( x * x + q[..., 1] ** 2 ) - r

def box( p, c, b ):
	q = np.abs( p - c ) - b
	return np.linalg.norm( np.maximum( q, 0 ), axis = -1 ) + np.minimum( q.max( -1 ), 0 )

def wobble( p, f, a ):
	return a * ( np.sin( f * p[..., 0] + 1.3 ) * np.sin( f * 1.3 * p[..., 1] + 0.4 ) * np.sin( f * 0.9 * p[..., 2] + 2.1 ))

# --- the molotov: a bottle with a rag in its neck and around it ---------------

BOTTLE_Y = np.array([ 0.0, 0.05, 2.6, 2.95, 3.35, 3.65, 4.35, 4.40, 4.55 ])
BOTTLE_R = np.array([ 0.88, 0.95, 0.95, 0.80, 0.45, 0.33, 0.30, 0.36, 0.34 ])

def molotov( p ):
	glass = lathe( p, BOTTLE_Y, BOTTLE_R )
	rag_top = capsule( p, np.array([ 0, 4.3, 0 ]), np.array([ 0.25, 5.2, -0.1 ]), 0.26 ) + wobble( p, 5, 0.08 )
	rag_wrap = torus_y( p, np.array([ 0, 3.85, 0 ]), 0.36, 0.13 ) + wobble( p, 11, 0.03 )
	rag_tail = capsule( p, np.array([ 0.2, 3.8, -0.30 ]), np.array([ 0.55, 2.4, -0.95 ]), 0.12 ) + wobble( p, 8, 0.04 )
	rag = np.minimum( np.minimum( rag_top, rag_wrap ), rag_tail )
	return np.minimum( glass, rag ), np.where( rag < glass, 1, 0 )

def molotov_color( p, mat, n, v ):
	y = p[..., 1]
	# glass: brown, darker where it is seen through (the facing middle),
	# the liquid below its surface at y 2.1
	facing = np.abs(( n * v ).sum( -1 ))
	glass = np.stack([ 0.42, 0.22, 0.07 ], -1 ) * ( 0.55 + 0.45 * ( 1 - facing ))[..., None]
	liquid = np.where( y < 2.7, 1.0, 0.65 )[..., None]
	glass = glass * liquid
	label = (( y > 0.8 ) & ( y < 1.9 ))[..., None]
	glass = np.where( label, np.stack([ 0.75, 0.70, 0.55 ], -1 ) * ( 0.5 + 0.5 * ( 1 - facing ))[..., None], glass )
	cloth = np.stack([ 0.80, 0.74, 0.62 ], -1 ) * ( 0.75 + 0.25 * np.sin( 25 * p[..., 0] + 17 * p[..., 1] ))[..., None]
	soot = np.clip(( p[..., 1] - 4.8 ) * 2.5, 0, 1 )[..., None]
	cloth = cloth * ( 1 - 0.7 * soot )
	shiny = np.where( mat == 0, 1.0, 0.1 )
	return np.where(( mat == 1 )[..., None], cloth, glass ), shiny

# --- the incendiary grenade: a canister, its fuse, lever and pin -------------

CAN_Y = np.array([ 0.0, 0.08, 2.15, 2.30, 2.55, 2.62 ])
CAN_R = np.array([ 0.58, 0.64, 0.64, 0.55, 0.30, 0.26 ])

def incendiary( p ):
	can = lathe( p, CAN_Y, CAN_R )
	fuse = lathe( p, np.array([ 2.55, 2.62, 2.95, 3.0 ]), np.array([ 0.20, 0.24, 0.24, 0.18 ]))
	# the lever lies along the canister, held by the fuse at its top
	lever = box( p, np.array([ 0, 1.85, -0.68 ]), np.array([ 0.13, 0.95, 0.04 ]))
	lever = np.minimum( lever, box( p, np.array([ 0, 2.9, -0.45 ]), np.array([ 0.13, 0.05, 0.25 ])))
	ring = torus_y( p[..., [ 1, 0, 2 ]], np.array([ 2.75, 0.32, -0.25 ]), 0.20, 0.035 )
	metal = np.minimum( np.minimum( fuse, lever ), ring )
	return np.minimum( can, metal ), np.where( metal < can, 1, 0 )

def incendiary_color( p, mat, n, v ):
	y = p[..., 1]
	body = np.stack([ 0.40, 0.42, 0.40 ], -1 ) * np.ones_like( y )[..., None]
	band = (( y > 1.65 ) & ( y < 1.95 )) | (( y > 0.25 ) & ( y < 0.4 ))
	body = np.where( band[..., None], np.stack([ 0.85, 0.32, 0.06 ], -1 ), body )
	text = ( y > 0.8 ) & ( y < 1.3 ) & ( np.abs( p[..., 0] - 0.25 ) < 0.25 ) & ( p[..., 2] < 0 ) & ( np.sin( 40 * p[..., 0] ) > 0.2 )
	body = np.where( text[..., None], np.stack([ 0.85, 0.80, 0.70 ], -1 ), body )
	steel = np.stack([ 0.62, 0.62, 0.64 ], -1 ) * np.ones_like( y )[..., None]
	return np.where(( mat == 1 )[..., None], steel, body ), np.where( mat == 1, 0.9, 0.5 )

def render( sdf, color, angle, tilt, scale, center ):
	w, h = W * SS, H * SS
	ys, xs = np.mgrid[ 0:h, 0:w ]
	u = ( xs - w / 2 ) / ( h / 2 ) * scale
	v = -( ys - h / 2 ) / ( h / 2 ) * scale
	R = rot_x( tilt ) @ rot_z( angle )
	Ri = R.T
	ro = np.stack([ u, v, np.full_like( u, -10.0 )], -1 ) @ Ri.T + center
	rd = np.broadcast_to( np.array([ 0, 0, 1.0 ]) @ Ri.T, ro.shape )
	t = np.zeros( u.shape )
	hit = np.zeros( u.shape, bool )
	for _ in range( 160 ):
		p = ro + rd * t[..., None]
		d, _ = sdf( p )
		hit |= d < 1e-3
		t = np.where( hit, t, t + np.maximum( d, 2e-3 ))
		if ( t > 20 ).all():
			break
	hit &= t < 20
	p = ro + rd * t[..., None]
	e = 1e-3
	n = np.stack([ sdf( p + np.array([ e, 0, 0 ]))[0] - sdf( p - np.array([ e, 0, 0 ]))[0],
		sdf( p + np.array([ 0, e, 0 ]))[0] - sdf( p - np.array([ 0, e, 0 ]))[0],
		sdf( p + np.array([ 0, 0, e ]))[0] - sdf( p - np.array([ 0, 0, e ]))[0] ], -1 )
	n = normalize( n )
	_, mat = sdf( p )
	view = -rd
	light = normalize( np.array([ -0.6, 0.7, -0.5 ]) @ Ri.T )
	base, shiny = color( p, mat, n, view )
	diff = np.clip(( n * light ).sum( -1 ), 0, 1 )
	half = normalize( light + view )
	spec = np.clip(( n * half ).sum( -1 ), 0, 1 ) ** 40 * shiny
	rim = ( 1 - np.clip(( n * view ).sum( -1 ), 0, 1 )) ** 3 * 0.25
	rgb = base * ( 0.30 + 0.85 * diff )[..., None] + ( spec + rim )[..., None]
	rgba = np.concatenate([ np.clip( rgb, 0, 1 ), hit[..., None].astype( float )], -1 )
	rgba[..., :3] *= rgba[..., 3:]
	im = ( rgba.reshape( H, SS, W, SS, 4 ).mean(( 1, 3 )))
	a = im[..., 3:]
	im[..., :3] = np.where( a > 0, im[..., :3] / np.maximum( a, 1e-6 ), 0 )
	return Image.fromarray(( im * 255 + 0.5 ).astype( np.uint8 ), 'RGBA' )

def main():
	out = sys.argv[1]
	mol = render( molotov, molotov_color, np.radians( -62 ), np.radians( -10 ), 1.65, np.array([ 0, 2.6, 0 ]))
	inc = render( incendiary, incendiary_color, np.radians( -62 ), np.radians( -10 ), 1.25, np.array([ 0, 1.5, 0 ]))
	mol.save( out + '/molotov.tga' )
	inc.save( out + '/incgrenade.tga' )
	if len( sys.argv ) > 2:
		sheet = Image.new( 'RGBA', ( W, H * 2 + 8 ), ( 40, 40, 40, 255 ))
		sheet.alpha_composite( mol, ( 0, 0 ))
		sheet.alpha_composite( inc, ( 0, H + 8 ))
		sheet.resize(( W * 2, ( H * 2 + 8 ) * 2 ), Image.NEAREST ).save( sys.argv[2] )

if __name__ == '__main__':
	main()
