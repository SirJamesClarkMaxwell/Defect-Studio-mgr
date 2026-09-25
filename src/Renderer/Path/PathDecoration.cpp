#include "Core/dspch.hpp"

#include "Renderer/Path/PathDecoration.hpp"

#include <cmath>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool IsUsable(const PathEndpointDecoration &decoration, const double width)
		{
			return decoration.kind != PathDecorationKind::None && std::isfinite(width) && width > 0.0 &&
				std::isfinite(decoration.lengthScale) && std::isfinite(decoration.widthScale) &&
				decoration.lengthScale > 0.0f && decoration.widthScale > 0.0f;
		}

		[[nodiscard]] DecorationContour MakeFilled(const double length, const bool closesBack,
			std::initializer_list<DecorationContourPoint> points)
		{
			DecorationContour contour;
			contour.filled = true;
			contour.closesBack = closesBack;
			contour.trim = length;
			contour.points.assign(points.begin(), points.end());
			return contour;
		}
	} // namespace

	DecorationContour BuildDecorationContour(const PathEndpointDecoration &decoration, const double strokeWidth)
	{
		if (!IsUsable(decoration, strokeWidth))
			return {};
		const double length = strokeWidth * static_cast<double>(decoration.lengthScale);
		const double width = strokeWidth * static_cast<double>(decoration.widthScale) * 0.5;
		switch (decoration.kind)
		{
			case PathDecorationKind::Arrow:
				return MakeFilled(length, true, {{0.0, 0.0}, {length, width}, {length, 0.0}});
			case PathDecorationKind::Stealth:
				return MakeFilled(length * 1.15, false, {{0.0, 0.0}, {length * 0.65, width * 1.1}, {length * 1.15, width * 0.35}, {length * 1.15, 0.0}});
			case PathDecorationKind::OpenArrow:
			{
				DecorationContour contour;
				contour.filled = false;
				contour.closesBack = false;
				contour.trim = length * 0.7;
				contour.points = {{0.0, 0.0}, {length, width}, {length, 0.0}};
				return contour;
			}
			case PathDecorationKind::Bar:
				return MakeFilled(length * 0.08, true, {{0.0, width * 1.25}, {length * 0.08, width * 1.25}, {length * 0.08, 0.0}});
			case PathDecorationKind::Circle:
				return MakeFilled(length * 0.75, true, {{0.0, 0.0}, {length * 0.1875, width * 0.53}, {length * 0.375, width * 0.75}, {length * 0.5625, width * 0.53}, {length * 0.75, 0.0}});
			case PathDecorationKind::Square:
				return MakeFilled(length, true, {{0.0, width}, {length, width}, {length, 0.0}});
			case PathDecorationKind::Diamond:
				return MakeFilled(length, true, {{0.0, 0.0}, {length * 0.5, width}, {length, 0.0}});
			case PathDecorationKind::None: break;
		}
		return {};
	}

	ShaftRange TrimmedRange(const double totalLength, const PathStrokeStyle &style)
	{
		if (!std::isfinite(totalLength) || totalLength <= 0.0)
			return {};
		const double start = BuildDecorationContour(style.startDecoration, style.width).trim;
		const double end = totalLength - BuildDecorationContour(style.endDecoration, style.width).trim;
		return end > start ? ShaftRange{start, end} : ShaftRange{};
	}
} // namespace DefectStudio
