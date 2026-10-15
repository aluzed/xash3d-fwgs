#!/usr/bin/env python3
"""
make_icon.py - draws installer.ico and the wizard's small image (wizard-small-*.bmp)

An amber sight on a dark slate tile: nothing of Valve's marks. Drawn at
1024 pixels and reduced to each size of the icon; the small sizes get
thicker strokes so the sight still reads at 16 pixels.

    python make_icon.py [installer.ico]   (needs Pillow)
"""
import sys
from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
AMBER = (255, 160, 40, 255)
AMBER_DARK = (200, 100, 20, 255)


def tile(size, bold):
	s = 1024
	img = Image.new('RGBA', (s, s), (0, 0, 0, 0))

	# the tile: a vertical slate gradient in a rounded square
	grad = Image.new('RGBA', (s, s))
	g = ImageDraw.Draw(grad)
	for y in range(s):
		t = y / (s - 1)
		g.line([(0, y), (s, y)], fill=(int(52 - 28 * t), int(60 - 32 * t), int(72 - 34 * t), 255))
	mask = Image.new('L', (s, s), 0)
	ImageDraw.Draw(mask).rounded_rectangle([24, 24, s - 24, s - 24], radius=190, fill=255)
	img.paste(grad, (0, 0), mask)

	d = ImageDraw.Draw(img)
	c = s // 2
	w = 92 if bold else 56          # ring and line width
	r = 300                         # ring radius

	# a soft glow under the sight
	glow = Image.new('RGBA', (s, s), (0, 0, 0, 0))
	ImageDraw.Draw(glow).ellipse([c - r - 70, c - r - 70, c + r + 70, c + r + 70], outline=(255, 150, 30, 60), width=w + 80)
	img.alpha_composite(glow)

	d.ellipse([c - r, c - r, c + r, c + r], outline=AMBER, width=w)

	# four lines through the ring, a gap in the middle
	gap, end = 120 if bold else 110, r + 150
	for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
		x0, y0 = c + dx * gap, c + dy * gap
		x1, y1 = c + dx * end, c + dy * end
		box = [min(x0, x1) - (w // 2 if dy else 0), min(y0, y1) - (w // 2 if dx else 0),
			max(x0, x1) + (w // 2 if dy else 0), max(y0, y1) + (w // 2 if dx else 0)]
		d.rectangle(box, fill=AMBER)

	dot = 58 if bold else 44
	d.ellipse([c - dot, c - dot, c + dot, c + dot], fill=AMBER_DARK if not bold else AMBER)

	return img.resize((size, size), Image.LANCZOS)


def main():
	out = sys.argv[1] if len(sys.argv) > 1 else 'installer.ico'
	images = [tile(size, size <= 48) for size in SIZES]
	images[-1].save(out, format='ICO', sizes=[(i.width, i.height) for i in images], append_images=images[:-1])

	# the small image at the top right of the wizard pages, 100% and 200% scale
	for px in (55, 110):
		bmp = Image.new('RGB', (px, px), (255, 255, 255))
		icon = tile(px, px <= 64)
		bmp.paste(icon, (0, 0), icon)
		bmp.save('wizard-small-%d.bmp' % px)


if __name__ == '__main__':
	main()
