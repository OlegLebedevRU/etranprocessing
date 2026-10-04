"""Replace blurred black video areas with a synthetic Windows desktop.

Operates on copies in tools/out/; originals are left untouched until we approve.
"""
from __future__ import annotations

import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = r"D:\repo\platerra\Public\etranprocessing\l4desk-landing"
SRC = os.path.join(ROOT, "site", "images")
OUT = os.path.join(ROOT, "tools", "out")
os.makedirs(OUT, exist_ok=True)


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        r"C:\Windows\Fonts\seguisb.ttf" if bold else r"C:\Windows\Fonts\segoeui.ttf",
        r"C:\Windows\Fonts\arialbd.ttf" if bold else r"C:\Windows\Fonts\arial.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def rounded_rect(draw: ImageDraw.ImageDraw, xy, radius: int, fill=None, outline=None, width: int = 1):
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def make_desktop(width: int, height: int) -> Image.Image:
    """Windows 11-style desktop: gradient wallpaper, taskbar, two windows, icons."""
    img = Image.new("RGB", (width, height), (0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Wallpaper: vertical blue-teal gradient with soft diagonal glow
    top = (24, 52, 120)
    bottom = (12, 28, 70)
    for y in range(height):
        t = y / max(height - 1, 1)
        r = int(top[0] + (bottom[0] - top[0]) * t)
        g = int(top[1] + (bottom[1] - top[1]) * t)
        b = int(top[2] + (bottom[2] - top[2]) * t)
        draw.line([(0, y), (width, y)], fill=(r, g, b))

    # Soft light bloom (like Win11 abstract)
    glow = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(glow)
    gdraw.ellipse(
        [width * 0.15, -height * 0.35, width * 0.95, height * 0.55],
        fill=(90, 160, 255, 55),
    )
    gdraw.ellipse(
        [width * -0.1, height * 0.35, width * 0.55, height * 1.1],
        fill=(40, 200, 190, 35),
    )
    glow = glow.filter(ImageFilter.GaussianBlur(radius=int(width * 0.04)))
    img = Image.alpha_composite(img.convert("RGBA"), glow).convert("RGB")
    draw = ImageDraw.Draw(img)

    # Taskbar
    tb_h = max(int(height * 0.048), 28)
    tb_y = height - tb_h
    draw.rectangle([0, tb_y, width, height], fill=(28, 32, 42))

    # Start button (Win11 four-pane logo approximation)
    cx = width // 2
    icon = max(int(tb_h * 0.42), 12)
    ix = cx - int(width * 0.12)
    iy = tb_y + (tb_h - icon) // 2
    gap = max(icon // 8, 2)
    half = (icon - gap) // 2
    for row in (0, 1):
        for col in (0, 1):
            x0 = ix + col * (half + gap)
            y0 = iy + row * (half + gap)
            draw.rectangle([x0, y0, x0 + half, y0 + half], fill=(120, 190, 255))

    # Center taskbar icons (search, explorer, edge-like, notepad-like)
    icon_specs = [
        (-0.04, (255, 255, 255)),  # search circle
        (0.0, (255, 200, 80)),     # folder
        (0.04, (80, 170, 255)),    # browser
        (0.08, (180, 220, 255)),   # notepad
    ]
    for offset, color in icon_specs:
        x0 = int(cx + width * offset) - icon // 2
        y0 = iy
        rounded_rect(draw, [x0, y0, x0 + icon, y0 + icon], radius=max(icon // 5, 2), fill=color)

    # System tray clock
    clock_font = load_font(max(int(tb_h * 0.38), 10))
    clock = "14:52"
    date = "30.09.2026"
    tw = draw.textlength(clock, font=clock_font)
    dw = draw.textlength(date, font=clock_font)
    draw.text((width - tw - width * 0.02, tb_y + tb_h * 0.12), clock, font=clock_font, fill=(230, 235, 245))
    draw.text((width - dw - width * 0.02, tb_y + tb_h * 0.52), date, font=clock_font, fill=(180, 190, 210))

    # Desktop icons (left column)
    icon_font = load_font(max(int(height * 0.016), 9))
    labels = ["Этот компьютер", "Сеть", "Корзина", "Отчёты"]
    for i, label in enumerate(labels):
        x0 = int(width * 0.025)
        y0 = int(height * 0.08) + i * int(height * 0.11)
        s = max(int(height * 0.055), 24)
        rounded_rect(draw, [x0, y0, x0 + s, y0 + s], radius=max(s // 6, 3), fill=(220, 235, 255))
        draw.rectangle([x0 + s // 6, y0 + s // 3, x0 + s - s // 6, y0 + s - s // 5], fill=(80, 150, 230))
        draw.text((x0, y0 + s + 2), label, font=icon_font, fill=(240, 245, 255))

    # Window 1: File Explorer
    win_font = load_font(max(int(height * 0.022), 11))
    small_font = load_font(max(int(height * 0.018), 10))
    wx = int(width * 0.18)
    wy = int(height * 0.12)
    ww = int(width * 0.42)
    wh = int(height * 0.52)
    # shadow
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    shd = ImageDraw.Draw(sh)
    rounded_rect(shd, [wx + 8, wy + 10, wx + ww + 8, wy + wh + 10], radius=10, fill=(0, 0, 0, 90))
    sh = sh.filter(ImageFilter.GaussianBlur(8))
    img = Image.alpha_composite(img.convert("RGBA"), sh).convert("RGB")
    draw = ImageDraw.Draw(img)

    rounded_rect(draw, [wx, wy, wx + ww, wy + wh], radius=10, fill=(248, 249, 252), outline=(180, 190, 210), width=1)
    # title bar
    rounded_rect(draw, [wx, wy, wx + ww, wy + int(wh * 0.09)], radius=10, fill=(255, 255, 255))
    draw.rectangle([wx, wy + int(wh * 0.07), wx + ww, wy + int(wh * 0.09)], fill=(255, 255, 255))
    draw.text((wx + int(ww * 0.04), wy + int(wh * 0.02)), "Проводник", font=win_font, fill=(30, 40, 60))
    for i, c in enumerate([(255, 95, 87), (255, 189, 46), (40, 200, 64)]):
        r = max(int(wh * 0.018), 5)
        cxk = wx + ww - int(ww * 0.04) - i * int(ww * 0.05)
        cyk = wy + int(wh * 0.045)
        draw.ellipse([cxk - r, cyk - r, cxk + r, cyk + r], fill=c)

    # sidebar + file grid
    draw.rectangle([wx + 1, wy + int(wh * 0.09), wx + int(ww * 0.28), wy + wh - 1], fill=(236, 240, 248))
    folders = ["Рабочий стол", "Загрузки", "Документы", "Отчёты", "C:\\"]
    for i, name in enumerate(folders):
        y0 = wy + int(wh * 0.13) + i * int(wh * 0.1)
        draw.rectangle(
            [wx + int(ww * 0.04), y0 + int(wh * 0.025), wx + int(ww * 0.08), y0 + int(wh * 0.06)],
            fill=(255, 200, 80),
        )
        draw.text((wx + int(ww * 0.1), y0), name, font=small_font, fill=(50, 60, 80))

    grid_labels = ["l4tools", "l4desk", "logs", "tools", "config", "backup"]
    for i, name in enumerate(grid_labels):
        col = i % 3
        row = i // 3
        x0 = wx + int(ww * 0.32) + col * int(ww * 0.22)
        y0 = wy + int(wh * 0.14) + row * int(wh * 0.22)
        s = int(ww * 0.12)
        rounded_rect(draw, [x0, y0, x0 + s, y0 + s], radius=6, fill=(255, 210, 100))
        draw.text((x0, y0 + s + 2), name, font=small_font, fill=(40, 50, 70))

    # Window 2: Notepad / terminal-ish
    wx2 = int(width * 0.52)
    wy2 = int(height * 0.22)
    ww2 = int(width * 0.38)
    wh2 = int(height * 0.42)
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    shd = ImageDraw.Draw(sh)
    rounded_rect(shd, [wx2 + 8, wy2 + 10, wx2 + ww2 + 8, wy2 + wh2 + 10], radius=10, fill=(0, 0, 0, 90))
    sh = sh.filter(ImageFilter.GaussianBlur(8))
    img = Image.alpha_composite(img.convert("RGBA"), sh).convert("RGB")
    draw = ImageDraw.Draw(img)

    rounded_rect(draw, [wx2, wy2, wx2 + ww2, wy2 + wh2], radius=10, fill=(32, 36, 48), outline=(90, 100, 120), width=1)
    rounded_rect(draw, [wx2, wy2, wx2 + ww2, wy2 + int(wh2 * 0.1)], radius=10, fill=(45, 50, 65))
    draw.rectangle([wx2, wy2 + int(wh2 * 0.08), wx2 + ww2, wy2 + int(wh2 * 0.1)], fill=(45, 50, 65))
    draw.text((wx2 + int(ww2 * 0.04), wy2 + int(wh2 * 0.022)), "Диагностика — l4tools", font=win_font, fill=(220, 228, 240))

    term_font = load_font(max(int(height * 0.02), 10))
    lines = [
        ("C:\\l4tools> l4superv status", (140, 220, 140)),
        ("service l4con ........................ ONLINE", (180, 230, 180)),
        ("service l4desk ........................ ONLINE", (180, 230, 180)),
        ("last check: exit code 0", (160, 200, 255)),
        ("C:\\l4tools> _", (220, 230, 240)),
    ]
    for i, (line, color) in enumerate(lines):
        draw.text(
            (wx2 + int(ww2 * 0.05), wy2 + int(wh2 * 0.16) + i * int(wh2 * 0.12)),
            line,
            font=term_font,
            fill=color,
        )

    return img


def draw_overlay_badge(img: Image.Image, xy, title: str, subtitle: str | None = None) -> None:
    """Dark pill used on video frames: green dot + title + optional subtitle."""
    draw = ImageDraw.Draw(img)
    x, y = xy
    title_font = load_font(13, bold=True)
    sub_font = load_font(12)
    pad_x, pad_y = 10, 6
    th = 18
    tw = draw.textlength(title, font=title_font)
    sw = draw.textlength(subtitle, font=sub_font) if subtitle else 0
    total_w = pad_x * 2 + 8 + 8 + tw + (8 + sw if subtitle else 0)
    total_h = pad_y * 2 + th
    rounded_rect(draw, [x, y, x + total_w, y + total_h], radius=6, fill=(20, 22, 28))
    # green dot
    dot_r = 5
    draw.ellipse([x + pad_x, y + total_h // 2 - dot_r, x + pad_x + 10, y + total_h // 2 + dot_r], fill=(46, 204, 80))
    draw.text((x + pad_x + 18, y + pad_y + 1), title, font=title_font, fill=(245, 248, 252))
    if subtitle:
        draw.text((x + pad_x + 18 + tw + 8, y + pad_y + 2), subtitle, font=sub_font, fill=(170, 178, 192))


def draw_light_badge(img: Image.Image, xy, title: str) -> None:
    draw = ImageDraw.Draw(img)
    x, y = xy
    font = load_font(13)
    tw = draw.textlength(title, font=font)
    total_w = tw + 28
    total_h = 30
    rounded_rect(draw, [x, y, x + total_w, y + total_h], radius=8, fill=(255, 255, 255))
    draw.text((x + 14, y + 6), title, font=font, fill=(30, 70, 160))


def paste_cover(img: Image.Image, desktop: Image.Image, box: tuple[int, int, int, int]) -> Image.Image:
    """Cover-fit desktop into box and paste."""
    x0, y0, x1, y1 = box
    bw, bh = x1 - x0, y1 - y0
    if bw <= 0 or bh <= 0:
        return img
    dw, dh = desktop.size
    scale = max(bw / dw, bh / dh)
    nw, nh = int(dw * scale), int(dh * scale)
    resized = desktop.resize((nw, nh), Image.Resampling.LANCZOS)
    left = (nw - bw) // 2
    top = (nh - bh) // 2
    cropped = resized.crop((left, top, left + bw, top + bh))
    img.paste(cropped, (x0, y0))
    return img


def process_hd_desktop() -> None:
    name = "l4desk-landing-hd-desktop.png"
    im = Image.open(os.path.join(SRC, name)).convert("RGB")
    w, h = im.size
    desktop = make_desktop(1600, 900)
    # Video content area inside the dark frame (leaves 1px frame edge)
    box = (131, 242, 1253, h)
    paste_cover(im, desktop, box)
    # Overlays on top of video
    draw_overlay_badge(
        im,
        (141, 254),
        "В ЭФИРЕ",
        "• Основной экран (\\\\.\\DISPLAY1) (1920×1080)",
    )
    out = os.path.join(OUT, name)
    im.save(out, "PNG")
    print("wrote", out, im.size)


def process_control() -> None:
    name = "l4desk-landing-control.png"
    im = Image.open(os.path.join(SRC, name)).convert("RGB")
    w, h = im.size
    desktop = make_desktop(1600, 900)
    box = (7, 6, w - 4, h - 4)
    paste_cover(im, desktop, box)
    draw_overlay_badge(
        im,
        (16, 14),
        "В ЭФИРЕ",
        "• Основной экран (\\\\.\\DISPLAY1) (1920×1080)",
    )
    draw_light_badge(im, (930, 14), "Управление мышью активно")
    out = os.path.join(OUT, name)
    im.save(out, "PNG")
    print("wrote", out, im.size)


def process_mobile() -> None:
    name = "l4desk-landing-hd-mobile.png"
    im = Image.open(os.path.join(SRC, name)).convert("RGB")
    w, h = im.size
    desktop = make_desktop(1200, 800)
    # Video frame on mobile: approx x 130-753, y 1247-1620
    box = (131, 1248, 752, 1618)
    paste_cover(im, desktop, box)

    draw = ImageDraw.Draw(im)
    # Top overlay bar (dark) over video — two compact lines like the original
    draw.rectangle((131, 1248, 752, 1312), fill=(18, 20, 26))
    title_font = load_font(17, bold=True)
    sub_font = load_font(15)
    draw.ellipse([150, 1268, 166, 1284], fill=(46, 204, 80))
    draw.text((178, 1258), "В ЭФИРЕ", font=title_font, fill=(245, 248, 252))
    draw.text((178, 1286), "• Основной экран (\\\\.\\DISPLAY1)", font=sub_font, fill=(170, 178, 192))
    draw.text((560, 1258), "(1920×1080)", font=sub_font, fill=(170, 178, 192))
    draw.text((600, 1286), "1920×1080", font=sub_font, fill=(170, 178, 192))

    # Bottom controls
    btn_font = load_font(18)
    # Fit / Original buttons
    rounded_rect(draw, [160, 1548, 250, 1592], radius=8, fill=(255, 255, 255))
    draw.text((178, 1556), "Вписать", font=btn_font, fill=(30, 40, 60))
    rounded_rect(draw, [252, 1548, 470, 1592], radius=8, fill=(235, 238, 245))
    draw.text((270, 1556), "Исходный размер", font=btn_font, fill=(90, 100, 120))
    # fullscreen icon box
    rounded_rect(draw, [672, 1548, 720, 1592], radius=8, fill=(255, 255, 255))
    # simple expand glyph
    for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        x0 = 696 + (4 if dx > 0 else -12)
        y0 = 1570 + (4 if dy > 0 else -12)
        draw.rectangle([x0, y0, x0 + 8, y0 + 8], outline=(30, 40, 60), width=2)

    out = os.path.join(OUT, name)
    im.save(out, "PNG")
    print("wrote", out, im.size)


if __name__ == "__main__":
    process_hd_desktop()
    process_control()
    process_mobile()
    print("done")
