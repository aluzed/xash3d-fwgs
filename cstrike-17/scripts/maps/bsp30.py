#!/usr/bin/env python3
# bsp30.py - reads and writes GoldSrc BSP (version 30) maps, and adds a box
# to one as a new brush model, built as the map compilers build a crate:
# a node per side holding its face, empty leaves around, the shared solid
# leaf inside, clip nodes for the player's hulls, lightmaps given by the
# caller. Used by patch_dust2.py (story 065).
import struct

LUMPS = ( 'entities', 'planes', 'textures', 'vertices', 'visibility', 'nodes', 'texinfo',
	'faces', 'lighting', 'clipnodes', 'leaves', 'marksurfaces', 'edges', 'surfedges', 'models' )

# the hulls the engine traces players with: standing, large, crouched
HULLS = (
	(( -16, -16, -36 ), ( 16, 16, 36 )),
	(( -32, -32, -32 ), ( 32, 32, 32 )),
	(( -16, -16, -18 ), ( 16, 16, 18 )),
)

CONTENTS_EMPTY = -1
CONTENTS_SOLID = -2

class Bsp:
	def __init__( self, data ):
		version = struct.unpack_from( '<i', data, 0 )[0]
		if version != 30:
			raise ValueError( 'not a GoldSrc BSP (version %d)' % version )
		self.lumps = {}
		for i, name in enumerate( LUMPS ):
			ofs, length = struct.unpack_from( '<ii', data, 4 + 8 * i )
			self.lumps[name] = bytearray( data[ofs:ofs + length] )

	def save( self ):
		out = bytearray( struct.pack( '<i', 30 ) + bytes( 8 * len( LUMPS )))
		for i, name in enumerate( LUMPS ):
			while len( out ) % 4:
				out.append( 0 )
			struct.pack_into( '<ii', out, 4 + 8 * i, len( out ), len( self.lumps[name] ))
			out += self.lumps[name]
		return bytes( out )

	def count( self, name, size ):
		return len( self.lumps[name] ) // size

	# -- reading --------------------------------------------------------

	def texture_names( self ):
		lump = self.lumps['textures']
		n = struct.unpack_from( '<i', lump, 0 )[0]
		names = []
		for i in range( n ):
			ofs = struct.unpack_from( '<i', lump, 4 + 4 * i )[0]
			names.append( lump[ofs:ofs + 16].split( b'\0' )[0].decode( 'latin1' ) if ofs >= 0 else '' )
		return names

	def texinfo( self, i ):
		v = struct.unpack_from( '<8fii', self.lumps['texinfo'], i * 40 )
		return v[0:4], v[4:8], v[8], v[9]

	def face( self, i ):
		plane, side, firstedge, numedges, texinfo = struct.unpack_from( '<HHiHH', self.lumps['faces'], i * 20 )
		styles = struct.unpack_from( '<4B', self.lumps['faces'], i * 20 + 12 )
		lightofs = struct.unpack_from( '<i', self.lumps['faces'], i * 20 + 16 )[0]
		return dict( plane = plane, side = side, firstedge = firstedge, numedges = numedges,
			texinfo = texinfo, styles = styles, lightofs = lightofs )

	def face_vertices( self, i ):
		f = self.face( i )
		verts = []
		for k in range( f['firstedge'], f['firstedge'] + f['numedges'] ):
			e = struct.unpack_from( '<i', self.lumps['surfedges'], k * 4 )[0]
			a, b = struct.unpack_from( '<HH', self.lumps['edges'], abs( e ) * 4 )
			verts.append( struct.unpack_from( '<3f', self.lumps['vertices'], ( a if e >= 0 else b ) * 12 ))
		return verts

	def plane( self, i ):
		return struct.unpack_from( '<4fi', self.lumps['planes'], i * 20 )

	def lightmap_size( self, verts, texinfo ):
		# as the engine computes a face's extents: texture space, 16 texels a sample
		import math
		s, t, _, _ = texinfo
		size = []
		for vec in ( s, t ):
			vals = [ v[0] * vec[0] + v[1] * vec[1] + v[2] * vec[2] + vec[3] for v in verts ]
			lo = math.floor( min( vals ) / 16 )
			hi = math.ceil( max( vals ) / 16 )
			size.append( hi - lo + 1 )
		return size[0], size[1]

	def face_lightmap( self, i ):
		# the face's first light style, as rows of (r, g, b)
		f = self.face( i )
		w, h = self.lightmap_size( self.face_vertices( i ), self.texinfo( f['texinfo'] ))
		data = self.lumps['lighting'][f['lightofs']:f['lightofs'] + w * h * 3]
		return w, h, [[ tuple( data[( y * w + x ) * 3:( y * w + x ) * 3 + 3] ) for x in range( w )] for y in range( h )]

	# -- writing --------------------------------------------------------

	def add( self, name, data ):
		index = len( self.lumps[name] )
		self.lumps[name] += data
		return index

	def add_plane( self, normal, dist ):
		axis = [ abs( c ) for c in normal ].index( 1.0 ) if 1.0 in [ abs( c ) for c in normal ] else 3
		return self.add( 'planes', struct.pack( '<4fi', *normal, dist, axis )) // 20

	def add_texinfo( self, s, t, miptex, flags = 0 ):
		return self.add( 'texinfo', struct.pack( '<8fii', *s, *t, miptex, flags )) // 40

	def add_entity( self, keys ):
		text = self.lumps['entities'].rstrip( b'\0' )
		text += ( '{\n' + ''.join( '"%s" "%s"\n' % kv for kv in keys ) + '}\n' ).encode( 'latin1' ) + b'\0'
		self.lumps['entities'] = bytearray( text )

	def add_box( self, mins, maxs, sides ):
		"""a solid box from mins to maxs as a new brush model; sides maps
		'-x', '+x', '-y', '+y', '-z', '+z' to (s, t, miptex, light) for the
		drawn ones, light a function (w, h) -> rows of (r, g, b); a side
		against a wall is left out. Returns the model's number."""
		x0, y0, z0 = mins
		x1, y1, z1 = maxs
		corner = lambda i: ( x1 if i & 1 else x0, y1 if i & 2 else y0, z1 if i & 4 else z0 )
		# each side: its axis, its plane's distance, its normal's sign, its
		# corners counter-clockwise seen from outside
		geometry = {
			'-x': ( 0, x0, -1, ( 0, 4, 6, 2 )), '+x': ( 0, x1, 1, ( 1, 3, 7, 5 )),
			'-y': ( 1, y0, -1, ( 0, 1, 5, 4 )), '+y': ( 1, y1, 1, ( 2, 6, 7, 3 )),
			'-z': ( 2, z0, -1, ( 0, 2, 3, 1 )), '+z': ( 2, z1, 1, ( 4, 5, 7, 6 )),
		}

		# the faces, wound clockwise seen from outside as the compilers write them
		face_of = {}
		first_face = self.count( 'faces', 20 )
		planes = {}
		for key in ( '-x', '+x', '-y', '+y', '-z', '+z' ):
			axis, dist, sign, idx = geometry[key]
			normal = [ 0.0, 0.0, 0.0 ]
			normal[axis] = 1.0
			planes[key] = self.add_plane( normal, dist )
			if key not in sides:
				continue
			s, t, miptex, light = sides[key]
			ti = self.add_texinfo( s, t, miptex )
			verts = [ corner( i ) for i in reversed( idx )]
			first_vert = self.count( 'vertices', 12 )
			for v in verts:
				self.add( 'vertices', struct.pack( '<3f', *v ))
			first_edge = self.count( 'surfedges', 4 )
			for k in range( 4 ):
				e = self.add( 'edges', struct.pack( '<HH', first_vert + k, first_vert + ( k + 1 ) % 4 )) // 4
				self.add( 'surfedges', struct.pack( '<i', e ))
			w, h = self.lightmap_size( verts, self.texinfo( ti ))
			rows = light( w, h )
			lightofs = self.add( 'lighting', bytes( c for row in rows for px in row for c in px ))
			face_of[key] = self.add( 'faces', struct.pack( '<HHiHH4Bi', planes[key], 1 if sign < 0 else 0,
				first_edge, 4, ti, 0, 255, 255, 255, lightofs )) // 20
		num_faces = self.count( 'faces', 20 ) - first_face

		# hull 0: a node per side, an empty leaf beyond each, the solid leaf
		# (0, shared by every model) inside
		bbox = struct.pack( '<6h', *[ int( v ) for v in mins ], *[ int( v ) for v in maxs ])
		order = ( '+x', '-x', '+z', '+y', '-z', '-y' )
		first_node = self.count( 'nodes', 24 )
		first_leaf = self.count( 'leaves', 28 )
		for k, key in enumerate( order ):
			marks = self.count( 'marksurfaces', 2 )
			nmarks = 0
			if key in face_of:
				self.add( 'marksurfaces', struct.pack( '<H', face_of[key] ))
				nmarks = 1
			self.add( 'leaves', struct.pack( '<ii', CONTENTS_EMPTY, -1 ) + bbox + struct.pack( '<HH4B', marks, nmarks, 0, 0, 0, 0 ))
		for k, key in enumerate( order ):
			sign = geometry[key][2]
			outside = -( first_leaf + k ) - 1
			inside = first_node + k + 1 if k + 1 < len( order ) else -1 # leaf 0
			children = ( outside, inside ) if sign > 0 else ( inside, outside )
			ff, nf = ( face_of[key], 1 ) if key in face_of else ( 0, 0 )
			self.add( 'nodes', struct.pack( '<i2h', planes[key], *children ) + bbox + struct.pack( '<HH', ff, nf ))

		# hulls 1-3: the box grown by the hull, the same chain
		heads = [ first_node ]
		for hmins, hmaxs in HULLS:
			heads.append( self.count( 'clipnodes', 8 ))
			grown = {
				'-x': x0 - hmaxs[0], '+x': x1 - hmins[0], '-y': y0 - hmaxs[1], '+y': y1 - hmins[1],
				'-z': z0 - hmaxs[2], '+z': z1 - hmins[2] }
			first_clip = self.count( 'clipnodes', 8 )
			for k, key in enumerate( order ):
				axis, _, sign, _ = geometry[key]
				normal = [ 0.0, 0.0, 0.0 ]
				normal[axis] = 1.0
				p = self.add_plane( normal, grown[key] )
				inside = first_clip + k + 1 if k + 1 < len( order ) else CONTENTS_SOLID
				children = ( CONTENTS_EMPTY, inside ) if sign > 0 else ( inside, CONTENTS_EMPTY )
				self.add( 'clipnodes', struct.pack( '<i2h', p, *children ))

		model = self.count( 'models', 64 )
		self.add( 'models', struct.pack( '<9f4i3i', *mins, *maxs, 0, 0, 0, *heads, len( order ), first_face, num_faces ))
		return model
