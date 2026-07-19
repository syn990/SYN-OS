#!/usr/bin/env python3
# Generates a wallpaper PNG per theme in ~/.config/syn-os/themes/*.theme,
# writing to ~/.wallpaper/<SYN_THEME_NAME>-wallpaper.png. Dev-only tool, not
# shipped or run by the installer — re-run by hand after adding a theme.
# Pattern varies by SYN_THEME_FAMILY: Flatline gets a hairline grid, Slab
# gets banding, Halo gets concentric rings, Bevel gets a diagonal sheen;
# Vanilla is gradient-only. Requires Pillow (pip install pillow).
import colorsys
import math
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFilter

W, H = 1920, 1080
SMALL_W, SMALL_H = 240, 135  # 8x downscale working resolution, upsampled with BICUBIC
THEMES_DIR = os.path.expanduser("~/.config/syn-os/themes")
OUT_DIR = os.path.expanduser("~/.wallpaper")


def hexrgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def scale_lightness(rgb, target_v):
    # Re-lights rgb to a target HSV value (0-1), keeping its hue/sat.
    r, g, b = (c / 255 for c in rgb)
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    r2, g2, b2 = colorsys.hsv_to_rgb(h, s, target_v)
    return (round(r2 * 255), round(g2 * 255), round(b2 * 255))


def parse_theme(path):
    vals = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'^(SYN_[A-Z_]+)="([^"]*)"', line.strip())
            if m:
                vals[m.group(1)] = m.group(2)
    return vals


def small_mask_from_fn(fn):
    # Computes fn(x, y) -> 0..255 at SMALL_W x SMALL_H, then upsamples with
    # bicubic interpolation. A full-res per-pixel Python loop is too slow;
    # this gets a smooth result without one.
    small = Image.new("L", (SMALL_W, SMALL_H))
    px = small.load()
    for y in range(SMALL_H):
        for x in range(SMALL_W):
            px[x, y] = fn(x, y)
    return small.resize((W, H), Image.BICUBIC)


def base_field(dark_base, mid_tone):
    # Radial blend from a dim base (corners) to a lifted mid-tone, off-center
    # toward the upper-left (a "light source" position), so the image reads
    # as a scene rather than a flat swatch.
    cx, cy = SMALL_W * 0.32, SMALL_H * 0.28
    diag = math.hypot(SMALL_W, SMALL_H)

    def fn(x, y):
        d = math.hypot(x - cx, y - cy) / diag
        return int(255 * max(0.0, min(1.0, d * 1.15)))

    mask = small_mask_from_fn(fn)
    base_im = Image.new("RGB", (W, H), dark_base)
    mid_im = Image.new("RGB", (W, H), mid_tone)
    return Image.composite(base_im, mid_im, mask)


def add_radial_light(im, color, cx_frac, cy_frac, radius_frac, strength):
    cx, cy = SMALL_W * cx_frac, SMALL_H * cy_frac
    r = SMALL_W * radius_frac

    def fn(x, y):
        d = math.hypot(x - cx, y - cy) / r
        v = max(0.0, 1.0 - d)
        return int(255 * (v ** 1.6) * strength)

    mask = small_mask_from_fn(fn)
    color_im = Image.new("RGB", (W, H), color)
    return Image.composite(color_im, im, mask)


def vignette(im, strength):
    cx, cy = SMALL_W * 0.5, SMALL_H * 0.5
    diag = math.hypot(cx, cy)

    def fn(x, y):
        d = math.hypot(x - cx, y - cy) / diag
        return int(255 * max(0.0, min(1.0, d)) ** 2 * strength)

    mask = small_mask_from_fn(fn)
    black = Image.new("RGB", (W, H), (0, 0, 0))
    return Image.composite(black, im, mask)


def hairline_grid(im, color, spacing=96, alpha=20):
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    for x in range(0, W, spacing):
        od.line([(x, 0), (x, H)], fill=(*color, alpha), width=1)
    for y in range(0, H, spacing):
        od.line([(0, y), (W, y)], fill=(*color, alpha), width=1)
    return Image.alpha_composite(im.convert("RGBA"), overlay).convert("RGB")


def slab_bands(im, color, count=4, alpha=38, band_h=120):
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    for i in range(count):
        y = int(H * (i + 0.5) / count) - band_h // 2
        od.rectangle([0, y, W, y + 6], fill=(*color, min(255, alpha * 3)))
        od.rectangle([0, y, W, y + band_h], fill=(*color, alpha))
        od.rectangle([0, y + band_h - 6, W, y + band_h], fill=(*color, min(255, alpha * 3)))
    return Image.alpha_composite(im.convert("RGBA"), overlay).convert("RGB")


def halo_rings(im, color, alpha=70):
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    cx, cy = W * 0.32, H * 0.28
    max_r = math.hypot(W, H) * 0.42
    for i in range(1, 6):
        r = max_r * i / 6
        a = int(alpha * (1 - (i - 1) / 6))
        od.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(*color, a), width=2)
    return Image.alpha_composite(im.convert("RGBA"), overlay).convert("RGB")


def bevel_sheen(im, alpha=45):
    overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    band_w = 320
    cx = W * 0.6
    poly = [
        (cx - band_w, 0), (cx + band_w, 0),
        (cx - band_w * 0.2, H), (cx - band_w * 1.8, H),
    ]
    od.polygon(poly, fill=(255, 255, 255, alpha))
    overlay = overlay.filter(ImageFilter.GaussianBlur(110))
    return Image.alpha_composite(im.convert("RGBA"), overlay).convert("RGB")


def build_wallpaper(mode, family, bg_alt, accent, accent_dim, border):
    if mode == "dark":
        dark_base = scale_lightness(bg_alt, 0.05) if sum(bg_alt) > 10 else (5, 5, 6)
        mid_tone = scale_lightness(accent_dim, 0.14)
        light_color = scale_lightness(accent, 0.55)
        glow_strength = 0.85
        vig_strength = 0.55
    else:
        dark_base = scale_lightness(bg_alt, 0.90) if sum(bg_alt) < 750 else (235, 235, 232)
        mid_tone = scale_lightness(accent_dim, 0.82)
        light_color = scale_lightness(accent, 0.35)
        glow_strength = 0.35
        vig_strength = 0.22

    slab_color = scale_lightness(accent, 0.45 if mode == "dark" else 0.55)

    im = base_field(dark_base, mid_tone)

    if family == "SYN-OS-FLATLINE":
        im = hairline_grid(im, border, alpha=24)
    elif family == "SYN-OS-SLAB":
        im = slab_bands(im, slab_color, alpha=30)
    elif family == "SYN-OS-HALO":
        im = halo_rings(im, accent, alpha=75)
    elif family == "SYN-OS-BEVEL":
        im = bevel_sheen(im, alpha=45)

    im = add_radial_light(im, light_color, 0.30, 0.26, 0.55, glow_strength)
    im = vignette(im, vig_strength)

    return im


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    theme_files = sorted(f for f in os.listdir(THEMES_DIR) if f.endswith(".theme"))
    done = 0
    for fname in theme_files:
        path = os.path.join(THEMES_DIR, fname)
        v = parse_theme(path)
        name = v.get("SYN_THEME_NAME", fname[:-6])
        out_path = os.path.join(OUT_DIR, f"{name}-wallpaper.png")
        if "--only-missing" in sys.argv and os.path.exists(out_path):
            continue

        mode = v.get("SYN_THEME_MODE", "dark")
        family = v.get("SYN_THEME_FAMILY", "SYN-OS-VANILLA")
        bg_alt = hexrgb(v["SYN_BG_ALT"])
        accent = hexrgb(v["SYN_ACCENT"])
        accent_dim = hexrgb(v.get("SYN_ACCENT_DIM", v["SYN_ACCENT"]))
        border = hexrgb(v.get("SYN_BORDER", v["SYN_ACCENT"]))

        im = build_wallpaper(mode, family, bg_alt, accent, accent_dim, border)
        im.save(out_path)
        done += 1
        print(f"{name}: {family} {mode} -> {out_path}")

    print(f"\n{done} wallpapers written to {OUT_DIR}")


if __name__ == "__main__":
    main()
