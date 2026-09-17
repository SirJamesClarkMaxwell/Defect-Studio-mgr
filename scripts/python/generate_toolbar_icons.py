"""Generate the selection-mode and add-object toolbar alpha-mask icons."""

from pathlib import Path

from PIL import Image, ImageDraw


SIZE = 512
SCALE = 4
CANVAS = SIZE * SCALE


def scaled(value: int) -> int:
    return value * SCALE


def point(x: int, y: int) -> tuple[int, int]:
    return scaled(x), scaled(y)


def new_mask() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    mask = Image.new("L", (CANVAS, CANVAS), 0)
    return mask, ImageDraw.Draw(mask)


def save(mask: Image.Image, output_dir: Path, name: str) -> None:
    alpha = mask.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    icon = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    icon.putalpha(alpha)
    icon.save(output_dir / name, optimize=True)


def circle(draw: ImageDraw.ImageDraw, x: int, y: int, radius: int) -> None:
    draw.ellipse(
        (scaled(x - radius), scaled(y - radius), scaled(x + radius), scaled(y + radius)),
        fill=255,
    )


def thick_line(draw: ImageDraw.ImageDraw, points: list[tuple[int, int]], width: int = 42) -> None:
    draw.line([point(x, y) for x, y in points], fill=255, width=scaled(width), joint="curve")


def label_tag(draw: ImageDraw.ImageDraw, left: int, top: int, right: int, bottom: int) -> None:
    draw.rounded_rectangle(
        (scaled(left), scaled(top), scaled(right), scaled(bottom)),
        radius=scaled(28),
        fill=255,
    )
    draw.polygon(
        [point(left + 36, bottom - 2), point(left + 76, bottom - 2), point(left + 40, bottom + 48)],
        fill=255,
    )
    draw.rounded_rectangle(
        (scaled(left + 42), scaled(top + 42), scaled(right - 42), scaled(top + 70)),
        radius=scaled(12),
        fill=0,
    )


def mode_atoms(output_dir: Path) -> None:
    mask, draw = new_mask()
    circle(draw, 256, 118, 68)
    circle(draw, 154, 344, 68)
    circle(draw, 358, 344, 68)
    save(mask, output_dir, "tool-mode-atoms.png")


def mode_atoms_bonds(output_dir: Path) -> None:
    mask, draw = new_mask()
    thick_line(draw, [(122, 356), (256, 138), (390, 356)], 46)
    circle(draw, 256, 128, 61)
    circle(draw, 112, 374, 61)
    circle(draw, 400, 374, 61)
    save(mask, output_dir, "tool-mode-atoms-bonds.png")


def mode_bonds_labels(output_dir: Path) -> None:
    mask, draw = new_mask()
    thick_line(draw, [(80, 386), (286, 116)], 54)
    thick_line(draw, [(166, 422), (372, 152)], 54)
    label_tag(draw, 272, 244, 466, 402)
    save(mask, output_dir, "tool-mode-bonds-labels.png")


def mode_all(output_dir: Path) -> None:
    mask, draw = new_mask()
    thick_line(draw, [(112, 372), (250, 132), (386, 372)], 38)
    circle(draw, 250, 126, 53)
    circle(draw, 106, 382, 53)
    circle(draw, 396, 382, 53)
    label_tag(draw, 280, 204, 466, 348)
    save(mask, output_dir, "tool-mode-all.png")


def mode_labels(output_dir: Path) -> None:
    mask, draw = new_mask()
    label_tag(draw, 76, 86, 436, 270)
    label_tag(draw, 142, 276, 436, 424)
    save(mask, output_dir, "tool-mode-labels.png")


def add_arrow(output_dir: Path) -> None:
    mask, draw = new_mask()
    thick_line(draw, [(74, 386), (390, 126)], 48)
    draw.polygon([point(282, 94), point(446, 80), point(412, 242)], fill=255)
    circle(draw, 80, 382, 42)
    save(mask, output_dir, "tool-add-arrow.png")


def add_plane(output_dir: Path) -> None:
    mask, draw = new_mask()
    polygon = [point(72, 356), point(196, 116), point(442, 168), point(318, 408)]
    draw.line(polygon + [polygon[0]], fill=255, width=scaled(42), joint="curve")
    thick_line(draw, [(136, 332), (376, 186)], 28)
    circle(draw, 72, 356, 35)
    circle(draw, 196, 116, 35)
    circle(draw, 442, 168, 35)
    save(mask, output_dir, "tool-add-plane.png")


def add_atom(output_dir: Path) -> None:
    mask, draw = new_mask()
    circle(draw, 248, 266, 166)
    circle(draw, 188, 202, 42)
    draw.ellipse((scaled(154), scaled(168), scaled(222), scaled(236)), fill=0)
    thick_line(draw, [(356, 76), (356, 190)], 34)
    thick_line(draw, [(300, 132), (412, 132)], 34)
    save(mask, output_dir, "tool-add-atom.png")


def add_orbital(output_dir: Path) -> None:
    mask, draw = new_mask()
    draw.ellipse((scaled(58), scaled(174), scaled(270), scaled(338)), fill=255)
    draw.ellipse((scaled(242), scaled(174), scaled(454), scaled(338)), fill=255)
    circle(draw, 256, 256, 42)
    thick_line(draw, [(256, 72), (256, 132)], 30)
    thick_line(draw, [(226, 102), (286, 102)], 30)
    save(mask, output_dir, "tool-add-orbital.png")


def main() -> None:
    output_dir = Path(__file__).resolve().parents[2] / "install" / "app" / "assets" / "icons"
    output_dir.mkdir(parents=True, exist_ok=True)
    for renderer in (
        mode_atoms,
        mode_atoms_bonds,
        mode_bonds_labels,
        mode_all,
        mode_labels,
        add_arrow,
        add_plane,
        add_atom,
        add_orbital,
    ):
        renderer(output_dir)


if __name__ == "__main__":
    main()
