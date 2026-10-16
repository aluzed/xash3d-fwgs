#!/usr/bin/env python3
# patch_dust2.py - Counter-Strike 1.7's de_dust2 from Counter-Strike's own
# (the player's Steam install): the same map, with
#  - story 065: two small crates at the mid's big one (the "xbox"),
#    against the catwalk's wall: one on the terrorists' side, the other on
#    the big crate's front (the mid doors' side); one climbs alone from the mid to the catwalk (short):
#    floor -128, small crate -80, big crate -32, small crate 16, the
#    catwalk's parapet 32, its floor 0;
#  - a small crate at CT spawn's "elevator" (the crates one was boosted on
#    to reach the short): on the small crate at the top of the stack, so
#    that one reaches the short's parapet alone.
#  - two small crates at the terrorists' big crates under the upper passage
#    (x -1536, y -128), below and on them as at the mid's: up alone.
#  - long A: a small crate at the medium crate (64) against the north wall.
# Everything else is kept byte for byte; new things are brush models added
# after the map's own (bsp30.py).
#
#   python patch_dust2.py <Steam cstrike/maps/de_dust2.bsp> <out de_dust2.bsp>
import math
import os
import sys

sys.path.insert( 0, os.path.dirname( os.path.abspath( __file__ )))
from bsp30 import Bsp

# Dust2's small crates are 48 units, the big one 96 (x -352..-256,
# y 1376..1472, z -128..-32)
SMALL = 48.0

def find_face( bsp, texture, normal, point ):
	# the world's face of a texture, facing `normal`, whose plane holds
	# `point` and whose bounds contain it
	names = bsp.texture_names()
	for i in range( bsp.count( 'faces', 20 )):
		f = bsp.face( i )
		s, t, miptex, _ = bsp.texinfo( f['texinfo'] )
		if texture and names[miptex] != texture:
			continue
		px, py, pz, dist, _ = bsp.plane( f['plane'] )
		n = ( -px, -py, -pz ) if f['side'] else ( px, py, pz )
		if any( abs( a - b ) > 1e-3 for a, b in zip( n, normal )):
			continue
		if abs( px * point[0] + py * point[1] + pz * point[2] - dist ) > 0.5:
			continue
		v = bsp.face_vertices( i )
		if all( min( p[k] for p in v ) - 0.5 <= point[k] <= max( p[k] for p in v ) + 0.5 for k in range( 3 )):
			return i
	raise SystemExit( 'patch_dust2: no %s face at %s: is this Counter-Strike\'s de_dust2?' % ( texture or 'world', point ))

def light_sampler( bsp, face ):
	# the face's light at a world point (bilinear, clamped to the face)
	f = bsp.face( face )
	s, t, _, _ = bsp.texinfo( f['texinfo'] )
	verts = bsp.face_vertices( face )
	smin = math.floor( min( v[0] * s[0] + v[1] * s[1] + v[2] * s[2] + s[3] for v in verts ) / 16 )
	tmin = math.floor( min( v[0] * t[0] + v[1] * t[1] + v[2] * t[2] + t[3] for v in verts ) / 16 )
	w, h, rows = bsp.face_lightmap( face )
	def sample( p ):
		u = ( p[0] * s[0] + p[1] * s[1] + p[2] * s[2] + s[3] ) / 16 - smin
		v = ( p[0] * t[0] + p[1] * t[1] + p[2] * t[2] + t[3] ) / 16 - tmin
		u = min( max( u, 0 ), w - 1 ); v = min( max( v, 0 ), h - 1 )
		u0, v0 = int( u ), int( v )
		u1, v1 = min( u0 + 1, w - 1 ), min( v0 + 1, h - 1 )
		fu, fv = u - u0, v - v0
		mix = lambda a, b, k: tuple( x + ( y - x ) * k for x, y in zip( a, b ))
		return mix( mix( rows[v0][u0], rows[v0][u1], fu ), mix( rows[v1][u0], rows[v1][u1], fu ), fv )
	return sample

def luxel_points( s, t, verts ):
	# the world point of each luxel of a new face (axis-aligned faces: the
	# axis the face is on is constant over it)
	smin = math.floor( min( v[0] * s[0] + v[1] * s[1] + v[2] * s[2] + s[3] for v in verts ) / 16 )
	tmin = math.floor( min( v[0] * t[0] + v[1] * t[1] + v[2] * t[2] + t[3] for v in verts ) / 16 )
	fixed = [ k for k in range( 3 ) if max( v[k] for v in verts ) - min( v[k] for v in verts ) < 1e-3 ][0]
	ks = [ k for k in range( 3 ) if abs( s[k] ) > 1e-6 ][0]
	kt = [ k for k in range( 3 ) if abs( t[k] ) > 1e-6 ][0]
	def point( i, j ):
		p = [ 0.0, 0.0, 0.0 ]
		p[fixed] = verts[0][fixed]
		p[ks] = (( smin + i ) * 16 - s[3] ) / s[ks]
		p[kt] = (( tmin + j ) * 16 - t[3] ) / t[kt]
		return p
	return point

def moved( texinfo, delta ):
	# a texinfo's vectors, their offsets moved with the face by delta
	s, t, miptex, _ = texinfo
	shift = lambda vec: ( vec[0], vec[1], vec[2], vec[3] - ( vec[0] * delta[0] + vec[1] * delta[1] + vec[2] * delta[2] ))
	return shift( s ), shift( t ), miptex

def add_crate( bsp, mins, lit, textures ):
	"""a small crate at mins; lit maps its drawn sides ('-x', '+y', ...) to
	(sampler, where): the light of a luxel is the sampler's at where(point),
	a face of the map lit as that side would be; the other sides touch a
	wall or a crate, are not drawn and stay solid"""
	maxs = tuple( v + SMALL for v in mins )
	x0, y0, z0 = mins
	x1, y1, z1 = maxs
	verts = {
		'-x': [( x0, y0, z0 ), ( x0, y1, z0 ), ( x0, y1, z1 ), ( x0, y0, z1 )],
		'+x': [( x1, y0, z0 ), ( x1, y1, z0 ), ( x1, y1, z1 ), ( x1, y0, z1 )],
		'+y': [( x0, y1, z0 ), ( x1, y1, z0 ), ( x1, y1, z1 ), ( x0, y1, z1 )],
		'-y': [( x0, y0, z0 ), ( x1, y0, z0 ), ( x1, y0, z1 ), ( x0, y0, z1 )],
		'+z': [( x0, y0, z1 ), ( x1, y0, z1 ), ( x1, y1, z1 ), ( x0, y1, z1 )],
	}
	ref_ti, ref_at = textures
	delta = tuple( a - b for a, b in zip( mins, ref_at ))

	sides = {}
	for key, ( sampler, where ) in lit.items():
		s, t, miptex = moved( ref_ti[key], delta )
		def fill( w, h, s = s, t = t, sampler = sampler, where = where, v = verts[key] ):
			point = luxel_points( s, t, v )
			return [[ tuple( int( max( 0, min( 255, round( c )))) for c in sampler( where( point( i, j ))))
				for i in range( w )] for j in range( h )]
		sides[key] = ( s, t, miptex, fill )

	model = bsp.add_box( mins, maxs, sides )
	bsp.add_entity([( 'classname', 'func_wall' ), ( 'model', '*%d' % model )])
	return model

def scaled( sampler, k ):
	return lambda p: tuple( c * k for c in sampler( p ))

def crate_textures( bsp ):
	# the textures as a small crate of the map has them (at -1200 2048 0):
	# its sides along x and y, its top
	ref_at = ( -1200.0, 2048.0, 0.0 )
	side_x = bsp.texinfo( bsp.face( find_face( bsp, 'SandCrtSmSd', ( -1, 0, 0 ), ( -1200, 2072, 24 )))['texinfo'] )
	side_y = bsp.texinfo( bsp.face( find_face( bsp, 'SandCrtSmSd', ( 0, 1, 0 ), ( -1176, 2096, 24 )))['texinfo'] )
	top = bsp.texinfo( bsp.face( find_face( bsp, 'SandCrtSmTp', ( 0, 0, 1 ), ( -1176, 2072, 48 )))['texinfo'] )
	return ({ '-x': side_x, '+x': side_x, '-y': side_y, '+y': side_y, '+z': top }, ref_at )

def add_mid_crates( bsp ):
	textures = crate_textures( bsp )

	# the light of the place: the big crate's sides and top, the floor
	# beside it, the catwalk's wall above it
	big_west = light_sampler( bsp, find_face( bsp, 'SandCrtLrgSd', ( -1, 0, 0 ), ( -352, 1424, -80 )))
	big_south = light_sampler( bsp, find_face( bsp, 'SandCrtLrgSd', ( 0, -1, 0 ), ( -304, 1376, -80 )))
	big_top = light_sampler( bsp, find_face( bsp, 'SandCrtLrgTp', ( 0, 0, 1 ), ( -304, 1424, -32 )))
	floor = light_sampler( bsp, find_face( bsp, None, ( 0, 0, 1 ), ( -280, 1352, -128 )))
	wall = light_sampler( bsp, find_face( bsp, None, ( -1, 0, 0 ), ( -256, 1424, -16 )))

	# on the floor, on the terrorists' side of the big crate: its west side
	# as the big crate's, its south side as the big crate's, its top as the
	# floor out of its shadow; against the wall (+x) and the big crate (+y)
	low = add_crate( bsp, ( -304.0, 1328.0, -128.0 ), {
		'-x': ( big_west, lambda p: ( -352, p[1] + 48, p[2] )),
		'-y': ( big_south, lambda p: ( p[0], 1376, p[2] )),
		'+z': ( floor, lambda p: ( p[0] - 48, p[1], -128 )),
	}, textures )

	# on the big crate, against the wall, at its front (the mid doors' side):
	# one lands on the big crate from the first, then jumps on this one ahead;
	# its sides as the catwalk's wall facing the mid (south brighter, north
	# darker, as the big crate's own are), its top as the big crate's top
	high = add_crate( bsp, ( -304.0, 1424.0, -32.0 ), {
		'-x': ( wall, lambda p: ( -256, p[1], min( p[2], -1 ))),
		'-y': ( scaled( wall, 1.25 ), lambda p: ( -256, 1448, min( p[2], -1 ))),
		'+y': ( scaled( wall, 0.6 ), lambda p: ( -256, 1448, min( p[2], -1 ))),
		'+z': ( big_top, lambda p: ( p[0] - 48, p[1], -32 )),
	}, textures )
	return low, high

def add_elevator_crate( bsp, textures ):
	# CT spawn's "elevator": the crates the counter-terrorists boost each
	# other on to reach the short quickly. One climbs them alone up to the
	# small crate on top (40); the short beyond (96) has a parapet at 128,
	# 88 over it: a small crate on that one, against the north wall, on its
	# west edge, the short's side: one jumps on it from the small crate's
	# east edge (88), then over the parapet (128)
	wall = light_sampler( bsp, find_face( bsp, None, ( 0, -1, 0 ), ( 608, 2400, 64 )))
	short = light_sampler( bsp, find_face( bsp, 'SandCCrete', ( 0, 0, 1 ), ( 600, 2480, 96 )))
	return add_crate( bsp, ( 576.0, 2352.0, 40.0 ), {
		'-x': ( scaled( wall, 0.85 ), lambda p: ( 600, 2400, min( p[2], 95 ))),
		'+x': ( wall, lambda p: ( 620, 2400, min( p[2], 95 ))),
		'-y': ( scaled( wall, 1.1 ), lambda p: ( p[0], 2400, min( p[2], 95 ))),
		'+z': ( short, lambda p: ( p[0], p[1] + 96, 96 )),
	}, textures )

def add_t_crates( bsp, textures ):
	# the terrorists' side, the two big crates under the upper passage
	# (x -1536..-1440, y -128..64, their tops at 96 over a floor sloping
	# from 0 to 32): one climbs them alone as at the mid's big crate, a
	# small crate below, against the wall, another on them against the
	# passage's parapet (160), the passage beyond at 128
	big_west = light_sampler( bsp, find_face( bsp, 'SandCrtLrgSd', ( -1, 0, 0 ), ( -1536, -80, 56 )))
	big_top = light_sampler( bsp, find_face( bsp, 'SandCrtLrgTp', ( 0, 0, 1 ), ( -1488, -80, 96 )))
	low_wall = light_sampler( bsp, find_face( bsp, None, ( 0, 1, 0 ), ( -1568, -128, 64 )))
	high_wall = light_sampler( bsp, find_face( bsp, None, ( 0, 1, 0 ), ( -1488, -128, 112 )))
	east_wall = light_sampler( bsp, find_face( bsp, 'SandTrim', ( -1, 0, 0 ), ( -1440, -80, 144 )))

	low = add_crate( bsp, ( -1584.0, -128.0, 16.0 ), {
		'-x': ( big_west, lambda p: ( -1536, p[1], max( p[2], 17 ))),
		'+y': ( low_wall, lambda p: ( p[0], -128, max( p[2], 25 ))),
		'+z': ( big_top, lambda p: ( p[0] + 96, p[1], 96 )),
	}, textures )
	high = add_crate( bsp, ( -1488.0, -128.0, 96.0 ), {
		'-x': ( east_wall, lambda p: ( -1440, p[1], min( max( p[2], 129 ), 159 ))),
		'+y': ( high_wall, lambda p: ( p[0], -128, min( p[2], 127 ))),
		'+z': ( big_top, lambda p: ( p[0], p[1] + 48, 96 )),
	}, textures )
	return low, high

def add_long_crate( bsp, textures ):
	# long A: the medium crate (64, x 896..960, y 1152..1216) against the
	# north wall, before the big one (128): a small crate on its east side,
	# against the wall, to climb it alone
	medium_east = light_sampler( bsp, find_face( bsp, 'SandCrtSmSd', ( 1, 0, 0 ), ( 960, 1184, 32 )))
	medium_south = light_sampler( bsp, find_face( bsp, 'SandCrtSmSd', ( 0, -1, 0 ), ( 928, 1152, 32 )))
	medium_top = light_sampler( bsp, find_face( bsp, 'SandCrtSmTp', ( 0, 0, 1 ), ( 928, 1184, 64 )))
	return add_crate( bsp, ( 960.0, 1168.0, 0.0 ), {
		'+x': ( medium_east, lambda p: ( 960, p[1], p[2] )),
		'-y': ( medium_south, lambda p: ( p[0] - 64, 1152, p[2] )),
		'+z': ( medium_top, lambda p: ( p[0] - 48, p[1], 64 )),
	}, textures )

def main():
	src, dst = sys.argv[1], sys.argv[2]
	bsp = Bsp( open( src, 'rb' ).read())
	models = add_mid_crates( bsp ) + ( add_elevator_crate( bsp, crate_textures( bsp )), ) + add_t_crates( bsp, crate_textures( bsp )) + ( add_long_crate( bsp, crate_textures( bsp )), )
	with open( dst, 'wb' ) as f:
		f.write( bsp.save())
	print( 'patch_dust2: the crates as %s -> %s' % ( ', '.join( '*%d' % m for m in models ), dst ))

if __name__ == '__main__':
	main()
