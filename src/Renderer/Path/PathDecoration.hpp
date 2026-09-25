#pragma once

#include <vector>

#include "Renderer/Path/PathStyle.hpp"

namespace DefectStudio
{
	// One point of the axial profile of a decoration, in a local frame whose origin is the path
	// endpoint and whose +s runs back along the shaft (away from the tip). Round revolves this contour
	// about the tangent; Flat mirrors it to +/- halfWidth in the ribbon plane. One description, two
	// profiles - which is the whole reason decorations stopped being a table of triangle counts.
	struct DecorationContourPoint
	{
		double s = 0.0;         // distance back from the endpoint, >= 0, non-decreasing along the contour
		double halfWidth = 0.0; // radial offset from the axis, >= 0
	};

	struct DecorationContour
	{
		std::vector<DecorationContourPoint> points;
		bool filled = false;     // solid body (Arrow, Bar, Circle...) vs. an outline (OpenArrow)
		bool closesBack = false; // the back face is capped, so the shaft may end inside the decoration
		double trim = 0.0;       // how much shaft this decoration swallows; >= 0, <= the contour extent
	};

	// Builds the contour for one decoration at a stroke of `strokeWidth`. Contract:
	// - `None`, a non-finite scale, a non-positive scale or a non-positive width yields an empty
	//   contour with trim == 0 - never a one-point or zero-area shape the mesher has to special-case.
	// - Points are ordered by `s` starting at the tip (s == 0), and every value is finite.
	// - `trim` equals the length of shaft the decoration occupies: for a closesBack decoration that is
	//   the full contour extent, for an open one it is the part the shaft would poke through.
	// - OpenArrow is the only unfilled kind; Circle/Square/Diamond/Bar/Arrow/Stealth are all filled.
	// - Stealth's back is concave (it does not close), which is exactly what distinguishes it from
	//   Arrow at the same scales.
	[[nodiscard]] DecorationContour BuildDecorationContour(
		const PathEndpointDecoration &decoration, double strokeWidth);

	// The arc-length range of the shaft left after both endpoint decorations take their bite.
	struct ShaftRange
	{
		double start = 0.0;
		double end = 0.0;

		[[nodiscard]] bool IsEmpty() const
		{
			return !(end > start);
		}
	};

	// Contract:
	// - start == the start decoration's trim, end == totalLength minus the end decoration's trim.
	// - When the two trims meet or overlap the range is empty rather than inverted; the caller reports
	//   DecorationsExceedPathLength and draws the decorations without a shaft.
	// - A non-finite or non-positive totalLength yields an empty range at 0.
	[[nodiscard]] ShaftRange TrimmedRange(double totalLength, const PathStrokeStyle &style);
}
