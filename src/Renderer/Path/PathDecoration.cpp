#include "Core/dspch.hpp"

#include "Renderer/Path/PathDecoration.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

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

		[[nodiscard]] DecorationContour ValidateContour(DecorationContour contour)
		{
			bool valid = std::isfinite(contour.trim) && contour.trim >= 0.0;
			for (std::size_t index = 0; valid && index < contour.points.size(); ++index)
			{
				const DecorationContourPoint &point = contour.points[index];
				valid = std::isfinite(point.s) && std::isfinite(point.halfWidth) && point.s >= 0.0 &&
					point.halfWidth >= 0.0 && (index == 0u || contour.points[index - 1u].s <= point.s);
			}
			if (valid)
				return contour;
			DecorationContour empty;
			empty.filled = contour.filled;
			return empty;
		}

		[[nodiscard]] DecorationContour MakeContour(const double length, const bool closesBack,
			const bool filled,
			std::initializer_list<DecorationContourPoint> points)
		{
			DecorationContour contour;
			contour.filled = filled;
			contour.closesBack = closesBack && filled;
			contour.trim = length;
			contour.points.assign(points.begin(), points.end());
			return ValidateContour(std::move(contour));
		}
	} // namespace

	DecorationContour BuildDecorationContour(const PathEndpointDecoration &decoration, const double strokeWidth)
	{
		DecorationContour empty;
		empty.filled = decoration.filled;
		if (!IsUsable(decoration, strokeWidth))
			return empty;
		const double length = strokeWidth * static_cast<double>(decoration.lengthScale);
		const double width = strokeWidth * static_cast<double>(decoration.widthScale);
		const double arrowWidth = width * 0.5;
		if (!std::isfinite(length) || !std::isfinite(width) || length <= 0.0 || width <= 0.0)
			return empty;
		switch (decoration.kind)
		{
			case PathDecorationKind::Arrow:
				return MakeContour(length, true, decoration.filled, {{0.0, 0.0}, {length, arrowWidth}, {length, 0.0}});
			case PathDecorationKind::Stealth:
				return MakeContour(length * 1.15, false, decoration.filled,
					{{0.0, 0.0}, {length * 0.65, arrowWidth * 1.1}, {length * 1.15, arrowWidth * 0.35}, {length * 1.15, 0.0}});
			case PathDecorationKind::Latex:
			{
				DecorationContour contour = MakeContour(length, true, decoration.filled, {});
				constexpr std::size_t samples = 9;
				contour.points.reserve(samples);
				for (std::size_t index = 0; index < samples; ++index)
				{
					const double fraction = static_cast<double>(index) / static_cast<double>(samples - 1u);
					const double bow = std::sin(std::numbers::pi * fraction);
					contour.points.push_back({length * fraction, arrowWidth * 1.1 * bow});
				}
				return ValidateContour(std::move(contour));
			}
			case PathDecorationKind::Bar:
			{
				const double barLength = length;
				DecorationContour contour = MakeContour(barLength * 0.75, true, decoration.filled,
					{{0.0, width}, {barLength, width}});
				return ValidateContour(std::move(contour));
			}
			case PathDecorationKind::Circle:
			{
				DecorationContour contour = MakeContour(length, true, decoration.filled, {});
				constexpr std::size_t segments = 16;
				const double radius = length * 0.5;
				const double centre = radius;
				contour.points.reserve(segments + 1u);
				for (std::size_t index = 0; index <= segments; ++index)
				{
					const double fraction = static_cast<double>(index) / static_cast<double>(segments);
					const double s = length * fraction;
					const double squared = std::max(0.0, radius * radius - (s - centre) * (s - centre));
					contour.points.push_back({s, std::sqrt(squared)});
				}
				return ValidateContour(std::move(contour));
			}
			case PathDecorationKind::Square:
				return MakeContour(length, true, decoration.filled,
					{{0.0, width}, {length * 0.5, width}, {length, width}});
			case PathDecorationKind::Diamond:
				return MakeContour(length, true, decoration.filled, {{0.0, 0.0}, {length * 0.5, arrowWidth}, {length, 0.0}});
			case PathDecorationKind::Kite:
				return MakeContour(length, true, decoration.filled, {{0.0, 0.0}, {length / 3.0, arrowWidth}, {length, 0.0}});
			case PathDecorationKind::None: break;
		}
		return empty;
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
