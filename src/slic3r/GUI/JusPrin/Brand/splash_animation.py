#!/usr/bin/env python3
"""Draw the name animation as animated WebP files, in two versions.

The nozzle prints the name of the splash lockup one layer at a time, sweeping
across each layer and alternating direction, then drops the dot on the i and
parks where the logo has it.

- The splash's version loops: the finished logo holds briefly, the name fades,
  and printing starts again. It starts with the second-to-last layer: the
  splash is up for anything from under a second to twenty, so the first thing
  anyone sees is the name finishing and the dot going on.
- The About window's version plays once, at the same speed, from an empty
  name to the finished logo, and stays there: people stay on the About window
  long enough to watch the whole name print. The repository's README shows
  the same once-through print on the whole horizontal lockup, cone included.

The file covers only the name: the box component.splash.nameLeft, nameTop,
nameWidth and nameHeight in resources/jusprin/ui/design-tokens.json, inside the
splash's 480x480 lockup from brand_logo.py. On macOS and Windows the splash
plays it there over the still lockup. Rendered at twice the box's size for
Retina screens.

The About window plays its version, scaled down, over the name of the
horizontal lockup brand_logo.py draws. Its box is component.about.nameLeft,
nameTop, nameWidth and nameHeight, from the lockup's top-left at the About's
lockupHeight; about_box() works it out, and the run stops if the tokens differ.

Writes resources/jusprin/startup/splash-name-light.webp and -dark.webp, and
about-name-light.webp and -dark.webp, and .github/lockup-printing-light.webp
and -dark.webp for the README.
Needs what brand_logo.py needs, plus rsvg-convert and img2webp (libwebp):

  /tmp/logo-venv/bin/python src/slic3r/GUI/JusPrin/Brand/splash_animation.py
"""
import json, math, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brand_logo as B
import pathops

OUT = os.path.join(B.ROOT, 'resources/jusprin/startup')
README_OUT = os.path.join(B.ROOT, '.github')
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

def about_box():
    """The splash's box carried over to the About window's horizontal lockup,
    in whole DIPs: the same place relative to the name, at that name's size."""
    about = json.load(open(TOKENS))['component']['about']
    W, H, k = B.ROW
    rn, rx, ry = B.row_name(k, *B.row_offsets(W, H, k))
    s = about['lockupHeight'] / H * rn / n
    return tuple(round(v) for v in ((rx + (BOX[0] - NAME_TX) / n * rn) * about['lockupHeight'] / H,
                                    (ry + (BOX[1] - NAME_TY) / n * rn) * about['lockupHeight'] / H,
                                    BOX[2] * s, BOX[3] * s))

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
    for i in range(HOLD):
        yield (layers, None, 1, PARK_X, PARK_Y, 1)
    for i in range(1, FADE + 1):
        yield (layers, None, 1, PARK_X, PARK_Y, 1 - i / FADE)

HOLD, FADE = 24, 9
# The splash's first frame: the second-to-last layer starting.
START = 6 + 8 + 5 * 8

def splash_frames():
    frames = list(states())
    return frames[START:] + frames[:START]

def about_frames():
    """From the empty name to the nozzle parked, the last frame the still logo."""
    return list(states())[:-(HOLD + FADE)]

def name_body(state, fill, n, tx, ty):
    """The name and carriage of one frame, the name's origin at tx, ty and its
    units scaled by n."""
    done, part, dot_scale, nx, ny, alpha = state
    pieces = done + ([part] if part is not None else [])
    name = []
    if pieces:
        name.append('<path fill="%s" d="%s"/>' % (fill, B.d(B.union(*pieces), n, tx, ty)))
    if dot_scale > 0:
        b = dot.bounds
        cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        s = n * dot_scale
        name.append('<path fill="%s" d="%s"/>' % (fill, B.d(dot, s, tx + cx * (n - s), ty + cy * (n - s))))
    body = ''.join(name) if alpha >= 1 else '<g opacity="%.3f">%s</g>' % (alpha, ''.join(name))
    return body + '<path fill="%s" d="%s"/>' % (fill, B.d(carriage, n, tx + (nx - PARK_X) * n, ty + (ny - PARK_Y) * n))

def frame_svg(state, fill):
    return B.svg(BOX[2], BOX[3], name_body(state, fill, n, NAME_TX - BOX[0], NAME_TY - BOX[1]))

# The README's logo: the whole horizontal lockup, 180x64 as brand_logo.py
# draws it, widened for the carriage's swing past the name's right edge.
README_W = 240                        # how wide the README shows the 180-wide lockup
def readme_geometry():
    W, H, k = B.ROW
    ox, oy = B.row_offsets(W, H, k)
    rn, rx, ry = B.row_name(k, ox, oy)
    right = max(nx for _, _, _, nx, _, _ in states()) + carriage.bounds[2] - PARK_X
    return W, H, k, ox, oy, rn, rx, ry, max(W, math.ceil(rx + right * rn))

def readme_svg(state, fill):
    W, H, k, ox, oy, rn, rx, ry, width = readme_geometry()
    c = 96 / 118 * k
    return B.svg(width, H, B.mark_el(c, ox - 32 * c, oy - 2 * c, 'gradient') + '\n' + name_body(state, fill, rn, rx, ry))

def build(frames, draw, size, fill, loops, out):
    """draw makes one frame's SVG, size is its pixels; loops is how many times
    the file plays, 0 for ever."""
    with tempfile.TemporaryDirectory() as tmp:
        pngs = []
        for i, state in enumerate(frames):
            svg = os.path.join(tmp, '%03d.svg' % i)
            open(svg, 'w').write(draw(state, fill))
            pngs.append(svg[:-4] + '.png')
            subprocess.run(['rsvg-convert', '-w', str(size[0]), '-h', str(size[1]), svg, '-o', pngs[-1]], check=True)
        subprocess.run(['img2webp', '-loop', str(loops), '-d', str(round(1000 / FPS)), '-lossy', '-q', '90', '-min_size']
                       + pngs + ['-o', out], check=True, capture_output=True)

if __name__ == '__main__':
    about = json.load(open(TOKENS))['component']['about']
    tokens = (about['nameLeft'], about['nameTop'], about['nameWidth'], about['nameHeight'])
    if about_box() != tokens:
        sys.exit('component.about name box is %s; the lockup puts it at %s' % (tokens, about_box()))
    os.makedirs(OUT, exist_ok=True)
    width, height = readme_geometry()[-1], B.ROW[1]
    readme_size = tuple(round(v * README_W / B.ROW[0] * SCALE) for v in (width, height))
    for theme, fill in (('light', B.ORBIT), ('dark', B.MIST)):
        box = (BOX[2] * SCALE, BOX[3] * SCALE)
        build(splash_frames(), frame_svg, box, fill, 0, os.path.join(OUT, 'splash-name-%s.webp' % theme))
        build(about_frames(), frame_svg, box, fill, 1, os.path.join(OUT, 'about-name-%s.webp' % theme))
        build(about_frames(), readme_svg, readme_size, fill, 1, os.path.join(README_OUT, 'lockup-printing-%s.webp' % theme))
    print('README width: %d' % round(width * README_W / B.ROW[0]))
