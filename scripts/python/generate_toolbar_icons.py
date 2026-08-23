"""Generates the 10 toolbar icon PNGs that had no artwork yet (axis-align a/b/c/a*/b*/c* and the
4 atom/bond/label selection-mode buttons) - solid black glyph on a transparent 512x512 canvas,
same convention as the existing hand-picked icons in install/app/assets/icons (see e.g.
tool-move.png: a bold, simple pictogram with a generous margin, no outline/gradient).

Re-runnable: always overwrites the same 10 files, no state carried between runs.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parents[2]
ICONS_DIR = REPO_ROOT / "install" / "app" / "assets" / "icons"
FONT_PATH = (
    REPO_ROOT
    / "install"
    / "app"
    / "python"
    / "windows"
    / "Lib"
    / "site-packages"
    / "matplotlib"
    / "mpl-data"
    / "fonts"
    / "ttf"
    / "DejaVuSans-Bold.ttf"
)

SIZE = 512
BLACK = (0, 0, 0, 255)


def new_canvas() -> Image.Image:
    return Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))


def save(image: Image.Image, name: str) -> None:
    path = ICONS_DIR / name
    image.save(path)
    print(f"Wrote {path}")


def draw_axis_letter(text: str, filename: str) -> None:
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    font_size = 360 if len(text) == 1 else 300
    font = ImageFont.truetype(str(FONT_PATH), font_size)
    bbox = draw.textbbox((0, 0), text, font=font)
    width, height = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (SIZE - width) / 2 - bbox[0]
    y = (SIZE - height) / 2 - bbox[1]
    draw.text((x, y), text, font=font, fill=BLACK)
    save(image, filename)


def draw_atom(draw: ImageDraw.ImageDraw, center: tuple[float, float], radius: float) -> None:
    x, y = center
    draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=BLACK)


def draw_bond(draw: ImageDraw.ImageDraw, a: tuple[float, float], b: tuple[float, float], width: float) -> None:
    draw.line((a, b), fill=BLACK, width=int(width))


def draw_tag(draw: ImageDraw.ImageDraw, center: tuple[float, float], size: float) -> None:
    x, y = center
    half = size / 2
    draw.rounded_rectangle((x - half, y - half * 0.6, x + half, y + half * 0.6), radius=half * 0.25, fill=BLACK)


def mode_atoms() -> None:
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    radius = 78
    for cx, cy in ((256, 150), (150, 340), (362, 340)):
        draw_atom(draw, (cx, cy), radius)
    save(image, "tool-mode-atoms.png")


def mode_atoms_bonds() -> None:
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    a, b = (150, 360), (362, 152)
    draw_bond(draw, a, b, 46)
    draw_atom(draw, a, 82)
    draw_atom(draw, b, 82)
    save(image, "tool-mode-atoms-bonds.png")


def mode_bonds_labels() -> None:
    # Bond line plus a separate floating tag - kept apart (not merged into one blob) so both read
    # as distinct elements at a glance.
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    a, b = (100, 430), (270, 260)
    draw_bond(draw, a, b, 40)
    draw_tag(draw, (370, 130), 220)
    save(image, "tool-mode-bonds-labels.png")


def mode_all() -> None:
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    a, b = (110, 420), (280, 280)
    draw_bond(draw, a, b, 38)
    draw_atom(draw, a, 62)
    draw_atom(draw, b, 62)
    draw_tag(draw, (390, 120), 200)
    save(image, "tool-mode-all.png")


def main() -> None:
    ICONS_DIR.mkdir(parents=True, exist_ok=True)
    for letter in ("a", "b", "c"):
        draw_axis_letter(letter, f"tool-axis-{letter}.png")
        draw_axis_letter(f"{letter}*", f"tool-axis-{letter}-star.png")
    mode_atoms()
    mode_atoms_bonds()
    mode_bonds_labels()
    mode_all()


if __name__ == "__main__":
    main()
