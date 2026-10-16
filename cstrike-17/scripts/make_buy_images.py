#!/usr/bin/env python3
# make_buy_images.py - renders gfx/vgui/molotov.tga and incgrenade.tga, the
# buy menu pictures of the molotov and the incendiary grenade (story 054),
# 256x128 with alpha as Valve's: the game's own models
# (make_molotov_models.py, their meshes and textures) seen as Valve's
# grenades are, from the side, leaning up to the right, lit from the top
# left, supersampled.
#
#   python make_buy_images.py <gfx/vgui directory> [preview.png]
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert( 0, os.path.dirname( os.path.abspath( __file__ )))
import make_molotov_models as models

W, H = 256, 128
SS = 3            # supersampling
FILL = 0.80       # of the picture's height the object takes, as Valve's grenades
LEAN = 38.0       # degrees from upright, its top to the right
LIGHT = np.array([ -0.45, 0.65, 0.62 ])

def rotation( yaw, lean, tilt ):
	# turn about the object's axis (y), tip it towards the camera, lean it
	cy, sy = math.cos( yaw ), math.sin( yaw )
	ct, st = math.cos( tilt ), math.sin( tilt )
	cl, sl = math.cos( lean ), math.sin( lean )
	Ry = np.array([[ cy, 0, sy ], [ 0, 1, 0 ], [ -sy, 0, cy ]])
	Rx = np.array([[ 1, 0, 0 ], [ 0, ct, -st ], [ 0, st, ct ]])
	Rz = np.array([[ cl, sl, 0 ], [ -sl, cl, 0 ], [ 0, 0, 1 ]])
	return Rz @ Rx @ Ry

def render( mesh, texture, yaw, tilt ):
	verts, norms, uvs, tris = mesh
	verts = np.asarray( verts, float )
	norms = np.asarray( norms, float )
	uvs = np.asarray( uvs, float )
	tex = np.asarray( texture.convert( 'RGB' ), float ) / 255.0
	th, tw = tex.shape[:2]

	R = rotation( yaw, math.radians( LEAN ), tilt )
	v = ( verts - verts.mean( 0 )) @ R.T
	n = norms @ R.T

	w, h = W * SS, H * SS
	extent = max(( v[:, 1].max() - v[:, 1].min()) / ( FILL * h ), ( v[:, 0].max() - v[:, 0].min()) / ( 0.92 * w ))
	cx, cy = ( v[:, 0].max() + v[:, 0].min()) / 2, ( v[:, 1].max() + v[:, 1].min()) / 2
	sx = ( v[:, 0] - cx ) / extent + w / 2
	sy = h / 2 - ( v[:, 1] - cy ) / extent

	color = np.zeros(( h, w, 3 ))
	alpha = np.zeros(( h, w ))
	zbuf = np.full(( h, w ), -1e9 )
	L = LIGHT / np.linalg.norm( LIGHT )
	V = np.array([ 0.0, 0.0, 1.0 ])
	Hv = ( L + V ) / np.linalg.norm( L + V )

	for a, b, c in tris:
		xs = np.array([ sx[a], sx[b], sx[c] ]); ys = np.array([ sy[a], sy[b], sy[c] ])
		x0, x1 = int( max( 0, xs.min())), int( min( w - 1, xs.max() + 1 ))
		y0, y1 = int( max( 0, ys.min())), int( min( h - 1, ys.max() + 1 ))
		if x1 < x0 or y1 < y0:
			continue
		d = ( ys[1] - ys[2] ) * ( xs[0] - xs[2] ) + ( xs[2] - xs[1] ) * ( ys[0] - ys[2] )
		if abs( d ) < 1e-9:
			continue
		X, Y = np.meshgrid( np.arange( x0, x1 + 1 ) + 0.5, np.arange( y0, y1 + 1 ) + 0.5 )
		w0 = (( ys[1] - ys[2] ) * ( X - xs[2] ) + ( xs[2] - xs[1] ) * ( Y - ys[2] )) / d
		w1 = (( ys[2] - ys[0] ) * ( X - xs[2] ) + ( xs[0] - xs[2] ) * ( Y - ys[2] )) / d
		w2 = 1 - w0 - w1
		inside = ( w0 >= -1e-4 ) & ( w1 >= -1e-4 ) & ( w2 >= -1e-4 )
		if not inside.any():
			continue
		z = w0 * v[a, 2] + w1 * v[b, 2] + w2 * v[c, 2]
		zs = zbuf[y0:y1 + 1, x0:x1 + 1]
		ok = inside & ( z > zs )
		if not ok.any():
			continue
		uu = w0 * uvs[a, 0] + w1 * uvs[b, 0] + w2 * uvs[c, 0]
		vv = w0 * uvs[a, 1] + w1 * uvs[b, 1] + w2 * uvs[c, 1]
		nn = w0[..., None] * n[a] + w1[..., None] * n[b] + w2[..., None] * n[c]
		nn /= np.linalg.norm( nn, axis = -1, keepdims = True ) + 1e-9
		# both sides of the thin parts (the lever, the rag)
		nn = np.where(( nn @ V )[..., None] < 0, -nn, nn )
		albedo = tex[np.clip(( vv * ( th - 1 )).astype( int ), 0, th - 1 ), np.clip(( uu * ( tw - 1 )).astype( int ), 0, tw - 1 )]
		diffuse = np.clip( nn @ L, 0, 1 )
		spec = np.clip( nn @ Hv, 0, 1 ) ** 28
		rim = ( 1 - np.clip( nn @ V, 0, 1 )) ** 3
		shade = albedo * ( 0.30 + 0.85 * diffuse )[..., None] + ( 0.35 * spec + 0.10 * rim )[..., None]
		zs[ok] = z[ok]
		color[y0:y1 + 1, x0:x1 + 1][ok] = np.clip( shade[ok], 0, 1 )
		alpha[y0:y1 + 1, x0:x1 + 1][ok] = 1.0

	# down to the picture's size, colour weighted by coverage
	rgb = ( color * alpha[..., None] ).reshape( H, SS, W, SS, 3 ).mean(( 1, 3 ))
	a = alpha.reshape( H, SS, W, SS ).mean(( 1, 3 ))
	rgb = rgb / np.maximum( a, 1e-6 )[..., None]
	out = np.dstack(( rgb, a ))
	return Image.fromarray(( np.clip( out, 0, 1 ) * 255 ).astype( np.uint8 ), 'RGBA' )

def main():
	out = sys.argv[1]
	pictures = {}
	# the stencils face the camera (u 0.25 of the body's texture: +z), the
	# lever to the side, tipped a little to show the top
	for kind, name, yaw in (( 'molotov', 'molotov.tga', 0.35 ), ( 'incgrenade', 'incgrenade.tga', -1.1 )):
		k = models.KINDS[kind]
		img = render( k['mesh'](), k['texture'](), yaw, math.radians( 12 ))
		img.save( '%s/%s' % ( out, name ))
		pictures[name] = img

	if len( sys.argv ) > 2:
		sheet = Image.new( 'RGBA', ( W * 2 + 8, H ), ( 60, 70, 55, 255 ))
		for i, img in enumerate( pictures.values()):
			sheet.alpha_composite( img, ( i * ( W + 8 ), 0 ))
		sheet.resize(( sheet.width * 2, sheet.height * 2 ), Image.LANCZOS ).save( sys.argv[2] )

if __name__ == '__main__':
	main()
