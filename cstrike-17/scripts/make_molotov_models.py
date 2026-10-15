#!/usr/bin/env python3
# make_molotov_models.py - writes the models of the molotov and of the
# incendiary grenade (story 054), GoldSrc studio models (version 10) made
# here from nothing: a bottle with a rag, a canister with its lever and pin,
# their textures painted by this script.
#
#   python make_molotov_models.py <models directory> [preview.png]
#
#   w_molotov.mdl, w_incgrenade.mdl          on the ground and thrown
#   p_molotov.mdl, p_incgrenade.mdl          in the hand of the players
#   v_molotov_part.mdl, v_incgrenade_part.mdl
#       in the view: the client draws the arms of Valve's v_smokegrenade.mdl
#       without its grenade, then this part on the grenade's bone (Bone02)
#
# Placement: the numbers below put the new objects where the smoke grenade
# is in Valve's models (its bone, its axis, its size), read from them once.
import math
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

TEX = 256

# --- shapes -----------------------------------------------------------------

def lathe( ys, rs, segs, v0, v1, cap_bottom = True ):
	# a surface of revolution around +y: rings of vertices at the heights
	# ys and radii rs; texture: u around, v from v1 (bottom) to v0 (top)
	verts, norms, uvs, tris = [], [], [], []
	n = len( ys )
	for i in range( n ):
		# the slope of the profile for the normals
		a, b = max( 0, i - 1 ), min( n - 1, i + 1 )
		dy, dr = ys[b] - ys[a], rs[b] - rs[a]
		for k in range( segs + 1 ):
			t = 2 * math.pi * k / segs
			c, s = math.cos( t ), math.sin( t )
			verts.append(( rs[i] * c, ys[i], rs[i] * s ))
			nn = np.array([ dy * c, -dr, dy * s ])
			nn /= np.linalg.norm( nn ) + 1e-9
			norms.append( tuple( nn ))
			uvs.append(( k / segs, v1 + ( v0 - v1 ) * ( ys[i] - ys[0] ) / ( ys[-1] - ys[0] )))
	for i in range( n - 1 ):
		for k in range( segs ):
			a = i * ( segs + 1 ) + k
			b = a + segs + 1
			tris.append(( a, a + 1, b + 1 ))
			tris.append(( a, b + 1, b ))
	if cap_bottom:
		c = len( verts )
		verts.append(( 0, ys[0], 0 )); norms.append(( 0, -1, 0 )); uvs.append(( 0.5, v1 ))
		for k in range( segs ):
			tris.append(( c, k + 1, k ))
	return verts, norms, uvs, tris

def tube( points, radius, segs, v0, v1, u0 = 0.0, u1 = 1.0, wobble = 0.0, seed = 0 ):
	# a tube along a polyline (the rag), closed at its far end
	rng = np.random.default_rng( seed )
	pts = [ np.array( p, float ) for p in points ]
	verts, norms, uvs, tris = [], [], [], []
	up = np.array([ 0.0, 0.0, 1.0 ])
	for i, p in enumerate( pts ):
		d = pts[min( i + 1, len( pts ) - 1 )] - pts[max( i - 1, 0 )]
		d /= np.linalg.norm( d )
		side = np.cross( d, up )
		if np.linalg.norm( side ) < 1e-3:
			side = np.cross( d, np.array([ 1.0, 0, 0 ]))
		side /= np.linalg.norm( side )
		up2 = np.cross( side, d )
		r = radius[i] if hasattr( radius, '__len__' ) else radius
		for k in range( segs + 1 ):
			t = 2 * math.pi * k / segs
			nn = side * math.cos( t ) + up2 * math.sin( t )
			rr = r * ( 1 + wobble * ( rng.random() - 0.5 )) if k < segs else None
			if k == segs:
				rr = r * ( 1 + 0 )
			verts.append( tuple( p + nn * rr ))
			norms.append( tuple( nn ))
			uvs.append(( u0 + ( u1 - u0 ) * k / segs, v0 + ( v1 - v0 ) * i / ( len( pts ) - 1 )))
	for i in range( len( pts ) - 1 ):
		for k in range( segs ):
			a = i * ( segs + 1 ) + k
			b = a + segs + 1
			tris.append(( a, b + 1, a + 1 ))
			tris.append(( a, b, b + 1 ))
	return verts, norms, uvs, tris

def box( center, half, v0, v1, u0, u1 ):
	cx, cy, cz = center
	hx, hy, hz = half
	verts, norms, uvs, tris = [], [], [], []
	faces = [
		(( 1, 0, 0 ), [( 1, -1, -1 ), ( 1, 1, -1 ), ( 1, 1, 1 ), ( 1, -1, 1 )]),
		(( -1, 0, 0 ), [( -1, -1, 1 ), ( -1, 1, 1 ), ( -1, 1, -1 ), ( -1, -1, -1 )]),
		(( 0, 1, 0 ), [( -1, 1, -1 ), ( -1, 1, 1 ), ( 1, 1, 1 ), ( 1, 1, -1 )]),
		(( 0, -1, 0 ), [( -1, -1, 1 ), ( -1, -1, -1 ), ( 1, -1, -1 ), ( 1, -1, 1 )]),
		(( 0, 0, 1 ), [( -1, -1, 1 ), ( 1, -1, 1 ), ( 1, 1, 1 ), ( -1, 1, 1 )]),
		(( 0, 0, -1 ), [( -1, 1, -1 ), ( 1, 1, -1 ), ( 1, -1, -1 ), ( -1, -1, -1 )]),
	]
	for n, quad in faces:
		base = len( verts )
		for j, ( sx, sy, sz ) in enumerate( quad ):
			verts.append(( cx + sx * hx, cy + sy * hy, cz + sz * hz ))
			norms.append( n )
			uvs.append(( u0 if j in ( 0, 3 ) else u1, v0 if j < 2 else v1 ))
		tris.append(( base, base + 1, base + 2 ))
		tris.append(( base, base + 2, base + 3 ))
	return verts, norms, uvs, tris

def merge( parts ):
	verts, norms, uvs, tris = [], [], [], []
	for v, n, u, t in parts:
		o = len( verts )
		verts += v; norms += n; uvs += u
		tris += [( a + o, b + o, c + o ) for a, b, c in t ]
	return np.array( verts, float ), np.array( norms, float ), np.array( uvs, float ), tris

# the molotov: a brown bottle, a rag in its neck, tied around it and hanging
# (the profile of make_buy_images.py)
BOTTLE_Y = [ 0.0, 0.05, 0.9, 1.8, 2.6, 2.95, 3.35, 3.65, 4.35, 4.40, 4.55 ]
BOTTLE_R = [ 0.88, 0.95, 0.95, 0.95, 0.95, 0.80, 0.45, 0.33, 0.30, 0.36, 0.34 ]

def molotov_mesh():
	bottle = lathe( BOTTLE_Y, BOTTLE_R, 14, 0.0, 0.62 )
	plug = tube([( 0, 4.2, 0 ), ( 0.05, 4.7, -0.03 ), ( 0.18, 5.05, -0.08 ), ( 0.28, 5.3, -0.1 )], [ 0.28, 0.27, 0.22, 0.12 ], 7, 0.66, 0.80, 0.0, 0.5, 0.25, 1 )
	ring = []
	for i in range( 13 ):
		t = 2 * math.pi * i / 12
		ring.append(( 0.40 * math.cos( t ), 3.85 + 0.04 * math.sin( 3 * t ), 0.40 * math.sin( t )))
	wrap = tube( ring, 0.12, 6, 0.80, 0.86, 0.0, 1.0, 0.2, 2 )
	tail = tube([( 0.25, 3.8, -0.33 ), ( 0.55, 3.3, -0.75 ), ( 0.80, 2.8, -0.95 ), ( 0.95, 2.35, -1.0 )], [ 0.13, 0.12, 0.11, 0.06 ], 6, 0.86, 1.0, 0.5, 1.0, 0.25, 3 )
	return merge([ bottle, plug, wrap, tail ])

def molotov_texture():
	# top: the bottle unwrapped (v 0 = its neck, 0.62 = its base), bottom:
	# the cloth of the rag
	im = Image.new( 'RGB', ( TEX, TEX ))
	px = np.zeros(( TEX, TEX, 3 ))
	y = np.arange( TEX )[:, None] / TEX
	x = np.arange( TEX )[None, :] / TEX
	h = 4.55 * ( 1 - y / 0.62 ) # height on the bottle
	glass = np.array([ 0.36, 0.17, 0.05 ])
	# light through the glass: brighter at its edges, a highlight streak
	streak = np.exp( -(( x - 0.30 ) / 0.035 ) ** 2 ) * 0.9 + np.exp( -(( x - 0.80 ) / 0.06 ) ** 2 ) * 0.25
	shade = 0.75 + 0.25 * np.cos( 2 * math.pi * ( x - 0.3 ))
	col = glass[None, None, :] * shade[..., None] * np.ones(( TEX, 1, 1 ))
	liquid = ( h < 2.7 )[..., None] * np.ones(( 1, TEX, 1 ))
	col = np.where( liquid > 0, col * 1.25 + np.array([ 0.06, 0.03, 0 ]), col * 0.8 )
	label = (( h > 0.8 ) & ( h < 1.9 ))[..., None] * np.ones(( 1, TEX, 1 ))
	paper = np.array([ 0.78, 0.72, 0.56 ]) * ( 0.85 + 0.15 * np.cos( 2 * math.pi * ( x - 0.3 )))[..., None]
	col = np.where( label > 0, paper, col )
	col = col + streak[..., None] * np.array([ 0.9, 0.85, 0.75 ])
	px[:] = col
	# the label: a red band, rows of "text"
	lab = ( y > ( 1 - 1.9 / 4.55 ) * 0.62 ) & ( y < ( 1 - 0.8 / 4.55 ) * 0.62 )
	px[( y > ( 1 - 1.75 / 4.55 ) * 0.62 ) & ( y < ( 1 - 1.62 / 4.55 ) * 0.62 ) & ( x < 0.5 )[..., None].squeeze()] = [ 0.6, 0.1, 0.07 ]
	rng = np.random.default_rng( 7 )
	for row in range( 5 ):
		yy = int((( 1 - ( 1.5 - row * 0.12 ) / 4.55 ) * 0.62 ) * TEX )
		xx = 10
		while xx < 110:
			w = int( rng.integers( 3, 9 ))
			px[yy:yy + 2, xx:xx + w] *= 0.35
			xx += w + 2
	# the cloth, below
	ys0 = int( 0.64 * TEX )
	n = rng.random(( TEX - ys0, TEX ))
	cloth = Image.fromarray(( n * 255 ).astype( np.uint8 )).filter( ImageFilter.GaussianBlur( 1.2 ))
	cn = np.array( cloth ) / 255.0
	weave = 0.85 + 0.15 * np.sin( np.arange( TEX )[None, :] * 1.7 ) * np.sin( np.arange( TEX - ys0 )[:, None] * 1.3 )
	base = np.array([ 0.78, 0.72, 0.60 ])
	c = base[None, None, :] * ( 0.7 + 0.5 * cn )[..., None] * weave[..., None]
	# soot towards the end of the plug (top of its strip)
	vv = np.arange( TEX - ys0 )[:, None] / ( TEX - ys0 )
	soot = np.clip( 1 - vv / 0.12, 0, 1 ) * ( np.arange( TEX )[None, :] < TEX // 2 )
	c = c * ( 1 - 0.75 * soot[..., None] )
	px[ys0:] = c
	return Image.fromarray(( np.clip( px, 0, 1 ) * 255 ).astype( np.uint8 ))

# the incendiary grenade: a grey canister with orange bands, its fuse, lever
# and pin (as make_buy_images.py)
CAN_Y = [ 0.0, 0.08, 0.3, 0.45, 1.6, 2.0, 2.15, 2.30, 2.55, 2.62, 2.95, 3.0 ]
CAN_R = [ 0.58, 0.64, 0.64, 0.64, 0.64, 0.64, 0.64, 0.55, 0.30, 0.24, 0.24, 0.18 ]

def incendiary_mesh():
	can = lathe( CAN_Y, CAN_R, 14, 0.0, 0.62 )
	lever = box(( 0, 1.85, -0.70 ), ( 0.13, 0.95, 0.04 ), 0.66, 0.80, 0.0, 0.5 )
	lever_top = box(( 0, 2.9, -0.45 ), ( 0.13, 0.05, 0.26 ), 0.66, 0.80, 0.5, 1.0 )
	ring = []
	for i in range( 11 ):
		t = 2 * math.pi * i / 10
		ring.append(( 0.30 + 0.0 * math.cos( t ), 2.75 + 0.20 * math.cos( t ), -0.25 + 0.20 * math.sin( t )))
	pin = tube( ring, 0.035, 4, 0.82, 0.9, 0.0, 1.0 )
	return merge([ can, lever, lever_top, pin ])

def incendiary_texture():
	px = np.zeros(( TEX, TEX, 3 ))
	y = np.arange( TEX )[:, None] / TEX
	x = np.arange( TEX )[None, :] / TEX
	h = 3.0 * ( 1 - y / 0.62 )
	metal = np.array([ 0.42, 0.44, 0.42 ])
	shade = 0.8 + 0.2 * np.cos( 2 * math.pi * ( x - 0.3 ))
	streak = np.exp( -(( x - 0.30 ) / 0.04 ) ** 2 ) * 0.5
	col = metal[None, None, :] * shade[..., None] * np.ones(( TEX, 1, 1 ))
	band = ((( h > 1.65 ) & ( h < 1.95 )) | (( h > 0.25 ) & ( h < 0.40 )))[..., None] * np.ones(( 1, TEX, 1 ))
	col = np.where( band > 0, np.array([ 0.85, 0.32, 0.06 ]) * shade[..., None], col )
	top = ( h > 2.3 )[..., None] * np.ones(( 1, TEX, 1 ))
	col = np.where( top > 0, np.array([ 0.62, 0.62, 0.64 ]) * shade[..., None], col )
	col = col + streak[..., None] * np.array([ 0.8, 0.8, 0.8 ])
	px[:] = col
	# printed marks
	rng = np.random.default_rng( 5 )
	for row in range( 3 ):
		yy = int((( 1 - ( 1.25 - row * 0.15 ) / 3.0 ) * 0.62 ) * TEX )
		xx = 20
		while xx < 90:
			w = int( rng.integers( 3, 8 ))
			px[yy:yy + 2, xx:xx + w] = [ 0.85, 0.80, 0.70 ]
			xx += w + 2
	# steel of the lever, the pin
	ys0 = int( 0.64 * TEX )
	vv = np.arange( TEX - ys0 )[:, None] / ( TEX - ys0 )
	px[ys0:] = np.array([ 0.60, 0.61, 0.63 ]) * ( 0.85 + 0.15 * np.cos( vv * 9 ))[..., None]
	return Image.fromarray(( np.clip( px, 0, 1 ) * 255 ).astype( np.uint8 ))

# --- placement --------------------------------------------------------------

def place( mesh, scale, axis, center, along, up_hint = ( 0, 0, 1 )):
	# the shape's +y (base to top) along `axis`, the point `along` of its
	# height on `center`
	verts, norms, uvs, tris = mesh
	a = np.array( axis, float ); a /= np.linalg.norm( a )
	h = np.array( up_hint, float )
	if abs( np.dot( h, a )) > 0.9:
		h = np.array([ 1.0, 0, 0 ])
	x = np.cross( a, h ); x /= np.linalg.norm( x )
	z = np.cross( x, a )
	R = np.stack([ x, a, z ], 1 ) # columns: where the shape's x, y, z go
	v = ( verts - np.array([ 0, along, 0 ])) * scale
	v = v @ R.T + np.array( center, float )
	n = norms @ R.T
	return v, n, uvs, tris

# --- the studio model -------------------------------------------------------

def euler_to_matrix( a ):
	# GoldSrc's AngleMatrix of a bone value (x, y, z rotations in radians)
	sy, cy = math.sin( a[2] ), math.cos( a[2] )
	sp, cp = math.sin( a[1] ), math.cos( a[1] )
	sr, cr = math.sin( a[0] ), math.cos( a[0] )
	return np.array([
		[ cp * cy, sr * sp * cy + cr * -sy, cr * sp * cy + -sr * -sy ],
		[ cp * sy, sr * sp * sy + cr * cy, cr * sp * sy + -sr * cy ],
		[ -sp, sr * cp, cr * cp ]])

def write_mdl( path, name, bones, mesh_bone, mesh, texture, seqs ):
	# bones: [(name, parent, value[6])], the mesh on bone `mesh_bone`,
	# texture: an RGB image; seqs: [label]
	verts, norms, uvs, tris = mesh
	tex = texture.quantize( 256, method = Image.MEDIANCUT )
	pal = tex.getpalette()[:768]
	pal += [0] * ( 768 - len( pal ))
	tw, th = tex.size

	body = bytearray()
	def here():
		return 244 + len( body )
	def align():
		while len( body ) % 4:
			body.append( 0 )

	bmin = verts.min( 0 ) - 1
	bmax = verts.max( 0 ) + 1

	# bones
	boneindex = here()
	for bname, parent, value in bones:
		body += struct.pack( '<32sii6i6f6f', bname.encode(), parent, 0, *([-1] * 6), *value, *([1.0 / 32] * 6 ))
	# a sequence group
	seqgroupindex = here()
	body += struct.pack( '<32s64sii', b'default', b'', 0, 0 )
	# the animations: no motion, every bone at its value
	animindex = here()
	for _ in bones:
		body += struct.pack( '<6H', 0, 0, 0, 0, 0, 0 )
	# sequences
	seqindex = here()
	for label in seqs:
		body += struct.pack( '<32sfiiiiiiiiii3fii3f3fii2i2f2fiiiiii',
			label.encode(), 30.0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
			0, 0, 0, 0, 0, *bmin, *bmax, 1, animindex,
			0, 0, 0.0, 0.0, 1.0, 1.0, 0, 0, 0, 0, 0, 0 )
	# the texture
	textureindex = here()
	body += struct.pack( '<64siiii', b'molotov.bmp', 0, tw, th, 0 )
	texture_entry = textureindex
	skinindex = here()
	body += struct.pack( '<h', 0 )
	align()
	texturedataindex = here()
	pixels_at = here()
	body += tex.tobytes() + bytes( pal )
	align()
	# vertices, normals, their bone
	vertinfoindex = here()
	body += bytes([ mesh_bone ] * len( verts ))
	align()
	norminfoindex = here()
	body += bytes([ mesh_bone ] * len( norms ))
	align()
	vertindex = here()
	body += verts.astype( '<f4' ).tobytes()
	normindex = here()
	body += norms.astype( '<f4' ).tobytes()
	# the triangles, each a strip of three (s, t in texels), wound as
	# Valve's: clockwise seen from outside, else the renderer culls them
	triindex = here()
	for tri in tris:
		a, b, c = tri
		if np.dot( np.cross( verts[b] - verts[a], verts[c] - verts[a] ), norms[a] + norms[b] + norms[c] ) > 0:
			tri = ( a, c, b )
		body += struct.pack( '<h', 3 )
		for v in tri:
			s = int( round( uvs[v][0] * ( tw - 1 )))
			t = int( round( uvs[v][1] * ( th - 1 )))
			body += struct.pack( '<4h', v, v, s, t )
	body += struct.pack( '<h', 0 )
	align()
	meshindex = here()
	body += struct.pack( '<5i', len( tris ), triindex, 0, len( norms ), normindex )
	modelindex = here()
	radius = float( np.linalg.norm( verts, axis = 1 ).max())
	body += struct.pack( '<64sifiiiiiiiiii', name.encode(), 0, radius, 1, meshindex,
		len( verts ), vertinfoindex, vertindex, len( norms ), norminfoindex, normindex, 0, 0 )
	bodypartindex = here()
	body += struct.pack( '<64siii', b'studio', 1, 1, modelindex )

	# the texture's pixels
	struct.pack_into( '<64siiii', body, texture_entry - 244, b'molotov.bmp', 0, tw, th, pixels_at )

	length = 244 + len( body )
	hdr = struct.pack( '<4si64si3f3f3f3f3fi', b'IDST', 10, name.encode(), length,
		0, 0, 0, 0, 0, 0, 0, 0, 0, *bmin, *bmax, 0 )
	hdr += struct.pack( '<20i', len( bones ), boneindex, 0, boneindex, 0, boneindex,
		len( seqs ), seqindex, 1, seqgroupindex, 1, textureindex, texturedataindex,
		1, 1, skinindex, 1, bodypartindex, 0, boneindex )
	hdr += struct.pack( '<6i', 0, 0, 0, 0, 0, 0 )
	assert len( hdr ) == 244
	with open( path, 'wb' ) as f:
		f.write( hdr + body )

# --- the six models ---------------------------------------------------------

# Valve's smoke grenade, for the placement:
#  v_smokegrenade.mdl: on bone 36 "Bone02", body around (0.05, 0.1, -2.58),
#    its top towards +z (-0.088, 0.203, 0.975), radius about 0.95
#  p_smokegrenade.mdl: on "Cylinder01" (child of "Bip01 R Hand", value
#    below), body around (0.62, 0, 1.65), its top towards (-0.22, 0, -0.97)
#  w_smokegrenade.mdl: lying, radius about 4.6
HAND = ( 11.37556, 0.0, 0.0, 2.237628, 0.074035, -0.839386 )
CYLINDER = ( 3.726729, -0.053313, 2.419488, -0.262762, 2.898943, 1.398576 )

KINDS = {
	'molotov': dict( mesh = molotov_mesh, texture = molotov_texture, radius = 0.95, grip = 1.3 ),
	'incgrenade': dict( mesh = incendiary_mesh, texture = incendiary_texture, radius = 0.64, grip = 1.2 ),
}

def main():
	out = sys.argv[1]
	previews = []
	for kind, k in KINDS.items():
		mesh = k['mesh']()
		tex = k['texture']()
		previews.append( tex )

		s = 1.2 * 0.95 / k['radius']
		v = place( mesh, s, ( -0.088, 0.203, 0.975 ), ( 0.05, 0.1, -2.58 ), k['grip'] )
		write_mdl( f'{out}/v_{kind}_part.mdl', f'v_{kind}_part.mdl', [( 'Bone02', -1, ( 0, 0, 0, 0, 0, 0 ))], 0, v, tex, [ 'idle' ])

		s = 1.7 * 0.95 / k['radius']
		p = place( mesh, s, ( -0.22, 0, -0.97 ), ( 0.62, 0, 1.65 ), k['grip'] )
		write_mdl( f'{out}/p_{kind}.mdl', f'p_{kind}.mdl', [( 'Bip01 R Hand', -1, HAND ), ( 'Cylinder01', 0, CYLINDER )], 1, p, tex, [ 'idle' ])

		# lying along x, resting on the bottom of the thrown entity's box
		s = 4.0 * 0.95 / k['radius']
		w = place( mesh, s, ( 1, 0, 0 ), ( 0, 0, 0 ), k['grip'] )
		wv = w[0].copy()
		wv[:, 2] += -1 - wv[:, 2].min()
		write_mdl( f'{out}/w_{kind}.mdl', f'w_{kind}.mdl', [( 'root', -1, ( 0, 0, 0, 0, 0, 0 ))], 0, ( wv, w[1], w[2], w[3] ), tex, [ 'idle' ])

	if len( sys.argv ) > 2:
		sheet = Image.new( 'RGB', ( TEX * 2, TEX ))
		for i, t in enumerate( previews ):
			sheet.paste( t, ( i * TEX, 0 ))
		sheet.save( sys.argv[2] )

if __name__ == '__main__':
	main()
