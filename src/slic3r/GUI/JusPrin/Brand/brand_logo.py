#!/usr/bin/env python3
"""Draw every JusPrin logo file from one geometry.

The mark is a soft-serve cone: four rounded print layers on a waffle cone with
a curl on top. The name beside it is HarmonyOS Sans SC Bold cut into the same
layers, with a small nozzle above the i printing its dot. Concepts and the
choice are on the JusPrin Logo Concepts canvas
(https://claude.ai/artifact/Qroq8EdcQDSPkCAyFceDtQ, boards FSoftServe and
NameOptions row C).

Output is flat paths filled with a colour or one linear gradient, so nanosvg,
the application's SVG renderer, draws it without masks or text elements.

Writes resources/jusprin/branding/*.svg, resources/jusprin/overlay/images/*,
resources/jusprin/images/JusPrin.icns and .ico, and the AppImage icon.
Needs skia-pathops and fonttools, plus rsvg-convert, iconutil (macOS) and
ImageMagick for the raster icons:

  python3 -m venv /tmp/logo-venv && /tmp/logo-venv/bin/pip install skia-pathops fonttools
  /tmp/logo-venv/bin/python src/slic3r/GUI/JusPrin/Brand/brand_logo.py
"""
import os, shutil, subprocess, tempfile
import pathops
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), *['..'] * 5))
BRANDING = os.path.join(ROOT, 'resources/jusprin/branding')
OVERLAY = os.path.join(ROOT, 'resources/jusprin/overlay/images')
ICONS = os.path.join(ROOT, 'resources/jusprin/images')
APPIMAGE_ICON = os.path.join(ROOT, 'src/slic3r/GUI/JusPrin/Packaging/JusPrin.png')
FONT = os.path.join(ROOT, 'resources/fonts/HarmonyOS_Sans_SC_Bold.ttf')
ORBIT, BLOOM, INK, MIST, GREY = '#3A2D64', '#CE84B7', '#17131F', '#F6F2F7', '#262E30'
K = 0.5522847498

def union(*ps):
    b = pathops.OpBuilder(fix_winding=True)
    for p in ps: b.add(p, pathops.PathOp.UNION)
    return b.resolve()
def op(a, b, kind):
    return pathops.op(a, b, kind, fix_winding=True)
def rrect(x, y, w, h, r):
    p = pathops.Path(); k = r * K
    p.moveTo(x + r, y); p.lineTo(x + w - r, y); p.cubicTo(x + w - r + k, y, x + w, y + r - k, x + w, y + r)
    p.lineTo(x + w, y + h - r); p.cubicTo(x + w, y + h - r + k, x + w - r + k, y + h, x + w - r, y + h)
    p.lineTo(x + r, y + h); p.cubicTo(x + r - k, y + h, x, y + h - r + k, x, y + h - r)
    p.lineTo(x, y + r); p.cubicTo(x, y + r - k, x + r - k, y, x + r, y); p.close(); return p
def rect(x, y, w, h):
    p = pathops.Path(); p.moveTo(x, y); p.lineTo(x + w, y); p.lineTo(x + w, y + h); p.lineTo(x, y + h); p.close(); return p
def line(x1, y1, x2, y2, w, cap):
    p = pathops.Path(); p.moveTo(x1, y1); p.lineTo(x2, y2)
    p.stroke(w, cap, pathops.LineJoin.ROUND_JOIN, 4); p.convertConicsToQuads(); return p

# Soft-serve cone, 120x120 units (FSoftServe.dc.html on the logo canvas).
cone = pathops.Path(); cone.moveTo(38, 70); cone.lineTo(82, 70); cone.lineTo(64, 112); cone.quadTo(60, 118, 56, 112); cone.close()
for a in [(46, 70, 68, 104), (62, 70, 76, 91), (74, 70, 52, 104), (58, 70, 44, 91)]:
    cone = op(cone, line(*a, 3, pathops.LineCap.BUTT_CAP), pathops.PathOp.DIFFERENCE)
curl = pathops.Path(); curl.moveTo(58, 17); curl.cubicTo(56, 10, 60, 4, 68, 5)
curl.stroke(5, pathops.LineCap.ROUND_CAP, pathops.LineJoin.ROUND_JOIN, 4); curl.convertConicsToQuads()
MARK = union(cone, rrect(34, 58, 52, 10, 5), rrect(39, 44, 42, 10, 5), rrect(45, 30, 30, 10, 5), rrect(51, 16, 18, 10, 5), curl)

# Layered name, 330 units wide, baseline at y=70, 10 units per layer
# (NameOptions.dc.html, row C).
f = TTFont(FONT); gs = f.getGlyphSet(); cmap = f.getBestCmap(); s = 10 / 107
letters = pathops.Path(); x = 0
for ch in 'JusPrin':
    g = cmap[ord(ch)]
    gs[g].draw(TransformPen(letters.getPen(), (s, 0, 0, -s, x * s, 70)))
    x += f['hmtx'][g][0]
letters = union(letters)
stripes = union(*[rect(-5, 70 - (10 * k + 7), 340, 7) for k in range(7)])
name = op(letters, stripes, pathops.PathOp.INTERSECTION)
name = op(name, rect(252, 11, 19, 5), pathops.PathOp.DIFFERENCE)  # sliver of the i dot below its layer
nozzle = pathops.Path(); nozzle.moveTo(255, -15); nozzle.lineTo(267.8, -15); nozzle.lineTo(263.6, -6.5); nozzle.quadTo(261.4, -3, 259.2, -6.5); nozzle.close()
NAME = union(name, rrect(250.9, -25, 21, 10, 2.5), nozzle)

def d(path, sx, tx, ty):
    pen = SVGPathPen(None, lambda v: ('%.2f' % v).rstrip('0').rstrip('.'))
    path.draw(TransformPen(pen, (sx, 0, 0, sx, tx, ty)))
    return pen.getCommands()
def grad(gid, sx, tx, ty):
    return ('<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f">'
            '<stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
            % (gid, 20 * sx + tx, ty, 100 * sx + tx, 120 * sx + ty, ORBIT, BLOOM))
def mark_el(sx, tx, ty, fill):
    if fill == 'gradient':
        return '<defs>%s</defs><path fill="url(#g)" d="%s"/>' % (grad('g', sx, tx, ty), d(MARK, sx, tx, ty))
    return '<path fill="%s" d="%s"/>' % (fill, d(MARK, sx, tx, ty))
def svg(w, h, body):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="%g" height="%g" viewBox="0 0 %g %g">\n%s\n</svg>\n'
            % (w, h, w, h, body))
def write(folder, name, text):
    open(os.path.join(folder, name), 'w').write(text)

# Cone crop used by lockups: x 32..88, y 2..120 (as on the canvas boards).
def lockup_row(W, H, k, ox, oy, name_fill):
    c = 96 / 118 * k; n = 74.86 / 98 * k
    return (mark_el(c, ox - 32 * c, oy - 2 * c, 'gradient') + '\n'
            + '<path fill="%s" d="%s"/>' % (name_fill, d(NAME, n, ox + 65.56 * k, oy + 10.57 * k + 26 * n)))
def lockup_stack(W, H, cone_h, name_w, gap, top, name_fill):
    c = cone_h / 118; n = name_w / 330
    cx = (W - 56 * c) / 2
    return (mark_el(c, cx - 32 * c, top - 2 * c, 'gradient') + '\n'
            + '<path fill="%s" d="%s"/>' % (name_fill, d(NAME, n, (W - name_w) / 2, top + cone_h + gap + 26 * n)))

def raster_icons(icon_svg):
    with tempfile.TemporaryDirectory() as tmp:
        master = os.path.join(tmp, 'icon.svg')
        open(master, 'w').write(icon_svg)
        def png(size):
            out = os.path.join(tmp, '%d.png' % size)
            if not os.path.exists(out):
                subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size), master, '-o', out], check=True)
            return out
        iconset = os.path.join(tmp, 'JusPrin.iconset'); os.mkdir(iconset)
        for size in (16, 32, 128, 256, 512):
            shutil.copy(png(size), os.path.join(iconset, 'icon_%dx%d.png' % (size, size)))
            shutil.copy(png(size * 2), os.path.join(iconset, 'icon_%dx%d@2x.png' % (size, size)))
        subprocess.run(['iconutil', '-c', 'icns', iconset, '-o', os.path.join(ICONS, 'JusPrin.icns')], check=True)
        subprocess.run(['magick'] + [png(s) for s in (16, 24, 32, 48, 64, 72, 96, 128, 256)] + [os.path.join(ICONS, 'JusPrin.ico')], check=True)
        subprocess.run(['magick'] + [png(s) for s in (16, 32, 48, 64, 128, 256)] + [os.path.join(OVERLAY, 'OrcaSlicer-mac_256px.ico')], check=True)
        shutil.copy(png(128), os.path.join(OVERLAY, 'OrcaSlicer_128px.png'))
        shutil.copy(png(192), os.path.join(OVERLAY, 'OrcaSlicer_192px.png'))
        shutil.copy(png(256), APPIMAGE_ICON)

if __name__ == '__main__':
    # Source marks, 120x120.
    write(BRANDING, 'jusprin-mark.svg', svg(120, 120, mark_el(1, 0, 0, 'gradient')))
    write(BRANDING, 'jusprin-mark-ink.svg', svg(120, 120, mark_el(1, 0, 0, INK)))
    write(BRANDING, 'jusprin-mark-white.svg', svg(120, 120, mark_el(1, 0, 0, '#FFFFFF')))
    # Horizontal lockup, 180x64 like the file it replaces.
    k = 172 / 317.6
    for suf, fill in (('light', ORBIT), ('dark', MIST)):
        write(OVERLAY, 'OrcaSlicer_horizontal_%s.svg' % suf, svg(180, 64, lockup_row(180, 64, k, 4, (64 - 96 * k) / 2, fill)))
    # About lockup 108x125 and splash 480x480, stacked.
    for suf, fill in (('', ORBIT), ('_dark', MIST)):
        write(OVERLAY, 'OrcaSlicer_about%s.svg' % suf, svg(108, 125, lockup_stack(108, 125, 54.6, 100, 6, 17.6, fill)))
        write(OVERLAY, 'splash_logo%s.svg' % suf, svg(480, 480, lockup_stack(480, 480, 147.5, 270, 16, 88, fill)))
    # 64x64 marks: grey for message dialogs, 10% grey wizard watermark, colour for OrcaSlicer.svg.
    c = 64 / 120
    write(OVERLAY, 'OrcaSlicer_192px_grayscale.svg', svg(64, 64, mark_el(c, 0, 0, GREY)))
    write(OVERLAY, 'OrcaSlicer_192px_transparent.svg', svg(64, 64, '<g opacity="0.10">%s</g>' % mark_el(c, 0, 0, GREY)))
    write(OVERLAY, 'OrcaSlicer.svg', svg(64, 64, mark_el(c, 0, 0, 'gradient')))
    # App icon master, 1024x1024: the existing light tile (colours sampled from
    # the current JusPrin.icns) with the gradient cone.
    b = MARK.bounds; ch = 0.62 * 816; c = ch / (b[3] - b[1])
    tx = 512 - (b[0] + b[2]) / 2 * c; ty = 512 - (b[1] + b[3]) / 2 * c
    icon = ('<defs><linearGradient id="t" gradientUnits="userSpaceOnUse" x1="144" y1="144" x2="920" y2="920">'
            '<stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#B9A5A5"/></linearGradient>'
            '<filter id="sh" x="-10%" y="-10%" width="120%" height="125%"><feGaussianBlur stdDeviation="7"/></filter>'
            + grad('g', c, tx, ty) + '</defs>'
            '<rect x="104" y="112" width="816" height="816" rx="148" fill="#000000" opacity="0.45" filter="url(#sh)"/>'
            '<rect x="104" y="104" width="816" height="816" rx="148" fill="#8A8181"/>'
            '<rect x="104" y="104" width="816" height="812" rx="148" fill="url(#t)"/>'
            '<path fill="url(#g)" d="%s"/>' % d(MARK, c, tx, ty))
    raster_icons(svg(1024, 1024, icon))
