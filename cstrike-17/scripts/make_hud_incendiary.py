#!/usr/bin/env python3
# make_hud_incendiary.py - draws sprites/640hud_cs17.spr, the weapon list
# icons of the incendiary grenade (story 054) in the style of Valve's
# 640hud sheets: a dark panel with scanlines, the grenade in light grey
# with a dark outline, its name in bold italics, a glow when selected.
# The molotov's icons are Valve's own (640hud17 and 640hud18).
#
#   python make_hud_incendiary.py <FiraSans-Regular.ttf> <out.spr> [preview.png]
#
# Layout of the 256x128 sheet, as in weapon_incgrenade.txt:
#   640: 0 0 170 45 (weapon), 0 45 170 45 (weapon_s)
#   320: 176 0 80 20 (weapon), 176 20 80 20 (weapon_s)
import math
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SS = 4 # supersampling

def panel( w, h ):
	im = Image.new( 'L', ( w, h ), 0 )
	px = im.load()
	for y in range( h ):
		for x in range( w ):
			px[x, y] = 34 if y % 2 == 0 else 28
	# rounded corners, as Valve's panels
	mask = Image.new( 'L', ( w * SS, h * SS ), 0 )
	ImageDraw.Draw( mask ).rounded_rectangle(( 0, 0, w * SS - 1, h * SS - 1 ), radius = 4 * SS, fill = 255 )
	mask = mask.resize(( w, h ), Image.LANCZOS )
	return ImageChops.multiply( im, mask )

def grenade( w, h ):
	# the canister lying at a slant: a body, a cap, the lever along it and
	# the band of an incendiary; light grey shaded top to bottom
	W, H = w * SS, h * SS
	shape = Image.new( 'L', ( W, H ), 0 )
	d = ImageDraw.Draw( shape )
	body = ( 0.34 * W, 0.24 * H, 0.86 * W, 0.80 * H )
	d.rounded_rectangle( body, radius = 0.12 * H, fill = 255 )
	d.rounded_rectangle(( 0.20 * W, 0.34 * H, 0.36 * W, 0.70 * H ), radius = 0.06 * H, fill = 255 ) # cap
	d.rounded_rectangle(( 0.12 * W, 0.42 * H, 0.22 * W, 0.62 * H ), radius = 0.04 * H, fill = 255 ) # fuse
	d.polygon([( 0.22 * W, 0.30 * H ), ( 0.70 * W, 0.14 * H ), ( 0.72 * W, 0.24 * H ), ( 0.26 * W, 0.38 * H )], fill = 255 ) # lever
	d.ellipse(( 0.04 * W, 0.30 * H, 0.16 * W, 0.56 * H ), outline = 255, width = int( 0.025 * W )) # pin ring

	# shading: light on top, darker underneath, a darker band and grooves
	shade = Image.new( 'L', ( W, H ), 0 )
	sp = shade.load()
	for y in range( H ):
		t = y / H
		v = int( 235 - 120 * max( 0.0, t - 0.25 ) / 0.6 )
		for x in range( W ):
			sp[x, y] = v
	sd = ImageDraw.Draw( shade )
	sd.rectangle(( 0.56 * W, 0.24 * H, 0.68 * W, 0.80 * H ), fill = 90 ) # band
	for gx in ( 0.44, 0.76 ):
		sd.line(( gx * W, 0.26 * H, gx * W, 0.78 * H ), fill = 150, width = SS )
	sd.rectangle(( 0.20 * W, 0.34 * H, 0.36 * W, 0.42 * H ), fill = 200 )

	# tilted as Valve's grenades
	shape = shape.rotate( 8, Image.BICUBIC )
	shade = shade.rotate( 8, Image.BICUBIC )
	fill = ImageChops.multiply( shade, shape )
	# the dark outline of Valve's icons
	grown = shape.filter( ImageFilter.MaxFilter( 2 * SS + 1 ))
	outline = ImageChops.subtract( grown, shape )
	img = ImageChops.subtract( fill, ImageChops.multiply( outline, Image.new( 'L', ( W, H ), 255 )))
	return img.resize(( w, h ), Image.LANCZOS ), grown.resize(( w, h ), Image.LANCZOS )

def label( font_path, text, w, h, size, value ):
	W, H = w * SS, h * SS
	im = Image.new( 'L', ( W, H ), 0 )
	font = ImageFont.truetype( font_path, size * SS )
	d = ImageDraw.Draw( im )
	# bold: the text drawn with a stroke of its own colour
	d.text(( 0, 0 ), text, font = font, fill = value, stroke_width = int( 0.9 * SS ), stroke_fill = value )
	# italics
	shear = 0.22
	im = im.transform( im.size, Image.AFFINE, ( 1, shear, -shear * H * 0.5, 0, 1, 0 ), Image.BICUBIC )
	return im.resize(( w, h ), Image.LANCZOS )

def icon( font_path, w, h, selected ):
	base = panel( w, h )
	g, g_mask = grenade( int( w * 0.58 ), int( h * 0.92 ))
	gx, gy = w - g.width - int( w * 0.02 ), int( h * 0.0 )

	if selected:
		glow = Image.new( 'L', ( w, h ), 0 )
		glow.paste( g_mask, ( gx, gy ))
		glow = glow.filter( ImageFilter.GaussianBlur( h * 0.12 )).point( lambda v: min( 255, int( v * 1.6 )))
		base = ImageChops.lighter( base, glow )

	layer = Image.new( 'L', ( w, h ), 0 )
	layer.paste( g, ( gx, gy ))
	mask = Image.new( 'L', ( w, h ), 0 )
	mask.paste( g_mask, ( gx, gy ))
	base = Image.composite( layer, base, mask )

	t = label( font_path, 'INCENDIARY', int( w * 0.62 ), int( h * 0.34 ), int( h * 0.27 ), 235 if selected else 160 )
	tx, ty = int( w * 0.06 ), int( h * 0.62 )
	base.paste( ImageChops.lighter( base.crop(( tx, ty, tx + t.width, ty + t.height )), t ), ( tx, ty ))
	return base

def write_spr( path, im ):
	# IDSP version 2, one frame, additive like Valve's HUD sheets
	im = im.convert( 'L' )
	w, h = im.size
	pal = []
	for i in range( 256 ):
		pal += [i, i, i]
	d = struct.pack( '<4siiifiiifi', b'IDSP', 2, 2, 1, math.hypot( w / 2, h / 2 ), w, h, 1, 0.0, 0 )
	d += struct.pack( '<h', 256 ) + bytes( pal )
	d += struct.pack( '<iiiii', 0, -( w // 2 ), h // 2, w, h ) + im.tobytes()
	with open( path, 'wb' ) as f:
		f.write( d )

def main():
	font_path, out = sys.argv[1], sys.argv[2]
	sheet = Image.new( 'L', ( 256, 128 ), 0 )
	sheet.paste( icon( font_path, 170, 45, False ), ( 0, 0 ))
	sheet.paste( icon( font_path, 170, 45, True ), ( 0, 45 ))
	sheet.paste( icon( font_path, 80, 20, False ), ( 176, 0 ))
	sheet.paste( icon( font_path, 80, 20, True ), ( 176, 20 ))
	write_spr( out, sheet )
	if len( sys.argv ) > 3:
		sheet.resize(( 1024, 512 ), Image.NEAREST ).save( sys.argv[3] )

if __name__ == '__main__':
	main()
