#pragma once

#include "Renderer/Path/PathStyle.hpp"

namespace DefectStudio
{
	// What one frame of the ramp widget did, so the caller can group a whole drag into one undo
	// entry the way every other path edit does.
	//
	// `changed` is true on every frame the gradient differs from what was passed in - that is the
	// frame-by-frame signal, and on its own it is what made the style sliders cost one Ctrl+Z per
	// frame before S11g. `dragStarted` and `dragEnded` bracket a held marker or a held field, and
	// map onto BeginScenePathStyleDrag / CommitScenePathStyleDrag.
	//
	// A click that adds or removes a stop reports `changed` with both brackets false: it is
	// instantaneous, so it records its own undo entry rather than opening a drag.
	struct GradientRampResult
	{
		bool changed = false;
		bool dragStarted = false;
		bool dragEnded = false;
	};

	// Blender's Color Ramp, which is what a gradient editor is expected to look like: a horizontal
	// bar showing the gradient itself, with the stops as markers sitting on it that are dragged to
	// move, plus add and remove buttons and the selected stop's position, colour and alpha as
	// fields underneath.
	//
	// task/41 S11o: this replaces a vertical stack of one labelled row per stop. That stack was
	// correct and unusable - the thing being edited is a picture, and reading positions as a column
	// of numbers makes the user do the interpolation in their head.
	//
	// `selectedStop` is an index into `gradient.stops`, kept by the caller across frames. The widget
	// keeps it pointing at the same stop when a drag reorders the list - dragging a marker past its
	// neighbour must not silently start editing a different stop - and clamps it into range, so a
	// caller that removed a stop elsewhere cannot hand it a dangling index. It is set to -1 when
	// there is nothing to select.
	//
	// Contract:
	// - `gradient.stops` comes back sorted by position, finite, and within [0,1]. PathGradient
	//   rejects anything else rather than sorting it, so the widget must not be able to author it.
	// - Two stops may share a position: that is a hard colour edge and it is a legitimate thing to
	//   want. They must not be merged or nudged apart.
	// - Removing the last stop leaves `gradient.enabled` false rather than enabled and empty.
	// - Adding a stop puts it in the widest gap, at the colour the gradient already has there, so
	//   the picture does not jump when a stop appears.
	//
	//   ponytail: no interpolation mode. Blender's ramp offers RGB/HSV/HSL and
	//   Linear/Ease/Constant/B-Spline; PathGradient interpolates linearly in RGB and
	//   SampleStrokeColor is the only place that knows it. Adding modes is a change to the style,
	//   the file format and that sampler, none of which this widget touches. Upgrade path: a mode
	//   enum on PathGradient, defaulted to the current behaviour so no existing file moves.
	[[nodiscard]] GradientRampResult DrawGradientRamp(
		const char *id, PathGradient &gradient, int &selectedStop);

	// The colour the gradient shows at `position`, used by the widget to draw the bar and to pick a
	// new stop's colour. Pure, and the reason it is declared here: it is the half of the widget that
	// a test can reach without ImGui.
	//
	// Matches SampleStrokeColor's rule - clamped at both ends, no extrapolation past the first or
	// last stop - so the bar cannot disagree with what the renderer draws. "Matches" means to within
	// a float tolerance, not bit for bit: the two reach the same interpolation by different routes
	// and the last bit differs. A test comparing them with exact equality is testing the order of
	// operations, not the contract. An empty or disabled
	// gradient samples as opaque white, which is what an empty bar shows.
	[[nodiscard]] glm::vec4 SampleGradientAt(const PathGradient &gradient, float position);

	// Inserts a stop in the widest gap between existing stops, at the colour already shown there,
	// and returns its index. An empty gradient gets one stop at 0.5. Pure, and separated from the
	// widget for the same reason.
	std::size_t InsertGradientStopInWidestGap(PathGradient &gradient);
} // namespace DefectStudio
