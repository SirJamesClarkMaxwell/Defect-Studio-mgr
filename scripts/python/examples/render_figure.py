"""Render FigureSpec v1 snapshots; no application imports or physics calculations."""

import argparse
from itertools import cycle
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile


THESIS = {
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "font.size": 8,
    "axes.labelsize": 8,
    "axes.titlesize": 8,
    "legend.fontsize": 8,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "axes.linewidth": 0.8,
    "xtick.direction": "in",
    "ytick.direction": "in",
    "text.usetex": False,
    "mathtext.fontset": "dejavusans",
}
KINDS = {"Line", "Stems", "Scatter", "VerticalLine", "Segments"}
FORMATS = {"pdf", "svg", "png"}


def stem(value):
    if (not isinstance(value, str) or not value or value in {".", ".."}
            or value[-1] in ". " or any(ord(ch) < 32 or ch in '<>:"/\\|?*' for ch in value)):
        raise ValueError("Name and style must be file stems without path separators")
    base = value.split(".", 1)[0].upper()
    if base in {"CON", "PRN", "AUX", "NUL"} or re.fullmatch(r"(?:COM|LPT)[1-9]", base):
        raise ValueError("Reserved file name")
    return value


def number(value, nullable=False):
    if value is None and nullable:
        return float("nan")
    if type(value) not in (float, int) or not math.isfinite(value):
        raise ValueError("Expected a finite number (or null for a missing sample/limit)")
    return value


def integer(value):
    if type(value) is not int or not -(2**31) <= value < 2**31:
        raise ValueError("Expected an integer")
    return value


def boolean(value):
    if type(value) is not bool:
        raise ValueError("Expected a boolean")


def text(value):
    if not isinstance(value, str):
        raise ValueError("Expected text")


def formats(values):
    if not isinstance(values, list) or not values or any(value not in FORMATS for value in values):
        raise ValueError("Select formats from pdf, svg, png")
    return list(dict.fromkeys(values))


def validate(spec):
    if integer(spec["schemaVersion"]) != 1:
        raise ValueError("Unsupported FigureSpec schemaVersion (expected 1)")
    stem(spec["name"])
    stem(spec["style"])
    if number(spec["widthCm"]) <= 0 or number(spec["heightCm"]) <= 0 or integer(spec["dpi"]) <= 0:
        raise ValueError("Figure size and DPI must be positive")
    formats(spec["formats"])
    text(spec["createdBy"])
    plot = spec["plot"]
    text(plot["id"])
    text(plot["title"])
    boolean(plot["hasY2"])
    for key in ("x", "y", "y2"):
        axis = plot[key]
        text(axis["labelTex"])
        text(axis["unit"])
        boolean(axis["log"])
        low, high = number(axis["minimum"], True), number(axis["maximum"], True)
        if low < high and axis["log"] and low <= 0:
            raise ValueError("Log axis limits must be positive")
    if not isinstance(plot["series"], list):
        raise ValueError("Series must be an array")
    for series in plot["series"]:
        for key in ("id", "label", "source"):
            text(series[key])
        if series["kind"] not in KINDS:
            raise ValueError("Unsupported series kind")
        for key in ("x", "y"):
            if not isinstance(series[key], list):
                raise ValueError("Samples must be arrays")
            for value in series[key]:
                number(value, True)
        if series["kind"] != "VerticalLine" and len(series["x"]) != len(series["y"]):
            raise ValueError("Series x/y lengths differ")
        if integer(series["yAxis"]) not in (0, 1) or (series["yAxis"] == 1 and not plot["hasY2"]):
            raise ValueError("Invalid series yAxis")
        style = series["style"]
        color = style["color"]
        if color is not None:
            if not isinstance(color, list) or len(color) != 4 or any(not 0 <= number(v) <= 1 for v in color):
                raise ValueError("Color must have four components in [0, 1]")
        boolean(style["visible"])
        boolean(style["filled"])
        if (number(style["weight"]) <= 0 or not 0 <= number(style["fillAlpha"]) <= 1
                or integer(style["lineStyle"]) not in range(4)):
            raise ValueError("Invalid series style")
        if "marker" in series:
            marker = series["marker"]
            if integer(marker["shape"]) not in range(3) or number(marker["size"]) <= 0:
                raise ValueError("Invalid marker")
            boolean(marker["filled"])


def axis_label(axis):
    label = axis["labelTex"]
    if label and "$" not in label:
        label = f"${label}$"
    unit = axis["unit"]
    if unit:
        if any(ch in unit for ch in "\\_^") and "$" not in unit:
            unit = f"${unit}$"
        label += f" [{unit}]"
    return label


def configure_axis(ax, axis, direction):
    getattr(ax, f"set_{direction}label")(axis_label(axis))
    if axis["log"]:
        getattr(ax, f"set_{direction}scale")("log")
    low, high = number(axis["minimum"], True), number(axis["maximum"], True)
    # Adapters use equal limits for an axis fitted to the visible data.
    if low < high:
        getattr(ax, f"set_{direction}lim")(low, high)


def render(spec_path, out_dir=None, requested_formats=None):
    spec_path = Path(spec_path).resolve()
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    validate(spec)
    selected = formats(requested_formats if requested_formats is not None else spec["formats"])

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import to_rgba

    destination = Path(out_dir).resolve() if out_dir is not None else spec_path.parent
    destination.mkdir(parents=True, exist_ok=True)
    staged = []
    fig = None
    try:
        with matplotlib.rc_context(matplotlib.rcParamsDefault):
            matplotlib.rcParams.update(THESIS)
            style_path = spec_path.parent / (spec["style"] + ".mplstyle")
            if style_path.is_file():
                matplotlib.rcParams.update(matplotlib.rc_params_from_file(
                    style_path, fail_on_error=True, use_default_template=False))
            # A project style must not crop the requested physical page or require a TeX installation.
            matplotlib.rcParams.update({"savefig.bbox": None, "text.usetex": False})
            fig, left = plt.subplots(figsize=(spec["widthCm"] / 2.54, spec["heightCm"] / 2.54),
                                     dpi=spec["dpi"], layout="constrained")
            plot = spec["plot"]
            right = left.twinx() if plot["hasY2"] else None
            colors = [cycle(matplotlib.rcParams["axes.prop_cycle"].by_key().get("color", ["black"])) for _ in range(2)]
            handles, labels = [], []
            for series in plot["series"]:
                style = series["style"]
                if not style["visible"]:
                    continue
                ax = right if series["yAxis"] == 1 else left
                x = [number(v, True) for v in series["x"]]
                y = [number(v, True) for v in series["y"]]
                if not x:
                    continue
                color = style["color"]
                if color is None:
                    color = next(colors[series["yAxis"]])
                kwargs = {"color": color, "linewidth": style["weight"],
                          "linestyle": ("-", "--", ":", "-.")[style["lineStyle"]]}
                kind = series["kind"]
                if kind == "VerticalLine":
                    if not math.isfinite(x[0]):
                        continue
                    artist = ax.axvline(x[0], **kwargs)
                elif kind == "Stems":
                    artist = ax.vlines(x, 0, y, **kwargs)
                elif kind == "Scatter":
                    marker = series.get("marker", {"shape": 0, "size": 5, "filled": True})
                    artist = ax.scatter(x, y, s=marker["size"] ** 2,
                                        marker=("o", "D", "s")[marker["shape"]],
                                        facecolors=[color] if marker["filled"] else "none",
                                        edgecolors=[color], linewidths=style["weight"])
                else:
                    artist, = ax.plot(x, y, **kwargs)
                if style["filled"] and kind in {"Line", "Segments"}:
                    fill_color = list(to_rgba(color))
                    fill_color[3] *= style["fillAlpha"]
                    ax.fill_between(x, y, 0, color=fill_color)
                if series["label"] and series["label"] not in labels:
                    handles.append(artist)
                    labels.append(series["label"])
            configure_axis(left, plot["x"], "x")
            configure_axis(left, plot["y"], "y")
            if right is not None:
                configure_axis(right, plot["y2"], "y")
            left.set_title(plot["title"])
            if handles:
                left.legend(handles, labels)
            for output_format in selected:
                output = destination / f"{spec['name']}.{output_format}"
                with tempfile.NamedTemporaryFile(dir=destination, prefix=".figure-", suffix=".tmp", delete=False) as temp:
                    temporary = Path(temp.name)
                staged.append((temporary, output))
                fig.savefig(temporary, format=output_format, dpi=spec["dpi"], bbox_inches=None)
            for temporary, output in staged:
                os.replace(temporary, output)
        return [str(output) for _, output in staged]
    finally:
        if fig is not None:
            plt.close(fig)
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)


def self_test():
    axis = {"labelTex": r"\hbar\omega", "unit": "meV", "minimum": 0, "maximum": 3, "log": False}
    style = {"color": None, "weight": 1, "visible": True, "lineStyle": 0, "filled": False, "fillAlpha": 0.25}
    spec = {"schemaVersion": 1, "name": "self_test", "widthCm": 8.5, "heightCm": 6.5,
            "style": "thesis", "formats": ["pdf", "svg", "png"], "dpi": 100,
            "createdBy": "render_figure self-test", "plot": {
                "id": "S", "title": "S", "x": axis, "y": {**axis, "labelTex": "S_k", "unit": ""},
                "y2": {**axis, "labelTex": r"S(\hbar\omega)", "unit": "meV^{-1}"}, "hasY2": True,
                "series": [
                    {"id": "stems", "label": "modes", "kind": "Stems", "x": [1, 2], "y": [1, 2],
                     "yAxis": 0, "source": "fixture", "style": style},
                    {"id": "line", "label": "spectrum", "kind": "Segments", "x": [0, 1, None, 2, 3],
                     "y": [0, 1, None, 2, 0], "yAxis": 1, "source": "fixture", "style": {**style, "filled": True}},
                ]}}
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "self_test.figure.json"
        path.write_text(json.dumps(spec), encoding="utf-8")
        outputs = render(path)
        assert len(outputs) == 3 and all(Path(p).stat().st_size > 0 for p in outputs)
        pdf = Path(outputs[0]).read_bytes()
        page = re.search(rb"/MediaBox\s*\[\s*0\s+0\s+([\d.]+)\s+([\d.]+)\s*\]", pdf)
        assert page and abs(float(page[1]) - 8.5 / 2.54 * 72) < 1e-6
        assert abs(float(page[2]) - 6.5 / 2.54 * 72) < 1e-6
        before = pdf
        spec["schemaVersion"] = 2
        path.write_text(json.dumps(spec), encoding="utf-8")
        try:
            render(path)
        except ValueError:
            pass
        else:
            raise AssertionError("Version 2 was accepted")
        assert Path(outputs[0]).read_bytes() == before
        assert not list(Path(directory).glob(".figure-*.tmp"))
    return []


class JsonArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError(message)


def main():
    try:
        parser = JsonArgumentParser(description=__doc__)
        parser.add_argument("spec", nargs="?")
        parser.add_argument("--out-dir")
        parser.add_argument("--formats")
        parser.add_argument("--all", dest="all_directory")
        parser.add_argument("--self-test", action="store_true")
        args = parser.parse_args()
        if sum(bool(v) for v in (args.spec, args.all_directory, args.self_test)) != 1:
            raise ValueError("Choose a spec.json, --all DIR, or --self-test")
        requested = formats(args.formats.split(",")) if args.formats is not None else None
        if args.self_test:
            outputs = self_test()
        else:
            if args.all_directory:
                directory = Path(args.all_directory)
                if not directory.is_dir():
                    raise ValueError("Figures directory does not exist")
                paths = sorted(directory.glob("*.figure.json"))
            else:
                paths = [Path(args.spec)]
            outputs = []
            for path in paths:
                outputs.extend(render(path, args.out_dir, requested))
        print(json.dumps({"ok": True, "outputs": outputs}))
        return 0
    except Exception as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
