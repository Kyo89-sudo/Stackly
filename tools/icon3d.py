# Icone 3D di Stackly: tre varianti (A, B, C) generate a qualsiasi dimensione
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops

VIOLET = (124, 58, 237); FUCHSIA = (217, 70, 239); DEEP = (76, 29, 149); PINK = (236, 72, 153)

def blur(m, r): return m.filter(ImageFilter.GaussianBlur(max(r, 0.01)))

def shift(m, dx, dy):
    out = Image.new(m.mode, m.size, 0)
    out.paste(m, (int(round(dx)), int(round(dy))))
    return out

def squircle(S, x0, y0, x1, y1, n=5.0):
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) + 0.5
    cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    v = np.abs((xx - cx) / rx) ** n + np.abs((yy - cy) / ry) ** n
    a = np.clip((1.0 - v) * min(rx, ry) / n * 1.0 + 0.5, 0, 1)   # bordo antialias
    return Image.fromarray((a * 255).astype(np.uint8), 'L')

def grad(S, stops, angle_xy=(0.55, 0.45), y_only=False):
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    t = yy if y_only else np.clip(xx * angle_xy[0] + yy * angle_xy[1], 0, 1)
    out = np.zeros((S, S, 3), np.float32)
    pos = [p for p, _ in stops]; cols = [np.array(c, np.float32) for _, c in stops]
    for i in range(len(stops) - 1):
        a, b = pos[i], pos[i + 1]
        u = np.clip((t - a) / max(b - a, 1e-6), 0, 1)[..., None]
        m = ((t >= a) & (t <= b))[..., None] if i < len(stops) - 2 else (t >= a)[..., None]
        out = np.where(m, cols[i] + (cols[i + 1] - cols[i]) * u, out)
    out = np.where((t < pos[0])[..., None], cols[0], out)
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), 'RGB').convert('RGBA')

def layer(canvas, fill, mask, alpha=255):
    """Compone fill (colore o immagine RGBA) sul canvas usando mask (L) e alpha globale."""
    S = canvas.size[0]
    src = Image.new('RGBA', (S, S), fill) if isinstance(fill, tuple) else fill.copy()
    m = mask if alpha == 255 else mask.point(lambda v: v * alpha // 255)
    a = ImageChops.multiply(src.getchannel('A'), m)
    src.putalpha(a)
    canvas.alpha_composite(src)

def rounded(mask, r):
    """Arrotonda gli spigoli di una maschera disegnata a poligoni."""
    if r < 1: return mask
    return blur(mask, r).point(lambda v: 255 if v > 127 else 0).filter(ImageFilter.GaussianBlur(0.7))

# ---------------------------------------------------------------------------
# Piastrella 3D: ombra, spessore, faccia smussata, riflesso
# ---------------------------------------------------------------------------
def tile(S):
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    mg = S * 0.06; depth = S * 0.045
    face = squircle(S, mg, mg * 0.8, S - mg, S - mg - depth)
    side = shift(face, 0, depth)
    # ombra portata
    layer(c, (20, 0, 40, 255), blur(shift(face, 0, depth + S * 0.025), S * 0.035), 120)
    # spessore (lato inferiore), piu' scuro
    sidegrad = grad(S, [(0, (90, 30, 170)), (1, (120, 25, 130))], (1, 0))
    layer(c, sidegrad, ImageChops.lighter(side, face))
    # faccia
    fg = grad(S, [(0, (139, 92, 246)), (0.5, (168, 70, 240)), (1, (226, 72, 220))])
    layer(c, fg, face)
    # luce morbida in alto a sinistra
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    d = np.sqrt((xx - 0.25) ** 2 + (yy - 0.15) ** 2)
    glow = Image.fromarray((np.clip(1 - d / 0.75, 0, 1) ** 2 * 110).astype(np.uint8), 'L')
    layer(c, (255, 255, 255, 255), ImageChops.multiply(glow, face))
    # smussatura: bordo chiaro in alto, scuro in basso
    rim_hi = ImageChops.subtract(face, shift(face, 0, S * 0.014))
    layer(c, (255, 255, 255, 255), blur(rim_hi, S * 0.004), 150)
    rim_lo = ImageChops.subtract(face, shift(face, 0, -S * 0.012))
    layer(c, (60, 10, 110, 255), blur(rim_lo, S * 0.006), 90)
    # riflesso lucido sulla meta' alta
    gl = Image.new('L', (S, S), 0)
    ImageDraw.Draw(gl).ellipse([S * 0.02, -S * 0.55, S * 0.98, S * 0.44], fill=255)
    vert = Image.fromarray((np.clip(1 - yy / 0.45, 0, 1) * 60).astype(np.uint8), 'L')
    layer(c, (255, 255, 255, 255), ImageChops.multiply(ImageChops.multiply(blur(gl, S * 0.01), face), vert))
    return c, face

# ---------------------------------------------------------------------------
# Glifi
# ---------------------------------------------------------------------------
def stroke(d, pts, w, fill=255):
    d.line(pts, fill=fill, width=int(w), joint='curve')
    for (x, y) in (pts[0], pts[-1]):
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=fill)

# Misure piccole (tray, barra delle applicazioni, Esplora file): niente ombre sfumate, che su fondo
# chiaro diventano un alone grigio e fanno sembrare l'icona sfocata, e bordi allineati ai pixel.
CRISP = False
K = 1

def snap(v):
    return round(v / K) * K if CRISP else v

def glyph3d(c, face, gmask, S, depth, top_stops, side_col, clip=True):
    """Glifo estruso: ombra sulla faccia, spessore, faccia superiore sfumata, filo di luce."""
    if not CRISP:
        sh = blur(shift(gmask, S * 0.01, depth + S * 0.03), S * 0.025)
        layer(c, (40, 0, 80, 255), ImageChops.multiply(sh, face) if clip else sh, 110)
    steps = max(1, int(depth))
    for k in range(steps, 0, -1):
        t = k / steps
        col = tuple(int(side_col[i] * (1 - 0.25 * t)) for i in range(3)) + (255,)
        layer(c, col, shift(gmask, 0, k))
    layer(c, grad(S, top_stops, y_only=True), gmask)
    hi = ImageChops.subtract(gmask, shift(gmask, 0, S * 0.008))
    layer(c, (255, 255, 255, 255), hi, 200)

def variant_A(S, small):
    """Freccia che scende su due livelli, tutto in rilievo su piastrella 3D."""
    c, face = tile(S)
    g = Image.new('L', (S, S), 0); d = ImageDraw.Draw(g)
    cx = S / 2; w = S * (0.11 if small else 0.085)
    stroke(d, [(cx, S * 0.20), (cx, S * 0.48)], w)
    stroke(d, [(cx - S * 0.15, S * 0.35), (cx, S * 0.50), (cx + S * 0.15, S * 0.35)], w)
    stroke(d, [(cx - S * 0.25, S * 0.59), (cx, S * 0.70), (cx + S * 0.25, S * 0.59)], w)
    g2 = Image.new('L', (S, S), 0)
    if not small:
        stroke(ImageDraw.Draw(g2), [(cx - S * 0.25, S * 0.70), (cx, S * 0.81), (cx + S * 0.25, S * 0.70)], w)
        glyph3d(c, face, g2, S, S * 0.025, [(0, (233, 213, 255)), (1, (216, 180, 254))], (150, 90, 220))
    glyph3d(c, face, g, S, S * (0.02 if small else 0.03), [(0, (255, 255, 255)), (1, (240, 228, 255))], (190, 150, 245))
    return c

def slab(S, cx, cy, hw, hh, t, top, left, right, r):
    """Lastra isometrica arrotondata: faccia superiore + due fianchi."""
    sil = Image.new('L', (S, S), 0); d = ImageDraw.Draw(sil)
    d.polygon([(cx, cy - hh), (cx + hw, cy), (cx + hw, cy + t), (cx, cy + hh + t), (cx - hw, cy + t), (cx - hw, cy)], fill=255)
    sil = rounded(sil, r)
    topm = Image.new('L', (S, S), 0); ImageDraw.Draw(topm).polygon(
        [(cx, cy - hh), (cx + hw, cy), (cx, cy + hh), (cx - hw, cy)], fill=255)
    topm = ImageChops.multiply(rounded(topm, r), sil)
    lm = Image.new('L', (S, S), 0); ImageDraw.Draw(lm).rectangle([0, 0, cx, S], fill=255)
    rest = ImageChops.subtract(sil, topm)
    return sil, topm, ImageChops.multiply(rest, lm), ImageChops.subtract(rest, lm)

def iso_stack(c, S, face, cols, arrow_col, base_y, gap, hw, hh, t, white_top=False):
    cx = S / 2
    for i, cy in enumerate([base_y, base_y - gap, base_y - 2 * gap]):
        sil, top, left, right = slab(S, cx, cy, hw, hh, t, None, None, None, S * 0.018)
        sh = blur(shift(sil, 0, S * 0.03), S * 0.03)
        layer(c, (40, 0, 80, 255), ImageChops.multiply(sh, face) if face else sh, 110)
        tcol, lcol, rcol = cols[i]
        layer(c, grad(S, [(0, tcol[0]), (1, tcol[1])], (0.3, 0.7)), top)
        layer(c, lcol, left)
        layer(c, rcol, right)
        # filo di luce sul bordo frontale della faccia superiore
        edge = ImageChops.subtract(top, shift(top, 0, -S * 0.008))
        layer(c, (255, 255, 255, 255), edge, 140)
    # freccia incisa nella lastra superiore
    cy = base_y - 2 * gap
    g = Image.new('L', (S, S), 0); d = ImageDraw.Draw(g); w = S * 0.04
    stroke(d, [(cx, cy - hh * 0.62), (cx, cy + hh * 0.42)], w)
    stroke(d, [(cx - hw * 0.22, cy), (cx, cy + hh * 0.46), (cx + hw * 0.22, cy)], w)
    layer(c, (255, 255, 255, 255), shift(g, 0, S * 0.005), 120)
    layer(c, arrow_col, g)

def variant_B(S, small):
    """Pila isometrica di lastre bianche/lavanda su piastrella 3D."""
    c, face = tile(S)
    white = [((255, 255, 255), (237, 228, 255)), (205, 185, 250, 255), (170, 135, 240, 255)]
    lav = [((236, 224, 255), (214, 196, 252)), (180, 150, 245, 255), (146, 104, 230, 255)]
    iso_stack(c, S, face, [lav, lav, white], (124, 58, 237, 255), S * 0.60, S * 0.13, S * 0.30, S * 0.15, S * 0.06)
    return c

def variant_C(S, small):
    """Pila isometrica viola-fucsia senza piastrella, stile icone di Windows 11."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    l1 = [((168, 85, 247), (139, 92, 246)), (109, 40, 217, 255), (76, 29, 149, 255)]
    l2 = [((192, 80, 245), (168, 85, 247)), (147, 51, 234, 255), (107, 33, 168, 255)]
    l3 = [((244, 114, 252), (217, 70, 239)), (192, 38, 211, 255), (134, 25, 143, 255)]
    shadow = Image.new('L', (S, S), 0)
    ImageDraw.Draw(shadow).ellipse([S * 0.14, S * 0.72, S * 0.86, S * 0.94], fill=255)
    layer(c, (30, 0, 60, 255), blur(shadow, S * 0.04), 90)
    iso_stack(c, S, None, [l1, l2, l3], (255, 255, 255, 255), S * 0.66, S * 0.165, S * 0.40, S * 0.20, S * 0.085)
    return c

VARIANTS = {'A': variant_A, 'B': variant_B, 'C': variant_C}

def render(size, v):
    global CRISP, K
    k = 6 if size <= 48 else (4 if size <= 128 else 3)
    S = size * k
    CRISP, K = size <= 32, k
    try:
        img = VARIANTS[v](S, size <= 24)
    finally:
        CRISP, K = False, 1
    img = img.resize((size, size), Image.LANCZOS)
    if size <= 32:   # un filo di nitidezza dopo il ridimensionamento
        img = img.filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=0))
    return img

# ---------------------------------------------------------------------------
# Varianti "download + streaming video"
# ---------------------------------------------------------------------------
def rrect(S, box, r):
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=r, fill=255)
    return m

def tri_mask(S, pts, r):
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return rounded(m, r)

def sphere_badge(c, S, cx, cy, R, small):
    """Pallino 3D fucsia con freccia di download bianca."""
    disc = Image.new('L', (S, S), 0)
    ImageDraw.Draw(disc).ellipse([cx - R, cy - R, cx + R, cy + R], fill=255)
    layer(c, (40, 0, 70, 255), blur(shift(disc, 0, R * 0.25), R * 0.25), 130)
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    d = np.sqrt((xx - (cx - R * 0.35)) ** 2 + (yy - (cy - R * 0.4)) ** 2) / (R * 1.6)
    t = np.clip(d, 0, 1)[..., None]
    a = np.array((255, 140, 245), np.float32); b = np.array((236, 72, 153), np.float32); e = np.array((157, 23, 120), np.float32)
    col = np.where(t < 0.5, a + (b - a) * (t / 0.5), b + (e - b) * ((t - 0.5) / 0.5))
    layer(c, Image.fromarray(col.clip(0, 255).astype(np.uint8), 'RGB').convert('RGBA'), disc)
    spec = Image.new('L', (S, S), 0)
    ImageDraw.Draw(spec).ellipse([cx - R * 0.6, cy - R * 0.85, cx + R * 0.45, cy - R * 0.15], fill=255)
    layer(c, (255, 255, 255, 255), ImageChops.multiply(blur(spec, R * 0.12), disc), 110)
    g = Image.new('L', (S, S), 0); dd = ImageDraw.Draw(g); w = R * (0.32 if small else 0.24)
    stroke(dd, [(cx, cy - R * 0.5), (cx, cy + R * 0.3)], w)
    stroke(dd, [(cx - R * 0.36, cy - R * 0.02), (cx, cy + R * 0.36), (cx + R * 0.36, cy - R * 0.02)], w)
    layer(c, (120, 10, 90, 255), shift(g, 0, R * 0.08), 120)
    layer(c, (255, 255, 255, 255), g)

def progress3d(c, face, S, y, x0, x1, played, buffered, h, small):
    """Barra di streaming: traccia incassata, parte bufferizzata, parte vista, cursore 3D."""
    tr = rrect(S, [x0, y - h / 2, x1, y + h / 2], h / 2)
    layer(c, (50, 10, 100, 255), tr, 110)
    layer(c, (30, 0, 60, 255), ImageChops.subtract(tr, shift(tr, 0, h * 0.35)), 90)   # incasso
    bx = x0 + (x1 - x0) * buffered; px = x0 + (x1 - x0) * played
    layer(c, (233, 213, 255, 255), rrect(S, [x0, y - h / 2, bx, y + h / 2], h / 2), 120)
    layer(c, grad(S, [(0, (255, 255, 255)), (1, (240, 228, 255))], y_only=True), rrect(S, [x0, y - h / 2, px, y + h / 2], h / 2))
    if not small:
        kr = h * 1.25
        knob = Image.new('L', (S, S), 0)
        ImageDraw.Draw(knob).ellipse([px - kr, y - kr, px + kr, y + kr], fill=255)
        layer(c, (40, 0, 80, 255), blur(shift(knob, 0, kr * 0.4), kr * 0.4), 120)
        layer(c, grad(S, [(0, (255, 255, 255)), (1, (225, 205, 255))], y_only=True), knob)

def variant_D(S, small):
    """Piastrella 3D: grande play in rilievo, barra di streaming sotto, pallino download."""
    c, face = tile(S)
    cx = S * 0.47
    pts = [(cx - S * 0.15, S * 0.20), (cx - S * 0.15, S * 0.58), (cx + S * 0.20, S * 0.39)]
    g = tri_mask(S, pts, S * 0.03)
    glyph3d(c, face, g, S, S * 0.03, [(0, (255, 255, 255)), (1, (238, 226, 255))], (190, 150, 245))
    progress3d(c, face, S, S * 0.735, S * 0.20, S * 0.80, 0.55, 0.78, S * (0.06 if small else 0.045), small)
    if not small:
        sphere_badge(c, S, S * 0.765, S * 0.27, S * 0.12, small)
    return c

def variant_E(S, small):
    """Lettore video 3D (schermo di vetro con play e barra) e freccia fucsia che ne esce."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    x0, y0, x1, y1 = S * 0.07, S * 0.16, S * 0.93, S * 0.72
    depth = S * 0.04
    body = rrect(S, [x0, y0, x1, y1], S * 0.09)
    layer(c, (20, 0, 40, 255), blur(shift(body, 0, depth + S * 0.03), S * 0.035), 120)
    layer(c, grad(S, [(0, (90, 30, 170)), (1, (130, 25, 140))], (1, 0)), ImageChops.lighter(shift(body, 0, depth), body))
    layer(c, grad(S, [(0, (139, 92, 246)), (0.55, (168, 70, 240)), (1, (226, 72, 220))]), body)
    layer(c, (255, 255, 255, 255), blur(ImageChops.subtract(body, shift(body, 0, S * 0.012)), S * 0.004), 150)
    # schermo di vetro scuro
    m = S * 0.045
    scr = rrect(S, [x0 + m, y0 + m, x1 - m, y1 - m], S * 0.055)
    layer(c, grad(S, [(0, (46, 16, 101)), (1, (24, 8, 54))], y_only=True), scr)
    layer(c, (0, 0, 0, 255), ImageChops.subtract(scr, shift(scr, 0, S * 0.015)), 120)   # incasso
    gl = Image.new('L', (S, S), 0)
    ImageDraw.Draw(gl).polygon([(x0 + m, y0 + m), (S * 0.62, y0 + m), (S * 0.38, y1 - m), (x0 + m, y1 - m)], fill=255)
    layer(c, (255, 255, 255, 255), ImageChops.multiply(gl, scr), 22)
    # play
    cx, cy = S * 0.47, S * 0.40
    pts = [(cx - S * 0.09, cy - S * 0.12), (cx - S * 0.09, cy + S * 0.12), (cx + S * 0.13, cy)]
    g = tri_mask(S, pts, S * 0.02)
    glyph3d(c, scr, g, S, S * 0.02, [(0, (255, 255, 255)), (1, (233, 213, 255))], (170, 120, 240))
    progress3d(c, scr, S, y1 - m - S * 0.06, x0 + m + S * 0.06, x1 - m - S * 0.06, 0.45, 0.7, S * (0.045 if small else 0.028), small)
    # freccia 3D fucsia che scende dal lettore
    fa = Image.new('L', (S, S), 0); d = ImageDraw.Draw(fa)
    ax, w = S * 0.5, S * (0.13 if small else 0.10)
    stroke(d, [(ax, S * 0.56), (ax, S * 0.84)], w)
    stroke(d, [(ax - S * 0.13, S * 0.73), (ax, S * 0.86), (ax + S * 0.13, S * 0.73)], w)
    outline = blur(fa, S * 0.012).point(lambda v: 255 if v > 8 else 0)
    layer(c, (24, 8, 54, 255), outline)   # contorno che la stacca dal lettore
    glyph3d(c, None, fa, S, S * 0.03, [(0, (255, 140, 245)), (0.6, (236, 72, 153)), (1, (190, 24, 93))], (150, 20, 90), clip=False)
    return c

def variant_F(S, small):
    """Piastrella 3D: un play che punta in basso fa da freccia di download, sopra un vassoio-barra di streaming."""
    c, face = tile(S)
    cx = S / 2
    pts = [(cx - S * 0.22, S * 0.20), (cx + S * 0.22, S * 0.20), (cx, S * 0.55)]
    g = tri_mask(S, pts, S * 0.035)
    glyph3d(c, face, g, S, S * 0.035, [(0, (255, 255, 255)), (1, (238, 226, 255))], (190, 150, 245))
    progress3d(c, face, S, S * 0.71, S * 0.20, S * 0.80, 0.62, 0.84, S * (0.065 if small else 0.05), small)
    return c

VARIANTS.update({'D': variant_D, 'E': variant_E, 'F': variant_F})

# ---------------------------------------------------------------------------
# Sviluppo della proposta E: lettore video 3D rifinito + tre trattamenti del download
# ---------------------------------------------------------------------------
def player(c, S, small, box, play=True, bar=True):
    x0, y0, x1, y1 = box
    depth = S * 0.035
    r = (y1 - y0) * 0.16
    body = rrect(S, [x0, y0, x1, y1], r)
    layer(c, (20, 0, 40, 255), blur(shift(body, 0, depth + S * 0.03), S * 0.035), 120)
    layer(c, grad(S, [(0, (84, 28, 160)), (1, (124, 22, 132))], (1, 0)), ImageChops.lighter(shift(body, 0, depth), body))
    layer(c, grad(S, [(0, (150, 100, 250)), (0.5, (170, 72, 242)), (1, (222, 70, 222))]), body)
    layer(c, (255, 255, 255, 255), blur(ImageChops.subtract(body, shift(body, 0, S * 0.012)), S * 0.004), 170)
    m = S * (0.05 if small else 0.032)
    scr = rrect(S, [x0 + m, y0 + m, x1 - m, y1 - m], r * 0.62)
    layer(c, grad(S, [(0, (52, 20, 110)), (0.6, (30, 10, 70)), (1, (20, 6, 48))], y_only=True), scr)
    layer(c, (0, 0, 0, 255), ImageChops.subtract(scr, shift(scr, 0, S * 0.014)), 130)
    # bagliore viola dal basso dello schermo, come luce del video
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    cy0 = (y1 - m) / S
    g = np.clip(1 - np.sqrt(((xx - 0.5) / 0.45) ** 2 + ((yy - cy0) / 0.35) ** 2), 0, 1) ** 1.5 * 120
    layer(c, (200, 80, 255, 255), ImageChops.multiply(Image.fromarray(g.astype(np.uint8), 'L'), scr))
    # riflesso del vetro in diagonale
    gl = Image.new('L', (S, S), 0)
    ImageDraw.Draw(gl).polygon([(x0, y0), (x0 + (x1 - x0) * 0.62, y0), (x0 + (x1 - x0) * 0.36, y1), (x0, y1)], fill=255)
    layer(c, (255, 255, 255, 255), ImageChops.multiply(gl, scr), 20)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 - (S * 0.03 if bar and not small else 0)
    if play:
        R = (y1 - y0) * (0.30 if small else 0.22)
        disc = Image.new('L', (S, S), 0)
        ImageDraw.Draw(disc).ellipse([cx - R, cy - R, cx + R, cy + R], fill=255)
        if not small:
            layer(c, (255, 255, 255, 255), disc, 40)
            layer(c, (255, 255, 255, 255), ImageChops.subtract(disc, shift(disc, 0, R * 0.08)), 90)
        t = R * (0.75 if small else 0.55)
        pts = [(cx - t * 0.55, cy - t * 0.8), (cx - t * 0.55, cy + t * 0.8), (cx + t * 0.85, cy)]
        tri = tri_mask(S, pts, S * 0.012)
        glyph3d(c, scr, tri, S, S * 0.012, [(0, (255, 255, 255)), (1, (236, 224, 255))], (180, 140, 245))
    if bar and not small:
        progress3d(c, scr, S, y1 - m - S * 0.05, x0 + m + S * 0.05, x1 - m - S * 0.05, 0.42, 0.68, S * 0.024, small)
    return scr

def variant_E1(S, small):
    """Lettore + freccia piena arrotondata, lucida, che scende dal bordo inferiore."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    player(c, S, small, (S * 0.06, S * 0.12, S * 0.94, S * 0.66))
    ax = S * 0.5
    hw, sw = S * (0.19 if small else 0.16), S * (0.085 if small else 0.065)
    top, neck, tip = S * 0.52, S * 0.73, S * 0.92
    a = Image.new('L', (S, S), 0)
    ImageDraw.Draw(a).polygon([(ax - sw, top), (ax + sw, top), (ax + sw, neck), (ax + hw, neck), (ax, tip),
                               (ax - hw, neck), (ax - sw, neck)], fill=255)
    a = rounded(a, S * 0.022)
    # anello chiaro che stacca la freccia dal lettore (effetto adesivo)
    ring = blur(a, S * 0.016).point(lambda v: 255 if v > 6 else 0)
    layer(c, (40, 0, 80, 255), blur(shift(ring, 0, S * 0.02), S * 0.02), 120)
    layer(c, (255, 255, 255, 255), ring)
    glyph3d(c, None, a, S, S * 0.025, [(0, (250, 120, 245)), (0.55, (226, 70, 200)), (1, (176, 30, 140))],
            (140, 20, 110), clip=False)
    sp = Image.new('L', (S, S), 0)
    ImageDraw.Draw(sp).rounded_rectangle([ax - sw * 0.55, top + S * 0.02, ax - sw * 0.05, neck - S * 0.01], radius=sw * 0.3, fill=255)
    layer(c, (255, 255, 255, 255), ImageChops.multiply(blur(sp, S * 0.006), a), 110)
    return c

def variant_E2(S, small):
    """Lettore + sfera 3D fucsia col download sull'angolo in basso a destra."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    player(c, S, small, (S * 0.05, S * 0.14, S * 0.88, S * 0.70))
    R = S * (0.20 if small else 0.165)
    cx, cy = S * 0.77, S * 0.74
    ring = Image.new('L', (S, S), 0)
    ImageDraw.Draw(ring).ellipse([cx - R * 1.13, cy - R * 1.13, cx + R * 1.13, cy + R * 1.13], fill=255)
    layer(c, (255, 255, 255, 255), ring)
    sphere_badge(c, S, cx, cy, R, small)
    return c

def variant_E3(S, small):
    """Lettore sopra un vassoio 3D: il video 'cade' nel vassoio con una freccia a chevron."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    # vassoio
    tx0, tx1, ty = S * 0.10, S * 0.90, S * 0.80
    tray = rrect(S, [tx0, ty - S * 0.10, tx1, ty + S * 0.07], S * 0.06)
    layer(c, (20, 0, 40, 255), blur(shift(tray, 0, S * 0.04), S * 0.035), 110)
    layer(c, grad(S, [(0, (236, 72, 153)), (1, (157, 23, 120))], (1, 0)), ImageChops.lighter(shift(tray, 0, S * 0.03), tray))
    layer(c, grad(S, [(0, (255, 140, 245)), (1, (217, 70, 239))], y_only=True), tray)
    layer(c, (255, 255, 255, 255), blur(ImageChops.subtract(tray, shift(tray, 0, S * 0.012)), S * 0.004), 170)
    player(c, S, small, (S * 0.13, S * 0.08, S * 0.87, S * 0.60))
    ch = Image.new('L', (S, S), 0)
    stroke(ImageDraw.Draw(ch), [(S * 0.42, ty - S * 0.035), (S * 0.5, ty + S * 0.02), (S * 0.58, ty - S * 0.035)], S * (0.05 if small else 0.035))
    layer(c, (120, 10, 90, 255), shift(ch, 0, S * 0.008), 120)
    layer(c, (255, 255, 255, 255), ch)
    return c

VARIANTS.update({'E1': variant_E1, 'E2': variant_E2, 'E3': variant_E3})

# ---------------------------------------------------------------------------
# Proposta E originale con freccia integrata meglio
# ---------------------------------------------------------------------------
def player_E(c, S, small):
    """Il lettore della proposta E originale (invariato), senza freccia."""
    x0, y0, x1, y1 = S * 0.07, S * 0.14, S * 0.93, S * 0.70
    depth = S * 0.04
    body = rrect(S, [x0, y0, x1, y1], S * 0.09)
    layer(c, (20, 0, 40, 255), blur(shift(body, 0, depth + S * 0.03), S * 0.035), 120)
    layer(c, grad(S, [(0, (90, 30, 170)), (1, (130, 25, 140))], (1, 0)), ImageChops.lighter(shift(body, 0, depth), body))
    layer(c, grad(S, [(0, (139, 92, 246)), (0.55, (168, 70, 240)), (1, (226, 72, 220))]), body)
    layer(c, (255, 255, 255, 255), blur(ImageChops.subtract(body, shift(body, 0, S * 0.012)), S * 0.004), 150)
    m = S * (0.06 if small else 0.045)
    scr = rrect(S, [x0 + m, y0 + m, x1 - m, y1 - m], S * 0.055)
    layer(c, grad(S, [(0, (46, 16, 101)), (1, (24, 8, 54))], y_only=True), scr)
    layer(c, (0, 0, 0, 255), ImageChops.subtract(scr, shift(scr, 0, S * 0.015)), 120)
    gl = Image.new('L', (S, S), 0)
    ImageDraw.Draw(gl).polygon([(x0 + m, y0 + m), (S * 0.62, y0 + m), (S * 0.38, y1 - m), (x0 + m, y1 - m)], fill=255)
    layer(c, (255, 255, 255, 255), ImageChops.multiply(gl, scr), 22)
    cx, cy = S * 0.30 if not small else S * 0.5, S * 0.38
    k = 1.25 if small else 1.0
    pts = [(cx - S * 0.075 * k, cy - S * 0.10 * k), (cx - S * 0.075 * k, cy + S * 0.10 * k), (cx + S * 0.11 * k, cy)]
    if small: pts = [(x - S * 0.1, y) for x, y in pts]
    glyph3d(c, scr, tri_mask(S, pts, S * 0.02), S, S * 0.02, [(0, (255, 255, 255)), (1, (233, 213, 255))], (170, 120, 240))
    if not small:
        progress3d(c, scr, S, y1 - m - S * 0.055, x0 + m + S * 0.05, S * 0.56, 0.6, 0.85, S * 0.028, small)
    return body

def arrow_mask(S, ax, top, tip, w, head):
    a = Image.new('L', (S, S), 0); d = ImageDraw.Draw(a)
    stroke(d, [(ax, top), (ax, tip - w * 0.2)], w)
    stroke(d, [(ax - head, tip - head), (ax, tip), (ax + head, tip - head)], w)
    return a

def knockout(c, mask):
    a = c.getchannel('A')
    c.putalpha(ImageChops.multiply(a, ImageChops.invert(mask)))

def arrow3d(c, S, a, gap, top_stops, side_col):
    knockout(c, blur(a, gap).point(lambda v: 255 if v > 4 else 0).filter(ImageFilter.GaussianBlur(0.8)))
    if not CRISP:
        layer(c, (30, 0, 60, 255), blur(shift(a, 0, S * 0.045), S * 0.03), 120)
    glyph3d(c, None, a, S, S * 0.03, top_stops, side_col, clip=False)

def variant_F1(S, small):
    """Freccia viola-fucsia in tinta col lettore, a destra, che esce dallo schermo; stacco a intaglio."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    player_E(c, S, small)
    w = S * (0.12 if small else 0.085)
    ax = S * (0.5 if small else 0.70)
    a = arrow_mask(S, ax, S * (0.40 if small else 0.33), S * 0.86, w, S * (0.15 if small else 0.13))
    arrow3d(c, S, a, S * 0.022, [(0, (250, 160, 255)), (0.5, (217, 70, 239)), (1, (168, 40, 200))], (120, 30, 160))
    return c

def variant_F2(S, small):
    """Come F1 ma freccia bianca (come il play), al centro, dal basso dello schermo."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    player_E(c, S, small)
    w = S * (0.12 if small else 0.09)
    a = arrow_mask(S, S * 0.5, S * 0.52, S * 0.88, w, S * (0.15 if small else 0.13))
    arrow3d(c, S, a, S * 0.024, [(0, (255, 255, 255)), (1, (236, 222, 255))], (170, 110, 235))
    return c

def variant_F3(S, small):
    """Freccia fucsia-rosa al centro (colori della E originale), piu' snella, con stacco a intaglio."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    player_E(c, S, small)
    w = S * (0.12 if small else 0.085)
    a = arrow_mask(S, S * 0.5, S * 0.52, S * 0.88, w, S * (0.15 if small else 0.13))
    arrow3d(c, S, a, S * 0.024, [(0, (255, 140, 245)), (0.6, (236, 72, 153)), (1, (190, 24, 93))], (150, 20, 90))
    return c

VARIANTS.update({'F1': variant_F1, 'F2': variant_F2, 'F3': variant_F3})

# ---------------------------------------------------------------------------
# F1 rifinita: forme diverse della freccia in tinta
# ---------------------------------------------------------------------------
ARROW_TOP = [(0, (252, 170, 255)), (0.45, (222, 80, 240)), (1, (150, 50, 225))]
ARROW_SIDE = (110, 28, 150)

def solid_arrow(S, ax, top, tip, sw, hw, hh, r):
    """Freccia piena: gambo + punta triangolare, spigoli arrotondati."""
    neck = tip - hh
    a = Image.new('L', (S, S), 0)
    ImageDraw.Draw(a).polygon([(ax - sw / 2, top), (ax + sw / 2, top), (ax + sw / 2, neck), (ax + hw, neck),
                               (ax, tip), (ax - hw, neck), (ax - sw / 2, neck)], fill=255)
    a = rounded(a, r)
    cap = Image.new('L', (S, S), 0)   # sommita' del gambo tonda
    ImageDraw.Draw(cap).ellipse([ax - sw / 2, top - sw / 2, ax + sw / 2, top + sw / 2], fill=255)
    return ImageChops.lighter(a, cap)

def gloss_arrow(c, S, a, ax, top, neck, sw):
    """Riflesso verticale sul lato sinistro del gambo."""
    g = Image.new('L', (S, S), 0)
    ImageDraw.Draw(g).rounded_rectangle([ax - sw * 0.36, top, ax - sw * 0.08, neck - sw * 0.1], radius=sw * 0.14, fill=255)
    layer(c, (255, 255, 255, 255), ImageChops.multiply(blur(g, S * 0.006), a), 120)

def F1_base(S, small, ax):
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    player_E(c, S, small)
    return c

def variant_G1(S, small):
    """Freccia piena e corposa, punta triangolare, a destra."""
    ax = S * (0.62 if small else 0.71)
    c = F1_base(S, small, ax)
    sw, hw, hh = S * (0.17 if small else 0.125), S * (0.23 if small else 0.195), S * (0.21 if small else 0.175)
    top, tip = S * (0.42 if small else 0.40), S * 0.90
    a = solid_arrow(S, ax, top, tip, sw, hw, hh, S * 0.011)
    arrow3d(c, S, a, S * 0.02, ARROW_TOP, ARROW_SIDE)
    if not small: gloss_arrow(c, S, a, ax, top, tip - hh, sw)
    return c

def variant_G2(S, small):
    """Freccia a tratto arrotondato, piu' spessa e corta."""
    ax = S * (0.62 if small else 0.71)
    c = F1_base(S, small, ax)
    w = S * (0.15 if small else 0.11)
    a = arrow_mask(S, ax, S * (0.46 if small else 0.45), S * 0.87, w, S * (0.17 if small else 0.14))
    arrow3d(c, S, a, S * 0.02, ARROW_TOP, ARROW_SIDE)
    return c

def variant_G3(S, small):
    """Freccia piena con scie di movimento sullo schermo (idea di flusso/streaming)."""
    ax = S * (0.62 if small else 0.71)
    c = F1_base(S, small, ax)
    sw, hw, hh = S * (0.17 if small else 0.125), S * (0.23 if small else 0.195), S * (0.21 if small else 0.175)
    top, tip = S * (0.44 if small else 0.45), S * 0.90
    if not small:
        st = Image.new('L', (S, S), 0); d = ImageDraw.Draw(st)
        for dx, y0, y1, al in ((-0.085, 0.22, 0.36, 255), (0.0, 0.20, 0.38, 255), (0.085, 0.24, 0.35, 255)):
            stroke(d, [(ax + S * dx, S * y0), (ax + S * dx, S * y1)], S * 0.022)
        layer(c, (240, 150, 255, 255), st, 170)
    a = solid_arrow(S, ax, top, tip, sw, hw, hh, S * 0.011)
    arrow3d(c, S, a, S * 0.02, ARROW_TOP, ARROW_SIDE)
    if not small: gloss_arrow(c, S, a, ax, top, tip - hh, sw)
    return c

VARIANTS.update({'G1': variant_G1, 'G2': variant_G2, 'G3': variant_G3})

# ---------------------------------------------------------------------------
# Icona definitiva: lettore + freccia a tratto spessa, sfumata fucsia -> rosso
# ---------------------------------------------------------------------------
ARROW_RED_TOP = [(0, (255, 150, 230)), (0.4, (236, 64, 180)), (0.8, (225, 35, 95)), (1, (200, 25, 70))]
ARROW_RED_SIDE = (130, 12, 55)

def variant_H(S, small):
    ax = S * (0.62 if small else 0.71)
    c = F1_base(S, small, ax)
    w = S * (0.16 if small else 0.11)
    a = arrow_mask(S, ax, S * (0.46 if small else 0.45), S * 0.87, w, S * (0.18 if small else 0.14))
    arrow3d(c, S, a, S * 0.02, ARROW_RED_TOP, ARROW_RED_SIDE)
    if not small:   # filo lucido sul gambo
        g = Image.new('L', (S, S), 0)
        ImageDraw.Draw(g).rounded_rectangle([ax - w * 0.34, S * 0.47, ax - w * 0.08, S * 0.70], radius=w * 0.13, fill=255)
        layer(c, (255, 255, 255, 255), ImageChops.multiply(blur(g, S * 0.006), a), 110)
    return c

VARIANTS['H'] = variant_H

# ---------------------------------------------------------------------------
# Tutta l'icona sul fucsia / rosso
# ---------------------------------------------------------------------------
def player_R(c, S, small, pal=None):
    pal = pal or PAL_R1
    x0, y0, x1, y1 = snap(S * 0.07), snap(S * 0.14), snap(S * 0.93), snap(S * 0.70)
    depth = snap(S * 0.04) if CRISP else S * 0.04
    body = rrect(S, [x0, y0, x1, y1], S * 0.09)
    if not CRISP:
        layer(c, (50, 0, 25, 255), blur(shift(body, 0, depth + S * 0.03), S * 0.035), 120)
    layer(c, grad(S, pal['side'], (1, 0)), ImageChops.lighter(shift(body, 0, depth), body))
    layer(c, grad(S, pal['body']), body)
    layer(c, (255, 255, 255, 255), blur(ImageChops.subtract(body, shift(body, 0, S * 0.012)), S * 0.004), 160)
    m = snap(S * (0.06 if small else 0.045))
    if CRISP: m = max(m, K)   # cornice di almeno un pixel pieno
    scr = rrect(S, [x0 + m, y0 + m, x1 - m, y1 - m], S * 0.055)
    layer(c, grad(S, pal['screen'], y_only=True), scr)
    layer(c, (0, 0, 0, 255), ImageChops.subtract(scr, shift(scr, 0, S * 0.015)), 120)
    gl = Image.new('L', (S, S), 0)
    ImageDraw.Draw(gl).polygon([(x0 + m, y0 + m), (S * 0.62, y0 + m), (S * 0.38, y1 - m), (x0 + m, y1 - m)], fill=255)
    layer(c, (255, 255, 255, 255), ImageChops.multiply(gl, scr), 22)
    cx, cy = (S * 0.30 if not small else S * 0.4), S * 0.38
    k = 1.25 if small else 1.0
    pts = [(cx - S * 0.075 * k, cy - S * 0.10 * k), (cx - S * 0.075 * k, cy + S * 0.10 * k), (cx + S * 0.11 * k, cy)]
    glyph3d(c, scr, tri_mask(S, pts, S * 0.02), S, S * 0.02, [(0, (255, 255, 255)), (1, (255, 220, 240))], (220, 120, 180))
    if not small:
        x_0, x_1, y = x0 + m + S * 0.05, S * 0.56, y1 - m - S * 0.055
        h = S * 0.028
        tr = rrect(S, [x_0, y - h / 2, x_1, y + h / 2], h / 2)
        layer(c, (255, 255, 255, 255), tr, 50)
        px = x_0 + (x_1 - x_0) * 0.6
        layer(c, grad(S, pal['bar'], (1, 0)), rrect(S, [x_0, y - h / 2, px, y + h / 2], h / 2))
        kr = h * 1.25
        knob = Image.new('L', (S, S), 0)
        ImageDraw.Draw(knob).ellipse([px - kr, y - kr, px + kr, y + kr], fill=255)
        layer(c, (255, 255, 255, 255), knob)

def red_arrow(c, S, small, top_stops, side):
    ax = S * (0.62 if small else 0.71)
    w = S * (0.16 if small else 0.11)
    a = arrow_mask(S, ax, S * (0.46 if small else 0.45), S * 0.87, w, S * (0.18 if small else 0.14))
    arrow3d(c, S, a, S * 0.02, top_stops, side)
    if not small:
        g = Image.new('L', (S, S), 0)
        ImageDraw.Draw(g).rounded_rectangle([ax - w * 0.34, S * 0.47, ax - w * 0.08, S * 0.70], radius=w * 0.13, fill=255)
        layer(c, (255, 255, 255, 255), ImageChops.multiply(blur(g, S * 0.006), a), 110)

def variant_R1(S, small):
    """Freccia rossa accesa, piu' viva della cornice."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0)); player_R(c, S, small)
    red_arrow(c, S, small, [(0, (255, 160, 200)), (0.45, (250, 60, 110)), (1, (205, 20, 50))], (120, 8, 30))
    return c

def variant_R2(S, small):
    """Freccia chiara rosa-bianca, a contrasto sulla cornice fucsia-rossa."""
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0)); player_R(c, S, small)
    red_arrow(c, S, small, [(0, (255, 255, 255)), (0.6, (255, 215, 235)), (1, (255, 170, 205))], (210, 70, 130))
    return c

VARIANTS.update({'R1': variant_R1, 'R2': variant_R2})


PAL_R1 = dict(side=[(0, (140, 20, 120)), (1, (140, 14, 45))],
              body=[(0, (232, 90, 245)), (0.5, (225, 50, 170)), (1, (230, 35, 80))],
              screen=[(0, (78, 14, 72)), (1, (36, 6, 30))],
              bar=[(0, (255, 170, 230)), (1, (255, 90, 140))],
              arrow=[(0, (255, 160, 200)), (0.45, (250, 60, 110)), (1, (205, 20, 50))], arrow_side=(120, 8, 30))
# lampone: un filo piu' freddo e rosato
PAL_S1 = dict(side=[(0, (130, 20, 120)), (1, (135, 15, 75))],
              body=[(0, (240, 100, 230)), (0.5, (230, 55, 160)), (1, (225, 40, 110))],
              screen=[(0, (70, 14, 70)), (1, (32, 6, 34))],
              bar=[(0, (255, 180, 235)), (1, (255, 100, 170))],
              arrow=[(0, (255, 170, 215)), (0.45, (245, 60, 140)), (1, (200, 25, 85))], arrow_side=(115, 10, 55))
# corallo: un filo piu' caldo, verso il rosso fragola
PAL_S2 = dict(side=[(0, (150, 25, 100)), (1, (150, 25, 35))],
              body=[(0, (235, 85, 220)), (0.5, (238, 55, 135)), (1, (240, 60, 70))],
              screen=[(0, (80, 16, 56)), (1, (40, 8, 22))],
              bar=[(0, (255, 185, 210)), (1, (255, 110, 120))],
              arrow=[(0, (255, 175, 180)), (0.45, (250, 70, 90)), (1, (210, 30, 40))], arrow_side=(125, 12, 20))
# rubino: piu' profondo e saturo
PAL_S3 = dict(side=[(0, (110, 12, 100)), (1, (110, 8, 35))],
              body=[(0, (210, 60, 230)), (0.5, (200, 30, 150)), (1, (205, 20, 65))],
              screen=[(0, (60, 8, 56)), (1, (26, 3, 22))],
              bar=[(0, (255, 150, 220)), (1, (240, 60, 120))],
              arrow=[(0, (255, 140, 190)), (0.45, (235, 40, 95)), (1, (180, 12, 40))], arrow_side=(100, 4, 25))

def _rv(pal):
    def f(S, small):
        c = Image.new('RGBA', (S, S), (0, 0, 0, 0)); player_R(c, S, small, pal)
        red_arrow(c, S, small, pal['arrow'], pal['arrow_side'])
        return c
    return f
VARIANTS.update({'S0': _rv(PAL_R1), 'S1': _rv(PAL_S1), 'S2': _rv(PAL_S2), 'S3': _rv(PAL_S3)})


def _rev(stops):
    return [(1 - p, c) for p, c in reversed(stops)]

# A: gradienti rovesciati (rosso in alto a sinistra, magenta in basso a destra; freccia magenta -> rosso al contrario)
PAL_S3A = dict(side=_rev(PAL_S3['side']), body=_rev(PAL_S3['body']), screen=PAL_S3['screen'],
               bar=_rev(PAL_S3['bar']), arrow=[(0, (240, 150, 255)), (0.45, (205, 45, 200)), (1, (140, 15, 140))],
               arrow_side=(85, 5, 80))
# B: cornice e freccia scambiate (cornice rosso rubino, freccia magenta)
PAL_S3B = dict(side=[(0, (120, 8, 40)), (1, (110, 8, 30))],
               body=[(0, (250, 100, 130)), (0.5, (225, 35, 80)), (1, (185, 15, 50))],
               screen=[(0, (62, 8, 26)), (1, (28, 3, 12))],
               bar=[(0, (255, 160, 190)), (1, (255, 80, 120))],
               arrow=[(0, (245, 150, 255)), (0.45, (210, 60, 230)), (1, (150, 20, 170))], arrow_side=(90, 5, 100))
VARIANTS.update({'S3A': _rv(PAL_S3A), 'S3B': _rv(PAL_S3B)})


def _ruby_with(arrow, side):
    p = dict(PAL_S3); p['arrow'] = arrow; p['arrow_side'] = side
    return _rv(p)
VARIANTS.update({
    'T1': _ruby_with([(0, (255, 190, 150)), (0.45, (255, 110, 70)), (1, (225, 50, 50))], (130, 25, 20)),      # corallo-arancio
    'T2': _ruby_with([(0, (255, 225, 160)), (0.5, (255, 170, 60)), (1, (235, 110, 30))], (140, 60, 10)),      # ambra / oro
    'T3': _ruby_with([(0, (255, 255, 255)), (0.6, (255, 215, 235)), (1, (255, 165, 205))], (200, 60, 120)),   # bianco-rosa
})
