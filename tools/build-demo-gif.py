"""Assemble the frames from tools/record-demo.ps1 into docs/demo.gif.

    python tools/build-demo-gif.py demo-frames docs/demo.gif

Two things matter for size and honesty:

  * Consecutive frames that are the same picture must not each cost a frame.
    The paused hold and the windowed page reload are captured at 10fps but are
    visually static, so they collapse into one frame carrying an accumulated
    delay - the hold is still *shown* as a hold, it just is not re-encoded 15
    times. Collapsing is what tells the difference between "the GIF stopped"
    and "the capture stalled".
  * The result has to stay small enough for a repo. Downscale, then quantise.

Needs Pillow (`pip install pillow`). A 620px-wide 8.5s clip of this content
comes out around 4MB - the animated GIF inside the cut-out changes every frame,
so inter-frame compression has little to work with.
"""

import glob
import os
import sys

from PIL import Image, ImageChops

SRC = sys.argv[1] if len(sys.argv) > 1 else "demo-frames"
OUT = sys.argv[2] if len(sys.argv) > 2 else "demo.gif"
WIDTH = int(sys.argv[3]) if len(sys.argv) > 3 else 620
COLORS = int(sys.argv[4]) if len(sys.argv) > 4 else 160
BASE_MS = 100              # the capture cadence (10fps)
SAME_FRACTION = 0.001      # below this, treat the frame as a repeat


def same(a, b):
    """True when two frames are the same picture at capture resolution."""
    # Compare on a quarter-size copy: fast, and immune to the odd antialiasing
    # pixel that a full-resolution compare would flag.
    sa = a.resize((a.width // 4, a.height // 4), Image.BILINEAR).convert("RGB")
    sb = b.resize((b.width // 4, b.height // 4), Image.BILINEAR).convert("RGB")
    diff = ImageChops.difference(sa, sb)
    # Anything brighter than 24 in any channel counts as changed.
    changed = diff.convert("L").point(lambda v: 255 if v > 24 else 0)
    return (changed.histogram()[255] / float(sa.width * sa.height)) < SAME_FRACTION


paths = sorted(glob.glob(os.path.join(SRC, "f*.png")))
if not paths:
    raise SystemExit("no frames in " + SRC)

kept = []
delays = []
prev = None
for p in paths:
    im = Image.open(p).convert("RGB")
    if prev is not None and same(prev, im):
        delays[-1] += BASE_MS          # still on screen, just longer
        prev = im
        continue
    kept.append(im)
    delays.append(BASE_MS)
    prev = im

print("frames {0} -> {0} kept {1} (collapsed {2})".format(len(paths), len(kept), len(paths) - len(kept)))

scale = WIDTH / float(kept[0].width)
size = (WIDTH, int(round(kept[0].height * scale)))

# Optional dump of the deduped frames plus their delays, so an encoder with a
# better global-palette pass (ImageMagick) can take over from here.
if len(sys.argv) > 5 and sys.argv[5] == "dump":
    dst = SRC + "_kept"
    os.makedirs(dst, exist_ok=True)
    for i, im in enumerate(kept):
        im.resize(size, Image.LANCZOS).save(os.path.join(dst, "k{0:04d}.png".format(i)))
    with open(os.path.join(dst, "delays.txt"), "w") as f:
        f.write(" ".join(str(d) for d in delays))
    print("dumped {0} frames + delays.txt to {1}".format(len(kept), dst))
    raise SystemExit(0)

pal = [im.resize(size, Image.LANCZOS).convert("P", palette=Image.ADAPTIVE, colors=COLORS)
       for im in kept]

pal[0].save(
    OUT,
    save_all=True,
    append_images=pal[1:],
    duration=delays,
    loop=0,
    optimize=True,
    disposal=1,
)
print("{0}: {1}x{2}, {3} frames, {4:.2f} MB, total {5:.1f}s".format(
    OUT, size[0], size[1], len(pal), os.path.getsize(OUT) / 1048576.0, sum(delays) / 1000.0))
