#!/usr/bin/env python3
# make_menu_buttons.py - menu buttons whose words Valve's btns_main.bmp
# doesn't have, in its own lettering: each letter cut out of a label it
# has, the three states (normal, focused, pressed) kept, joined again.
#
#   python make_menu_buttons.py <btns_main.bmp> <out directory> [sheet.png]
#
# btns_main.bmp: buttons of 156 x 78, three states of 26 rows each.
import sys

import numpy as np
from PIL import Image

W, H, STATE = 156, 78, 26

# the labels of Counter-Strike's btns_main.bmp, by index, the letters to
# take from them (labels whose letters touch are left out)
LABELS = {
	1: 'Resume', 2: 'Training', 4: 'Load Game', 7: 'Quit', 8: 'Play CS', 9: 'Easy', 10: 'Medium',
	12: 'Save Game', 14: 'Cancel', 15: 'Options', 16: 'Video', 17: 'Audio', 18: 'Controls', 19: 'Done',
	21: 'Defaults', 24: 'Modes', 25: 'Advanced', 27: 'Delete', 31: 'Customize', 33: 'Exit',
	34: 'Connect', 35: 'Refresh', 38: 'Create', 40: 'Chat', 42: 'Search', 43: 'Servers',
	52: 'Update', 55: 'Console', 63: 'Load',
}

# the buttons made: file, pieces of labels (label, word, first letter, end)
# joined, ' ' a space: whole runs of letters, cut only where they join
BUTTONS = {
	'btn_presets.bmp': [( 8, 0, 0, 1 ), ( 38, 0, 1, 3 ), ( 1, 0, 2, 3 ), ( 38, 0, 5, 6 ), ( 33, 0, 3, 4 ), ( 1, 0, 2, 3 )], # P re s e t s
	'btn_raytracing.bmp': [( 1, 0, 0, 1 ), ( 8, 0, 2, 4 ), ' ', ( 2, 0, 0, 3 ), ( 14, 0, 3, 4 ), ( 2, 0, 5, 8 )],  # R ay  Tra c ing
	'btn_gameoptions.bmp': [( 12, 1, 0, 4 ), ' ', ( 15, 0, 0, 7 )],                                                  # Game  Options
	'btn_use.bmp': [( 52, 0, 0, 1 ), ( 1, 0, 2, 3 ), ( 38, 0, 5, 6 )],                                               # U s e
	'btn_rename.bmp': [( 1, 0, 0, 2 ), ( 14, 0, 2, 3 ), ( 14, 0, 1, 2 ), ( 1, 0, 4, 6 )],                             # Re n a me
	'btn_copycode.bmp': [( 34, 0, 0, 2 ), ( 15, 0, 1, 2 ), ( 9, 0, 3, 4 ), ' ', ( 34, 0, 0, 2 ), ( 10, 0, 2, 3 ), ( 38, 0, 5, 6 )],   # Co p y  Co de
	'btn_usecode.bmp': [( 52, 0, 0, 1 ), ( 1, 0, 2, 3 ), ( 38, 0, 5, 6 ), ' ', ( 34, 0, 0, 2 ), ( 10, 0, 2, 3 ), ( 38, 0, 5, 6 )],    # U s e  Co de
}

def ink( state ):
	# a column holds a letter where any of its pixels is lit
	return state.max( axis = ( 0, 2 )) > 40

def spans( cols ):
	out, start = [], None
	for x, on in enumerate( cols ):
		if on and start is None:
			start = x
		elif not on and start is not None:
			out.append(( start, x ))
			start = None
	if start is not None:
		out.append(( start, len( cols )))
	return out

def cut_word( button, x0, x1, n ):
	# a word of n letters lit from x0 to x1: the lettering is a typewriter's,
	# each letter as wide; its cuts at the least lit column near each
	# expected boundary (the letters touch, their glow joins them)
	weight = button[0:STATE].astype( int ).sum( axis = ( 0, 2 ))
	w = ( x1 - x0 ) / n
	cuts = [ x0 ]
	for k in range( 1, n ):
		guess = int( round( x0 + k * w ))
		lo, hi = max( cuts[-1] + 3, guess - 3 ), min( x1 - 3, guess + 4 )
		cuts.append( lo + int( np.argmin( weight[lo:hi] )) if hi > lo else guess )
	cuts.append( x1 )
	return [( cuts[k], cuts[k + 1] ) for k in range( n )]

def glyphs( sheet ):
	# letter -> (index, x0, x1), the first label having it; and the width of
	# a space, between the words of two-word labels
	found, spaces = {}, []
	for index, text in LABELS.items():
		button = sheet[index * H:( index + 1 ) * H]
		cols = ink( button[0:STATE] ) | ink( button[STATE:2 * STATE] ) | ink( button[2 * STATE:3 * STATE] )
		words = text.split( ' ' )
		parts = spans( cols )
		if len( parts ) != len( words ):
			continue
		for k, ( word, ( x0, x1 )) in enumerate( zip( words, parts )):
			if k:
				spaces.append( x0 - parts[k - 1][1] )
			for ch, ( a, b ) in zip( word, cut_word( button, x0, x1, len( word ))):
				found.setdefault( ch, ( index, a, b ))
	space = int( round( np.median( spaces ))) if spaces else 8
	return found, 0, space

def word_cuts( sheet, index ):
	# a label's words, each cut into its letters
	text = LABELS[index]
	button = sheet[index * H:( index + 1 ) * H]
	cols = ink( button[0:STATE] ) | ink( button[STATE:2 * STATE] ) | ink( button[2 * STATE:3 * STATE] )
	parts = spans( cols )
	words = text.split( ' ' )
	if len( parts ) != len( words ):
		raise SystemExit( 'make_menu_buttons: label %d (%s) does not split into its words' % ( index, text ))
	return [ cut_word( button, x0, x1, len( word )) for word, ( x0, x1 ) in zip( words, parts )]

def compose( sheet, pieces, space, left = 3 ):
	out = np.zeros(( H, W, 3 ), np.uint8)
	x = left
	for piece in pieces:
		if piece == ' ':
			x += space
			continue
		index, word, first, end = piece
		cuts = word_cuts( sheet, index )[word]
		x0, x1 = cuts[first][0], cuts[end - 1][1]
		w = x1 - x0
		if x + w > W:
			raise SystemExit( 'make_menu_buttons: too wide' )
		out[:, x:x + w] = np.maximum( out[:, x:x + w], sheet[index * H:( index + 1 ) * H, x0:x1] )
		x += w
	return out

def main():
	src = Image.open( sys.argv[1] )
	sheet = np.array( src.convert( 'RGB' ))
	_, _, space = glyphs( sheet )
	made = []
	for name, pieces in BUTTONS.items():
		img = Image.fromarray( compose( sheet, pieces, space ))
		# 8 bits as Valve's, its palette
		img.quantize( palette = src.convert( 'P' ) if src.mode == 'P' else None ).save( '%s/%s' % ( sys.argv[2], name ))
		made.append( img )
	if len( sys.argv ) > 3:
		preview = Image.new( 'RGB', ( W * len( made ), H ))
		for i, img in enumerate( made ):
			preview.paste( img, ( i * W, 0 ))
		preview.resize(( preview.width * 2, preview.height * 2 )).save( sys.argv[3] )

if __name__ == '__main__':
	main()
