#!/usr/bin/env python3
# make_smoke_puff.py - draws sprites/smoke_puff.spr, the puffs of the smoke
# grenade's volume (story 061): four soft, thick puffs in a 2x2 sheet of one
# frame, an "index alpha" sprite (the index is the opacity, the colour is
# the palette's last entry, white, tinted by the client). Valve's
# gas_puff_01.spr is at most 25% opaque: a smoke made of it can't hide.
#
#   python make_smoke_puff.py <out.spr> [preview.png]
import math
import struct
import sys

import numpy as np
from PIL import Image

SIZE = 512   # the sheet: made for 4K, used at every resolution
PUFF = 256   # one puff

def noise( shape, scale, rng ):
	# smooth value noise: random values on a coarse grid, upsampled
	coarse = rng.random(( shape[0] // scale + 2, shape[1] // scale + 2 ))
	img = Image.fromarray(( coarse * 255 ).astype( np.uint8 )).resize(( shape[1] + scale * 2, shape[0] + scale * 2 ), Image.BICUBIC )
	return np.array( img, float )[scale:scale + shape[0], scale:scale + shape[1]] / 255.0

def puff( rng ):
	y, x = np.mgrid[0:PUFF, 0:PUFF] + 0.5
	cx = cy = PUFF / 2
	# a lumpy outline: the radius varies with the angle
	ang = np.arctan2( y - cy, x - cx )
	lumps = sum( rng.uniform( 0.02, 0.045 ) * np.sin( k * ang + rng.uniform( 0, 6.3 )) for k in ( 2, 3, 5 ))
	r = np.hypot( x - cx, y - cy ) / ( PUFF / 2 ) / ( 0.92 + lumps )
	body = np.clip( 1.0 - r, 0.0, 1.0 )
	body = body ** 0.55 # thick inside, soft edge
	fbm = 0.45 * noise(( PUFF, PUFF ), 64, rng ) + 0.3 * noise(( PUFF, PUFF ), 32, rng ) + 0.15 * noise(( PUFF, PUFF ), 16, rng ) + 0.1 * noise(( PUFF, PUFF ), 6, rng )
	a = body * ( 0.75 + 0.45 * fbm )
	return np.clip( a, 0.0, 1.0 )

def main():
	out = sys.argv[1]
	rng = np.random.default_rng( 61 )
	sheet = np.zeros(( SIZE, SIZE ))
	for i in range( 4 ):
		px, py = ( i % 2 ) * PUFF, ( i // 2 ) * PUFF
		sheet[py:py + PUFF, px:px + PUFF] = puff( rng )

	idx = np.clip( sheet * 255.0 + 0.5, 0, 255 ).astype( np.uint8 )

	# palette: a grey ramp, the last entry (the colour) white
	pal = []
	for i in range( 256 ):
		pal += [ i, i, i ]

	w = h = SIZE
	d = struct.pack( '<4siiifiiifi', b'IDSP', 2, 2, 2, math.hypot( w / 2, h / 2 ), w, h, 1, 0.0, 0 )
	d += struct.pack( '<h', 256 ) + bytes( pal )
	d += struct.pack( '<iiiii', 0, -( w // 2 ), h // 2, w, h ) + idx.tobytes()
	with open( out, 'wb' ) as f:
		f.write( d )

	if len( sys.argv ) > 2:
		Image.fromarray( idx ).save( sys.argv[2] )

if __name__ == '__main__':
	main()
