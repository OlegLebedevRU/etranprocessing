"""Locate near-black video regions in landing screenshots."""
from PIL import Image
import os

base = r"D:\repo\platerra\Public\etranprocessing\l4desk-landing\site\images"
names = [
    "l4desk-landing-hd-desktop.png",
    "l4desk-landing-hd-mobile.png",
    "l4desk-landing-control.png",
]

for name in names:
    im = Image.open(os.path.join(base, name)).convert("RGB")
    w, h = im.size
    print(f"\n=== {name} {w}x{h} ===")
    # vertical scan at center x
    x = w // 2
    runs = []
    start = None
    for y in range(h):
        r, g, b = im.getpixel((x, y))
        dark = r < 28 and g < 28 and b < 28
        if dark and start is None:
            start = y
        elif not dark and start is not None:
            if y - start > 20:
                runs.append((start, y - 1, y - start))
            start = None
    if start is not None and h - start > 20:
        runs.append((start, h - 1, h - start))
    print(f"  vertical dark runs at x={x}: {runs}")

    # horizontal scan at a few y
    for y in (h // 2, h // 3, 2 * h // 3, 40, 250, 1300 if h > 1300 else h - 1):
        if y >= h:
            continue
        runs = []
        start = None
        for x2 in range(w):
            r, g, b = im.getpixel((x2, y))
            dark = r < 28 and g < 28 and b < 28
            if dark and start is None:
                start = x2
            elif not dark and start is not None:
                if x2 - start > 20:
                    runs.append((start, x2 - 1, x2 - start))
                start = None
        if start is not None and w - start > 20:
            runs.append((start, w - 1, w - start))
        print(f"  horizontal dark runs at y={y}: {runs}")
