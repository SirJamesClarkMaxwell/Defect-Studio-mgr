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
	// - `filled` is copied from `decoration.filled`, for every kind. It is not a property of the
	//   shape family. (task/41 S11i: it used to be hardcoded true except for the OpenArrow kind,
	//   and the mesher ignored it even there - a hollow tip came out solid.)
	//
	// Per-kind shape, all of which the S11 manual round found wrong except Arrow and Stealth. These
	// are the shapes, in the local frame where s runs back from the tip and halfWidth is the radial
	// offset; `length` is lengthScale * strokeWidth and `width` is widthScale * strokeWidth:
	// - Arrow:   (0,0) -> (length,width), back straight across. Closes back.
	// - Stealth: Arrow, but the back is notched forward to about a third of `length`, so the two
	//   barbs trail behind the join. Does NOT close back - that is what distinguishes it from Arrow.
	// - Latex:   Arrow's silhouette with the sides bowed outward and the back swept, sampled finely
	//   enough that the curve does not read as a polygon. Closes back.
	// - Bar:     a tee. halfWidth is `width` over a length of about one stroke width, so it is a
	//   stub across the tangent rather than a taper. Closes back.
	// - Circle:  a half-circle of radius `length / 2`, centred at s == length / 2, sampled at no
	//   fewer than 12 points. It must read as a disc, not as the five-point lens S11 shipped.
	// - Square:  CONSTANT halfWidth == width from s == 0 to s == length. It must not taper to a
	//   point; that was the defect. Closes back, and its front face is flat.
	// - Diamond: widest at s == length / 2, back to zero at s == length. Symmetric. Closes back.
	// - Kite:    widest at s == length / 3, back to zero at s == length. Closes back.
	//
	// Every kind's contour is expressed in these two scales alone, so a caller that wants a longer
	// or fatter tip changes the scales, not the kind.
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
