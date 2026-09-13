# Original artwork and generator. GPL-3.0-or-later.
# Run with Python and Pillow to regenerate the embedded texture from the adjacent SVG.
from pathlib import Path
from PIL import Image, ImageDraw
import xml.etree.ElementTree as ET

asset_dir = Path(__file__).resolve().parent
size, supersample = 32, 8
circle = ET.parse(asset_dir / 'DestinationDotIcon.svg').getroot().find('{http://www.w3.org/2000/svg}circle')
x, y, r = (float(circle.attrib[key]) for key in ('cx', 'cy', 'r'))
stroke = float(circle.attrib['stroke-width'])
canvas = Image.new('RGBA', (size * supersample, size * supersample))
draw = ImageDraw.Draw(canvas)
for radius, color in ((r + stroke / 2, (24, 24, 24, 255)), (r - stroke / 2, (245, 245, 245, 255))):
    draw.ellipse(tuple(round(value * supersample) for value in (x-radius, y-radius, x+radius, y+radius)), fill=color)
pixels = canvas.resize((size, size), Image.Resampling.LANCZOS)
argb = [(a << 24) | (r << 16) | (g << 8) | b for r, g, b, a in pixels.get_flattened_data()]
header = '''/* Command & Conquer Generals Zero Hour - GPL-3.0-or-later */
#ifndef DESTINATION_DOT_ICON_H
#define DESTINATION_DOT_ICON_H

// Original vector art, rasterized with antialiasing. See DestinationDotIcon.svg.
// Neutral colour lets the renderer preserve each order's existing green/red/blue tint.
enum { DESTINATION_DOT_SIZE = 32, DESTINATION_DOT_HOTSPOT_X = 16, DESTINATION_DOT_HOTSPOT_Y = 16 };
static const unsigned int DestinationDotPixels[DESTINATION_DOT_SIZE * DESTINATION_DOT_SIZE] = {
'''
header += '\n'.join('\t' + ', '.join(f'0x{p:08X}u' for p in argb[i:i+8]) + ',' for i in range(0, len(argb), 8))
header += '\n};\n\n#endif\n'
target = asset_dir / 'DestinationDotIcon.h'
if not target.exists() or target.read_text(encoding='utf-8') != header:
    target.write_text(header, encoding='utf-8', newline='\n')
print('DestinationDotIcon.h matches the SVG rasterization.')
