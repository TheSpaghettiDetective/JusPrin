#!/usr/bin/env python3
"""Draw the splash's name animation as animated WebP files.

The nozzle prints the name of the splash lockup one layer at a time, sweeping
across each layer and alternating direction, then drops the dot on the i and
parks where the logo has it. The finished logo holds briefly, the name fades,
and printing starts again. The file starts with the second-to-last layer: the
splash is often up for under a second, so the first thing anyone sees is the
name finishing and the dot going on.

The file covers only the name: the box component.splash.nameLeft, nameTop,
nameWidth and nameHeight in resources/jusprin/ui/design-tokens.json, inside the
splash's 480x480 lockup from brand_logo.py. On macOS the splash plays it there
over the still lockup. Rendered at twice the box's size for Retina screens.

Writes resources/jusprin/startup/splash-name-light.webp and -dark.webp.
Needs what brand_logo.py needs, plus rsvg-convert and img2webp (libwebp):

  /tmp/logo-venv/bin/python src/slic3r/GUI/JusPrin/Brand/splash_animation.py
"""
import json, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brand_logo as B
import pathops

OUT = os.path.join(B.ROOT, 'resources/jusprin/startup')
TOKENS = os.path.join(B.ROOT, 'resources/jusprin/ui/design-tokens.json')
FPS = 30
SCALE = 2

splash = json.load(open(TOKENS))['component']['splash']
SIZE = splash['size']
BOX = (splash['nameLeft'], splash['nameTop'], splash['nameWidth'], splash['nameHeight'])
CONE_H, NAME_W, GAP, TOP = B.SPLASH_STACK
n = NAME_W / 330                      # brand_logo's name is 330 units wide
NAME_TX, NAME_TY = (SIZE - NAME_W) / 2, TOP + CONE_H + GAP + 26 * n
INTER, DIFF = pathops.PathOp.INTERSECTION, pathops.PathOp.DIFFERENCE

# The name's print layers, bottom first, with the i's dot taken out of the top one.
DOT_BOX = B.rect(248, -5, 26, 20)
layers = [B.op(B.name, B.rect(-5, 70 - (10 * k + 7), 340, 7), INTER) for k in range(7)]
dot = B.op(layers[6], DOT_BOX, INTER)
layers[6] = B.op(layers[6], DOT_BOX, DIFF)
carriage = B.union(B.rrect(250.9, -25, 21, 10, 2.5), B.nozzle)
PARK_X, PARK_Y = 261.4, -4.75         # the nozzle tip where the logo has it
DOT_TOP = dot.bounds[1]

def ease(t): return t * t * (3 - 2 * t)
def lerp(a, b, t): return a + (b - a) * t
def layer_top(k): return 70 - (10 * k + 7)

def states():
    """One state per frame: (whole layers, part of the layer being printed,
    dot scale, nozzle tip x, y, name opacity)."""
    for i in range(6):
        yield ([], None, 0, PARK_X, PARK_Y, 1)
    x0 = layers[0].bounds[0] - 4
    for i in range(1, 9):
        t = ease(i / 8)
        yield ([], None, 0, lerp(PARK_X, x0, t), lerp(PARK_Y, layer_top(0), t), 1)
    for k in range(7):
        b = layers[k].bounds
        rightward = k % 2 == 0
        a, z = (b[0] - 4, b[2] + 4) if rightward else (b[2] + 4, b[0] - 4)
        for i in range(1, 9):
            x = lerp(a, z, i / 8)
            done = B.rect(-10, -30, x + 10, 120) if rightward else B.rect(x, -30, 400, 120)
            yield (layers[:k], B.op(layers[k], done, INTER), 0, x, layer_top(k), 1)
    last = layers[6].bounds[0] - 4
    for i in range(1, 7):
        t = ease(i / 6)
        yield (layers, None, 0, lerp(last, PARK_X, t), lerp(layer_top(6), DOT_TOP - 3, t), 1)
    for i in range(1, 6):
        yield (layers, None, i / 5, PARK_X, DOT_TOP - 3, 1)
    for i in range(1, 7):
        t = ease(i / 6)
        yield (layers, None, 1, PARK_X, lerp(DOT_TOP - 3, PARK_Y, t), 1)
    for i in range(24):
        yield (layers, None, 1, PARK_X, PARK_Y, 1)
    for i in range(1, 10):
        yield (layers, None, 1, PARK_X, PARK_Y, 1 - i / 9)

# The first frame: the second-to-last layer starting.
START = 6 + 8 + 5 * 8

def frame_svg(state, fill):
    done, part, dot_scale, nx, ny, alpha = state
    pieces = done + ([part] if part is not None else [])
    tx, ty = NAME_TX - BOX[0], NAME_TY - BOX[1]
    name = []
    if pieces:
        name.append('<path fill="%s" d="%s"/>' % (fill, B.d(B.union(*pieces), n, tx, ty)))
    if dot_scale > 0:
        b = dot.bounds
        cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        s = n * dot_scale
        name.append('<path fill="%s" d="%s"/>' % (fill, B.d(dot, s, tx + cx * (n - s), ty + cy * (n - s))))
    body = ''.join(name) if alpha >= 1 else '<g opacity="%.3f">%s</g>' % (alpha, ''.join(name))
    body += '<path fill="%s" d="%s"/>' % (fill, B.d(carriage, n, tx + (nx - PARK_X) * n, ty + (ny - PARK_Y) * n))
    return B.svg(BOX[2], BOX[3], body)

def build(fill, out):
    frames = list(states())
    frames = frames[START:] + frames[:START]
    with tempfile.TemporaryDirectory() as tmp:
        pngs = []
        for i, state in enumerate(frames):
            svg = os.path.join(tmp, '%03d.svg' % i)
            open(svg, 'w').write(frame_svg(state, fill))
            pngs.append(svg[:-4] + '.png')
            subprocess.run(['rsvg-convert', '-w', str(BOX[2] * SCALE), '-h', str(BOX[3] * SCALE), svg, '-o', pngs[-1]], check=True)
        subprocess.run(['img2webp', '-loop', '0', '-d', str(round(1000 / FPS)), '-lossy', '-q', '90', '-min_size']
                       + pngs + ['-o', out], check=True, capture_output=True)

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for theme, fill in (('light', B.ORBIT), ('dark', B.MIST)):
        build(fill, os.path.join(OUT, 'splash-name-%s.webp' % theme))
