"""Draws the BayesTS Studio icon: a time series running into a fan chart --
the median forecast in white, its 68% and 90% bands in gold -- on a deep
blue tile with a soft sheen from above and a fine light rim.

One geometry, on a 256-unit square, gives every file:

  src/app/icons/bayests-studio.svg       the scalable master
  src/app/icons/bayests-studio-<n>.png   the window icon, n = 16 ... 256
  src/app/icons/bayests-studio.ico       the executable's and the setup's
  packaging/msix/Assets/*.png            the MSIX logos

Sizes up to 24 pixels are drawn with heavier strokes, a simpler history and
without the sheen, the rim and the line's shadow, which at that size only
blur it: the fan has to read in a title bar. Run from the repository root:

  python tools/make_icon.py
"""

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "src" / "app" / "icons"
MSIX = ROOT / "packaging" / "msix" / "Assets"

# The tile: the studio's blue, lit from above.
TILE_TOP = (0x2F, 0x67, 0xB2)
TILE_BOTTOM = (0x0C, 0x24, 0x4A)
# The bands: gold, the inner one deeper, each lighter towards its top.
BAND90_TOP, BAND90_BOTTOM = (0xFB, 0xE3, 0xA6), (0xF0, 0xC0, 0x6A)
BAND68_TOP, BAND68_BOTTOM = (0xF8, 0xB9, 0x3A), (0xD6, 0x84, 0x00)
LINE = (0xFF, 0xFF, 0xFF)

ORIGIN = (128.0, 134.0)         # where the history ends and the forecast starts
RADIUS = 56


def geometry(small: bool):
    """The shapes, in 256 units: the two bands as polygons, the line as a
    polyline and its width."""
    if small:
        history = [(34, 150), (82, 120), (128, 134)]
        width = 26
    else:
        history = [(34, 152), (58, 136), (82, 150), (106, 122), (128, 134)]
        width = 13
    end = 222
    # The bands widen with the square root of the horizon, as a forecast's
    # do, around a median that rises.
    x0, y0 = ORIGIN
    rise = 22.0
    steps = 24

    def band(half):
        xs = [x0 + (end - x0) * i / steps for i in range(steps + 1)]
        mid = [y0 - rise * (x - x0) / (end - x0) for x in xs]
        widths = [half * ((x - x0) / (end - x0)) ** 0.5 for x in xs]
        upper = [(x, m - w) for x, m, w in zip(xs, mid, widths)]
        lower = [(x, m + w) for x, m, w in zip(xs, mid, widths)]
        return upper + lower[::-1]

    return {"band90": band(64.0), "band68": band(32.0), "line": history + [(end, y0 - rise)], "width": width}


def vertical(n, top, bottom, y0=0.0, y1=1.0):
    """An n x n image running from `top` at height y0 to `bottom` at y1 (as
    fractions of n), flat beyond them."""
    ramp = Image.linear_gradient("L").resize((1, 256))
    lo, hi = int(y0 * 255), int(y1 * 255)
    lut = [0 if v <= lo else 255 if v >= hi else round(255 * (v - lo) / max(1, hi - lo)) for v in range(256)]
    mask = ramp.point(lut).resize((n, n))
    return Image.composite(Image.new("RGBA", (n, n), bottom + (255,)), Image.new("RGBA", (n, n), top + (255,)), mask)


def shape(n, draw):
    """A mask: what `draw(ImageDraw)` fills, in white."""
    m = Image.new("L", (n, n), 0)
    draw(ImageDraw.Draw(m))
    return m


def render(size: int) -> Image.Image:
    """The icon at `size` pixels, drawn eight times larger and reduced, which
    smooths every edge without a vector renderer."""
    small = size <= 24
    g = geometry(small)
    ss = 8
    n = size * ss
    k = n / 256.0
    scale = lambda pts: [(x * k, y * k) for x, y in pts]

    tile = shape(n, lambda d: d.rounded_rectangle([0, 0, n - 1, n - 1], radius=RADIUS * k, fill=255))
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    img.paste(vertical(n, TILE_TOP, TILE_BOTTOM), (0, 0), tile)

    if not small:
        # A sheen from above: white, fading out by the middle of the tile.
        sheen = vertical(n, (255, 255, 255), (255, 255, 255))
        fade = Image.linear_gradient("L").resize((n, n)).point(lambda v: max(0, 34 - v * 34 // 128))
        sheen.putalpha(ImageChops.multiply(fade, tile))
        img = Image.alpha_composite(img, sheen)

    # The bands, each a gradient through its own outline, top lighter.
    for poly, top, bottom in ((g["band90"], BAND90_TOP, BAND90_BOTTOM), (g["band68"], BAND68_TOP, BAND68_BOTTOM)):
        pts = scale(poly)
        ys = [y for _, y in pts]
        fill = vertical(n, top, bottom, min(ys) / n, max(ys) / n)
        img.paste(fill, (0, 0), shape(n, lambda d: d.polygon(pts, fill=255)))

    pts = scale(g["line"])
    w = g["width"] * k

    def stroke(d, colour):
        d.line(pts, fill=colour, width=round(w), joint="curve")
        # Round caps, which ImageDraw.line does not draw.
        for x, y in (pts[0], pts[-1]):
            d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=colour)

    if not small:
        # The line's shadow: soft, a little below it, within the tile.
        line = shape(n, lambda d: stroke(d, 255))
        blur = line.filter(ImageFilter.GaussianBlur(6 * k))
        offset = Image.new("L", (n, n), 0)
        offset.paste(blur, (0, round(4 * k)))
        alpha = ImageChops.multiply(offset.point(lambda v: v * 95 // 255), tile)
        shadow = Image.new("RGBA", (n, n), (4, 14, 34, 0))
        shadow.putalpha(alpha)
        img = Image.alpha_composite(img, shadow)

    d = ImageDraw.Draw(img)
    stroke(d, LINE + (255,))

    if not small:
        # A fine light rim, the edge of the tile catching the light.
        rim = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        ImageDraw.Draw(rim).rounded_rectangle([k, k, n - 1 - k, n - 1 - k], radius=(RADIUS - 1) * k,
                                              outline=(255, 255, 255, 46), width=max(1, round(2 * k)))
        img = Image.alpha_composite(img, rim)
    return img.resize((size, size), Image.LANCZOS)


def svg() -> str:
    g = geometry(small=False)
    hexc = lambda c: "#%02x%02x%02x" % c
    poly = lambda p: " ".join("%.1f,%.1f" % xy for xy in p)
    span = lambda p: (min(y for _, y in p), max(y for _, y in p))
    t90, b90 = span(g["band90"])
    t68, b68 = span(g["band68"])
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <!-- BayesTS Studio: a time series running into a fan chart. Generated by tools/make_icon.py. -->
  <defs>
    <linearGradient id="tile" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{hexc(TILE_TOP)}"/><stop offset="1" stop-color="{hexc(TILE_BOTTOM)}"/>
    </linearGradient>
    <linearGradient id="sheen" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.13"/><stop offset="0.5" stop-color="#ffffff" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="band90" gradientUnits="userSpaceOnUse" x1="0" y1="{t90:.1f}" x2="0" y2="{b90:.1f}">
      <stop offset="0" stop-color="{hexc(BAND90_TOP)}"/><stop offset="1" stop-color="{hexc(BAND90_BOTTOM)}"/>
    </linearGradient>
    <linearGradient id="band68" gradientUnits="userSpaceOnUse" x1="0" y1="{t68:.1f}" x2="0" y2="{b68:.1f}">
      <stop offset="0" stop-color="{hexc(BAND68_TOP)}"/><stop offset="1" stop-color="{hexc(BAND68_BOTTOM)}"/>
    </linearGradient>
    <filter id="shadow" x="-10%" y="-10%" width="120%" height="140%">
      <feDropShadow dx="0" dy="4" stdDeviation="6" flood-color="#040e22" flood-opacity="0.37"/>
    </filter>
  </defs>
  <rect width="256" height="256" rx="{RADIUS}" fill="url(#tile)"/>
  <rect width="256" height="256" rx="{RADIUS}" fill="url(#sheen)"/>
  <polygon points="{poly(g['band90'])}" fill="url(#band90)"/>
  <polygon points="{poly(g['band68'])}" fill="url(#band68)"/>
  <polyline points="{poly(g['line'])}" fill="none" stroke="{hexc(LINE)}" stroke-width="{g['width']}"
            stroke-linecap="round" stroke-linejoin="round" filter="url(#shadow)"/>
  <rect x="1" y="1" width="254" height="254" rx="{RADIUS - 1}" fill="none" stroke="#ffffff" stroke-opacity="0.18" stroke-width="2"/>
</svg>
"""


def main():
    ICONS.mkdir(parents=True, exist_ok=True)
    (ICONS / "bayests-studio.svg").write_text(svg(), encoding="utf-8")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    images = {n: render(n) for n in sizes}
    for n in (16, 24, 32, 48, 64, 128, 256):
        images[n].save(ICONS / f"bayests-studio-{n}.png")
    # Every size its own drawing, rather than one reduced by the encoder.
    images[256].save(ICONS / "bayests-studio.ico", format="ICO", sizes=[(n, n) for n in sizes],
                     append_images=[images[n] for n in sizes if n != 256])
    for name, n in (("StoreLogo.png", 50), ("Square44x44Logo.png", 44), ("Square150x150Logo.png", 150)):
        render(n).save(MSIX / name)
    print("wrote", ICONS, "and", MSIX)


if __name__ == "__main__":
    main()
