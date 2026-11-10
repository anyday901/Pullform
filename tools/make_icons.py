"""Generates Pullform's original application icon (an extruded box with an
upward push/pull arrow) as a Windows .ico and raw RGBA files that the app
embeds for its window icon. Requires Pillow. Run from the repository root:

    python3 tools/make_icons.py
"""
from PIL import Image, ImageDraw

S = 1024  # supersampled canvas


def draw():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([40, 40, S - 40, S - 40], radius=200, fill=(32, 41, 56, 255))
    # Isometric box: top, left and right faces.
    cx, top, w, h, depth = S // 2, 300, 290, 165, 290
    t = [(cx, top), (cx + w, top + h), (cx, top + 2 * h), (cx - w, top + h)]
    left = [t[3], t[2], (t[2][0], t[2][1] + depth), (t[3][0], t[3][1] + depth)]
    right = [t[2], t[1], (t[1][0], t[1][1] + depth), (t[2][0], t[2][1] + depth)]
    d.polygon(left, fill=(52, 120, 200, 255))
    d.polygon(right, fill=(34, 86, 150, 255))
    d.polygon(t, fill=(120, 190, 250, 255))
    edge = (235, 244, 255, 255)
    for poly in (t, left, right):
        d.line(poly + [poly[0]], fill=edge, width=14, joint="curve")
    # Push/pull arrow rising from the top face.
    ay = top + h
    d.rectangle([cx - 30, ay - 220, cx + 30, ay - 20], fill=(255, 196, 60, 255))
    d.polygon([(cx - 100, ay - 200), (cx + 100, ay - 200), (cx, ay - 330)], fill=(255, 196, 60, 255))
    return img


def main():
    big = draw()
    sizes = [16, 24, 32, 48, 64, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(
        "assets/icons/pullform.ico", sizes=[(s, s) for s in sizes])
    big.resize((256, 256), Image.LANCZOS).save("assets/icons/pullform-256.png")
    for s in (16, 32, 48):
        with open(f"assets/icons/pullform-{s}.rgba", "wb") as f:
            f.write(big.resize((s, s), Image.LANCZOS).tobytes())


if __name__ == "__main__":
    main()
