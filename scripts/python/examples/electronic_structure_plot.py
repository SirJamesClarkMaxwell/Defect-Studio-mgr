from __future__ import annotations

import json
import sys

import matplotlib
import numpy as np

matplotlib.use("Agg")  # headless - this always runs as a subprocess, never a GUI session
import matplotlib.pyplot as plt


# Mirrors OccupationDiagramPanel's own drawing (src/Presentation/Panels/OccupationDiagramPanel.cpp):
# one tick per band per spin channel, an arrow through occupied levels, energy (+ irrep, if present)
# printed beside each tick. Not pixel-identical (that's a raw ImDrawList; this is matplotlib), just
# the same visual language - same up/down colors, same "arrow through occupied levels" idea. The
# VBM/CBM shading below reuses the panel's own reference-line colors (OccupationDiagramPanel.cpp's
# drawReferenceLine calls) rather than inventing new ones.
_UP_COLOR = "#5090ff"
_DOWN_COLOR = "#ff7548"
_DARK_BG = "#181818"
_DARK_TICK_COLOR = "#c8c8c8"
_LIGHT_BG = "#ffffff"
_LIGHT_TICK_COLOR = "#202020"
_VBM_COLOR = (120 / 255, 200 / 255, 255 / 255)
_CBM_COLOR = (255 / 255, 160 / 255, 120 / 255)


def _draw_band_edge_shading(ax, xlim, edge: float, far: float, color, label: str, tick_color: str):
    # Solid at the band edge (right where it meets the gap), fading to transparent deep into the
    # band - same "meets the gap" emphasis as the panel's VBM/CBM reference line, just filled
    # instead of a single stroke. `far` is the outer plot boundary (yMin for VBM, yMax for CBM),
    # `edge` is homo/lumo itself. imshow's simplest gradient trick: an (N, 1, 4) RGBA column,
    # stretched across the full plot width via extent + aspect="auto".
    steps = 128
    alpha = np.linspace(1.0, 0.0, steps)
    gradient = np.zeros((steps, 1, 4))
    gradient[:, 0, 0], gradient[:, 0, 1], gradient[:, 0, 2] = color
    gradient[:, 0, 3] = alpha
    y0, y1 = sorted((edge, far))
    # origin="lower" puts row 0 at y0 (bottom) - flip the column so alpha=1 always lands on `edge`.
    if edge > far:
        gradient = gradient[::-1]
    ax.imshow(gradient, extent=(xlim[0], xlim[1], y0, y1), aspect="auto", origin="lower", zorder=0)
    ax.text(xlim[1] - 0.05, edge, label, ha="right", va="bottom" if far > edge else "top",
            fontsize=7, color=tick_color, zorder=1)


def _draw_level(ax, x: float, energy: float, tick_color: str, half_width: float = 0.18):
    ax.plot([x - half_width, x + half_width], [energy, energy], color=tick_color, linewidth=1.5, zorder=2)


def _draw_arrow(ax, x: float, energy: float, points_up: bool, color: str, half_height: float = 0.12):
    y0 = energy - half_height if points_up else energy + half_height
    y1 = energy + half_height if points_up else energy - half_height
    ax.annotate(
        "", xy=(x, y1), xytext=(x, y0),
        arrowprops=dict(arrowstyle="-|>", color=color, linewidth=2.0, mutation_scale=14), zorder=3)


def _label(irrep: str | None) -> str:
    return f" {irrep}" if irrep else ""


def _draw_level_labels(ax, x: float, energy: float, band: int, irrep: str | None, tick_color: str,
                        offset: float = 0.22):
    # Same split as OccupationDiagramPanel::drawLevelLabels: band number to the left of the tick,
    # energy (+ irrep) to the right - never combined into one string on one side.
    ax.text(x - offset, energy, f"#{band}", ha="right", va="center", fontsize=7, color=tick_color)
    ax.text(x + offset, energy, f"{energy:.3f} eV{_label(irrep)}", ha="left", va="center", fontsize=7,
            color=tick_color)


def plot_occupation_diagram(
    bands: list[dict], split_spin_channels: bool, y_label: str, output_path: str, gap: dict | None = None,
    light_background: bool = False,
) -> None:
    bg_color = _LIGHT_BG if light_background else _DARK_BG
    tick_color = _LIGHT_TICK_COLOR if light_background else _DARK_TICK_COLOR

    # Symmetric around each tick (nr-label left, energy-label right - see _draw_level_labels) so the
    # level sits centered in the frame. This used to matter less: before the VBM/CBM shading below,
    # bbox_inches="tight" cropped to whatever content actually rendered, so asymmetric label margins
    # were invisible. The shading spans the *full* xlim (it's meant to look like a band, not a
    # sliver), so it now anchors the crop - any left/right xlim asymmetry reads as an off-center
    # level even though the tick itself never moved.
    figsize = (6.4, 8.0) if split_spin_channels else (4.2, 8.0)
    fig, ax = plt.subplots(figsize=figsize, dpi=150)

    if split_spin_channels:
        for band in bands:
            up, down = band["up"], band["down"]
            _draw_level(ax, 0.0, up["energy"], tick_color)
            _draw_level_labels(ax, 0.0, up["energy"], band["band"], up["irrep"], tick_color)
            if up["occupation"] > 0.5:
                _draw_arrow(ax, 0.0, up["energy"], True, _UP_COLOR)

            _draw_level(ax, 1.0, down["energy"], tick_color)
            _draw_level_labels(ax, 1.0, down["energy"], band["band"], down["irrep"], tick_color)
            if down["occupation"] > 0.5:
                _draw_arrow(ax, 1.0, down["energy"], False, _DOWN_COLOR)
        ax.set_xticks([0.0, 1.0])
        ax.set_xticklabels(["Up", "Down"])
        ax.set_xlim(-0.9, 1.9)
    else:
        for band in bands:
            up, down = band["up"], band["down"]
            _draw_level(ax, 0.0, up["energy"], tick_color)
            _draw_level_labels(ax, 0.0, up["energy"], band["band"], up["irrep"], tick_color)
            if up["occupation"] > 0.5:
                _draw_arrow(ax, -0.06, up["energy"], True, _UP_COLOR)
            if down["occupation"] > 0.5:
                _draw_arrow(ax, 0.06, up["energy"], False, _DOWN_COLOR)
        ax.set_xticks([0.0])
        ax.set_xticklabels(["Level"])
        ax.set_xlim(-0.9, 0.9)

    if gap is not None:
        # Same window OccupationDiagramPanel opens on (gap +/- 10% of its own width, floor 0.05 eV) -
        # see OccupationDiagramPanel.cpp's renderPlot. Without this, ylim was left to whatever the
        # plotted levels alone spanned, which can sit well inside the gap and leave homo/lumo (and
        # the shading below) outside the visible frame entirely.
        homo, lumo = gap["homo"], gap["lumo"]
        margin = max((lumo - homo) * 0.10, 0.05)
        y_min, y_max = homo - margin, lumo + margin
        # Defect levels are expected inside the gap, but a deep/shallow one sitting right at (or
        # past) that margin would otherwise get its tick/arrow/label clipped by the frame instead
        # of just rendering close to the shaded region.
        level_energies = [e for band in bands for e in (band["up"]["energy"], band["down"]["energy"])]
        if level_energies:
            y_min = min(y_min, min(level_energies) - 0.05)
            y_max = max(y_max, max(level_energies) + 0.05)
        ax.set_ylim(y_min, y_max)
        xlim = ax.get_xlim()
        _draw_band_edge_shading(ax, xlim, homo, y_min, _VBM_COLOR, "VBM", tick_color)
        _draw_band_edge_shading(ax, xlim, lumo, y_max, _CBM_COLOR, "CBM", tick_color)

    ax.set_ylabel(y_label)
    ax.set_facecolor(bg_color)
    fig.patch.set_facecolor(bg_color)
    ax.tick_params(colors=tick_color)
    ax.yaxis.label.set_color(tick_color)
    for spine in ax.spines.values():
        spine.set_color(tick_color)
    ax.set_xticks(ax.get_xticks())  # keep xticklabels set above, just recolor
    ax.tick_params(axis="x", colors=tick_color)

    fig.tight_layout()
    # bbox_inches="tight" crops to the actual rendered content (ticks, arrows, AND text labels) -
    # xlim only controls where data maps onto the axes box, not how much of that box is blank; a
    # fixed xlim always leaves unused margin (this was the real cause of the "reserved space for a
    # second channel" look in merged mode - the axes box fills the figure regardless of xlim).
    fig.savefig(output_path, facecolor=fig.get_facecolor(), bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit("usage: electronic_structure_plot.py <output_png_path> <json_payload>")

    output_path = sys.argv[1]
    payload = json.loads(sys.argv[2])
    plot_occupation_diagram(
        payload["bands"], payload["split_spin_channels"], payload["y_label"], output_path, payload.get("gap"),
        payload.get("light_background", False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
