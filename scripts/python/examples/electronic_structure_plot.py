from __future__ import annotations

import json
import sys

import matplotlib

matplotlib.use("Agg")  # headless - this always runs as a subprocess, never a GUI session
import matplotlib.pyplot as plt


# Mirrors OccupationDiagramPanel's own drawing (src/Presentation/Panels/OccupationDiagramPanel.cpp):
# one tick per band per spin channel, an arrow through occupied levels, energy (+ irrep, if present)
# printed beside each tick. Not pixel-identical (that's a raw ImDrawList; this is matplotlib), just
# the same visual language - same up/down colors, same "arrow through occupied levels" idea.
_UP_COLOR = "#5090ff"
_DOWN_COLOR = "#ff7548"
_TICK_COLOR = "#c8c8c8"


def _draw_level(ax, x: float, energy: float, half_width: float = 0.18):
    ax.plot([x - half_width, x + half_width], [energy, energy], color=_TICK_COLOR, linewidth=1.5, zorder=2)


def _draw_arrow(ax, x: float, energy: float, points_up: bool, color: str, half_height: float = 0.12):
    y0 = energy - half_height if points_up else energy + half_height
    y1 = energy + half_height if points_up else energy - half_height
    ax.annotate(
        "", xy=(x, y1), xytext=(x, y0),
        arrowprops=dict(arrowstyle="-|>", color=color, linewidth=2.0, mutation_scale=14), zorder=3)


def _label(irrep: str | None) -> str:
    return f" {irrep}" if irrep else ""


def plot_occupation_diagram(bands: list[dict], split_spin_channels: bool, y_label: str, output_path: str) -> None:
    fig, ax = plt.subplots(figsize=(6.0, 8.0), dpi=150)

    if split_spin_channels:
        for band in bands:
            up, down = band["up"], band["down"]
            _draw_level(ax, 0.0, up["energy"])
            ax.text(0.22, up["energy"], f"#{band['band']} {up['energy']:.3f} eV{_label(up['irrep'])}",
                    va="center", fontsize=7, color=_TICK_COLOR)
            if up["occupation"] > 0.5:
                _draw_arrow(ax, 0.0, up["energy"], True, _UP_COLOR)

            _draw_level(ax, 1.0, down["energy"])
            ax.text(1.22, down["energy"], f"#{band['band']} {down['energy']:.3f} eV{_label(down['irrep'])}",
                    va="center", fontsize=7, color=_TICK_COLOR)
            if down["occupation"] > 0.5:
                _draw_arrow(ax, 1.0, down["energy"], False, _DOWN_COLOR)
        ax.set_xticks([0.0, 1.0])
        ax.set_xticklabels(["Up", "Down"])
        ax.set_xlim(-0.5, 2.4)
    else:
        for band in bands:
            up, down = band["up"], band["down"]
            _draw_level(ax, 0.0, up["energy"])
            ax.text(0.22, up["energy"], f"#{band['band']} {up['energy']:.3f} eV{_label(up['irrep'])}",
                    va="center", fontsize=7, color=_TICK_COLOR)
            if up["occupation"] > 0.5:
                _draw_arrow(ax, -0.06, up["energy"], True, _UP_COLOR)
            if down["occupation"] > 0.5:
                _draw_arrow(ax, 0.06, up["energy"], False, _DOWN_COLOR)
        ax.set_xticks([0.0])
        ax.set_xticklabels(["Level"])
        ax.set_xlim(-0.5, 2.4)

    ax.set_ylabel(y_label)
    ax.set_facecolor("#181818")
    fig.patch.set_facecolor("#181818")
    ax.tick_params(colors=_TICK_COLOR)
    ax.yaxis.label.set_color(_TICK_COLOR)
    for spine in ax.spines.values():
        spine.set_color(_TICK_COLOR)
    ax.set_xticks(ax.get_xticks())  # keep xticklabels set above, just recolor
    ax.tick_params(axis="x", colors=_TICK_COLOR)

    fig.tight_layout()
    fig.savefig(output_path, facecolor=fig.get_facecolor())
    plt.close(fig)


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit("usage: electronic_structure_plot.py <output_png_path> <json_payload>")

    output_path = sys.argv[1]
    payload = json.loads(sys.argv[2])
    plot_occupation_diagram(
        payload["bands"], payload["split_spin_channels"], payload["y_label"], output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
