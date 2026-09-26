#include "Core/dspch.hpp"

#include "Renderer/Path/PathDecorationMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace DefectStudio::detail
{
	namespace
	{
		[[nodiscard]] glm::dvec3 SafeNormal(const glm::dvec3 &value, const glm::dvec3 &fallback)
		{
			return glm::dot(value, value) > 1.0e-18 ? glm::normalize(value) : fallback;
		}

		void AppendRingFan(StrokeGeometry &geometry, const std::uint32_t ring, const PathStrokeStyle &style,
			const EvaluatedSample &sample, const glm::dvec3 &outward, const bool flip)
		{
			const std::uint32_t ringSize = CrossSectionRingSize(style);
			const std::uint32_t centre = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			glm::dvec3 centrePosition(0.0);
			for (std::uint32_t radial = 0; radial < ringSize; ++radial)
				centrePosition += glm::dvec3(geometry.tubeVertices[ring + radial].position);
			centrePosition /= static_cast<double>(ringSize);
			geometry.tubeVertices.push_back({glm::vec3(centrePosition), glm::vec3(outward),
				SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT), static_cast<float>(sample.arcLength)});
			for (std::uint32_t radial = 0; radial < ringSize; ++radial)
			{
				const std::uint32_t next = ring + (radial + 1u) % ringSize;
				if (flip)
					geometry.indices.insert(geometry.indices.end(), {centre, ring + radial, next});
				else
					geometry.indices.insert(geometry.indices.end(), {centre, next, ring + radial});
			}
		}

		[[nodiscard]] std::array<std::uint32_t, 4> CollapsedRing(const std::uint32_t ring,
			const bool normalCollapsed, const bool depthCollapsed)
		{
			std::array<std::uint32_t, 4> result = {ring, ring + 1u, ring + 2u, ring + 3u};
			if (normalCollapsed)
			{
				result[1] = result[0];
				result[3] = result[2];
			}
			if (depthCollapsed)
			{
				result[2] = result[1];
				result[3] = result[0];
			}
			return result;
		}

		void AppendTriangleIfDistinct(StrokeGeometry &geometry, const std::uint32_t a,
			const std::uint32_t b, const std::uint32_t c)
		{
			const glm::vec3 &positionA = geometry.tubeVertices[a].position;
			const glm::vec3 &positionB = geometry.tubeVertices[b].position;
			const glm::vec3 &positionC = geometry.tubeVertices[c].position;
			if (positionA == positionB || positionB == positionC || positionC == positionA)
				return;
			geometry.indices.insert(geometry.indices.end(), {a, b, c});
		}

		void StitchFlatCollapsedRings(StrokeGeometry &geometry, const std::uint32_t lower,
			const std::uint32_t upper, const bool lowerNormalCollapsed, const bool lowerDepthCollapsed,
			const bool upperNormalCollapsed, const bool upperDepthCollapsed, const bool flip)
		{
			const std::array<std::uint32_t, 4> lowerRing =
				CollapsedRing(lower, lowerNormalCollapsed, lowerDepthCollapsed);
			const std::array<std::uint32_t, 4> upperRing =
				CollapsedRing(upper, upperNormalCollapsed, upperDepthCollapsed);
			for (std::uint32_t radial = 0; radial < 4u; ++radial)
			{
				const std::uint32_t next = (radial + 1u) % 4u;
				const std::uint32_t a = lowerRing[radial];
				const std::uint32_t b = upperRing[radial];
				const std::uint32_t c = lowerRing[next];
				const std::uint32_t d = upperRing[next];
				if (flip)
				{
					AppendTriangleIfDistinct(geometry, a, d, b);
					AppendTriangleIfDistinct(geometry, a, c, d);
				}
				else
				{
					AppendTriangleIfDistinct(geometry, a, b, d);
					AppendTriangleIfDistinct(geometry, a, d, c);
				}
			}
		}

		[[nodiscard]] double InnerHalfWidth(const DecorationContourPoint &point,
			const PathStrokeStyle &style)
		{
			return std::max(0.0, point.halfWidth - static_cast<double>(style.width) * 0.5);
		}

		[[nodiscard]] bool InnerDepthCollapsed(const PathStrokeStyle &style)
		{
			return static_cast<double>(style.ribbonThickness) <= static_cast<double>(style.width);
		}

		void AppendFilledRoundDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style)
		{
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (const DecorationContourPoint &point : contour.points)
				AppendCrossSectionRing(geometry, endpoint.position + inward * point.s, endpoint, point.halfWidth, style);
			for (std::size_t ring = 0; ring + 1u < contour.points.size(); ++ring)
			{
				const std::uint32_t lower = first + static_cast<std::uint32_t>(ring) * CrossSectionRingSize(style);
				const std::uint32_t upper = lower + CrossSectionRingSize(style);
				if (style.profile == StrokeProfile::Flat &&
					(contour.points[ring].halfWidth == 0.0 || contour.points[ring + 1u].halfWidth == 0.0))
					StitchFlatCollapsedRings(geometry, lower, upper, contour.points[ring].halfWidth == 0.0,
						false, contour.points[ring + 1u].halfWidth == 0.0, false, false);
				else
					StitchRings(geometry, lower, upper, CrossSectionRingSize(style));
			}
			if (contour.points.front().halfWidth > 0.0)
				AppendRingFan(geometry, first, style, endpoint, -inward, false);
			if (contour.closesBack && contour.points.back().halfWidth > 0.0)
				AppendRingFan(geometry, first + static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style),
					style, endpoint, inward, true);
		}

		void AppendHollowRoundDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style)
		{
			const std::uint32_t firstOuter = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (const DecorationContourPoint &point : contour.points)
				AppendCrossSectionRing(geometry, endpoint.position + inward * point.s, endpoint, point.halfWidth, style);
			const std::uint32_t firstInner = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (const DecorationContourPoint &point : contour.points)
				AppendCrossSectionRing(geometry, endpoint.position + inward * point.s, endpoint,
					std::max(0.0, point.halfWidth - static_cast<double>(style.width) * 0.5), style, true);

			for (std::size_t ring = 0; ring + 1u < contour.points.size(); ++ring)
			{
				const std::uint32_t outer = firstOuter + static_cast<std::uint32_t>(ring) * CrossSectionRingSize(style);
				const std::uint32_t inner = firstInner + static_cast<std::uint32_t>(ring) * CrossSectionRingSize(style);
				const bool outerCollapsed = contour.points[ring].halfWidth == 0.0 ||
					contour.points[ring + 1u].halfWidth == 0.0;
				if (style.profile == StrokeProfile::Flat && outerCollapsed)
				{
					const bool lowerInnerCollapsed = InnerHalfWidth(contour.points[ring], style) == 0.0;
					const bool upperInnerCollapsed = InnerHalfWidth(contour.points[ring + 1u], style) == 0.0;
					StitchFlatCollapsedRings(geometry, outer, outer + CrossSectionRingSize(style),
						contour.points[ring].halfWidth == 0.0, false,
						contour.points[ring + 1u].halfWidth == 0.0, false, false);
					StitchFlatCollapsedRings(geometry, inner, inner + CrossSectionRingSize(style),
						lowerInnerCollapsed, InnerDepthCollapsed(style), upperInnerCollapsed,
						InnerDepthCollapsed(style), true);
				}
				else
				{
					StitchRings(geometry, outer, outer + CrossSectionRingSize(style), CrossSectionRingSize(style));
					StitchRings(geometry, inner, inner + CrossSectionRingSize(style), CrossSectionRingSize(style), true);
				}
			}
			if (contour.points.front().halfWidth > 0.0)
				StitchRings(geometry, firstOuter, firstInner, CrossSectionRingSize(style), true);
			else if (style.profile == StrokeProfile::Flat)
				StitchFlatCollapsedRings(geometry, firstOuter, firstInner, true, false, true,
					InnerDepthCollapsed(style), true);
			if (contour.closesBack && contour.points.back().halfWidth > 0.0)
				StitchRings(geometry, firstOuter + static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style),
					firstInner + static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style),
					CrossSectionRingSize(style));
			else if (contour.closesBack && style.profile == StrokeProfile::Flat)
			{
				const std::uint32_t last = static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style);
				StitchFlatCollapsedRings(geometry, firstOuter + last, firstInner + last, true, false, true,
					InnerDepthCollapsed(style), false);
			}
		}

		StrokeRibbonVertex MakeRibbonVertex(const DecorationContourPoint &point, const EvaluatedSample &endpoint,
			const glm::dvec3 &inward, const PathStrokeStyle &style, const float side, const bool inner)
		{
			const bool cameraFacing = style.profile == StrokeProfile::CameraFacing;
			const double radius = inner ? std::max(0.0, point.halfWidth - static_cast<double>(style.width) * 0.5) : point.halfWidth;
			const glm::dvec3 position = cameraFacing
				? endpoint.position + inward * point.s
				: endpoint.position + inward * point.s + endpoint.normal * radius * static_cast<double>(side);
			return {glm::vec3(position), glm::vec3(endpoint.tangent), glm::vec3(endpoint.normal),
				SampleStrokeColor(style, endpoint.normalizedT), cameraFacing ? side : 0.0f,
				static_cast<float>(endpoint.normalizedT), static_cast<float>(endpoint.arcLength),
				cameraFacing ? static_cast<float>(radius) : 0.0f};
		}

		void AppendFilledRibbonDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style)
		{
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
			for (const DecorationContourPoint &point : contour.points)
				for (const float side : {-1.0f, 1.0f})
					geometry.ribbonVertices.push_back(MakeRibbonVertex(point, endpoint, inward, style, side, false));
			for (std::uint32_t point = 0; point + 1u < contour.points.size(); ++point)
			{
				const std::uint32_t a = first + point * 2u;
				geometry.indices.insert(geometry.indices.end(), {a, a + 2u, a + 3u, a, a + 3u, a + 1u});
			}
		}

		void AppendHollowRibbonDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style)
		{
			struct Ring
			{
				std::uint32_t outerMinus = 0;
				std::uint32_t outerPlus = 0;
				std::uint32_t innerMinus = 0;
				std::uint32_t innerPlus = 0;
			};
			std::vector<Ring> rings;
			rings.reserve(contour.points.size());
			for (const DecorationContourPoint &point : contour.points)
			{
				const std::uint32_t outerMinus = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
				geometry.ribbonVertices.push_back(MakeRibbonVertex(point, endpoint, inward, style, -1.0f, false));
				const std::uint32_t outerPlus = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
				geometry.ribbonVertices.push_back(MakeRibbonVertex(point, endpoint, inward, style, 1.0f, false));
				const std::uint32_t innerMinus = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
				geometry.ribbonVertices.push_back(MakeRibbonVertex(point, endpoint, inward, style, -1.0f, true));
				const std::uint32_t innerPlus = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
				geometry.ribbonVertices.push_back(MakeRibbonVertex(point, endpoint, inward, style, 1.0f, true));
				rings.push_back({outerMinus, outerPlus, innerMinus, innerPlus});
			}
			for (std::size_t index = 0; index + 1u < rings.size(); ++index)
			{
				const Ring &a = rings[index];
				const Ring &b = rings[index + 1u];
				geometry.indices.insert(geometry.indices.end(), {a.outerMinus, b.outerMinus, b.innerMinus, a.outerMinus, b.innerMinus, a.innerMinus,
					a.outerPlus, a.innerPlus, b.innerPlus, a.outerPlus, b.innerPlus, b.outerPlus});
			}
			if (contour.points.front().halfWidth > 0.0)
				geometry.indices.insert(geometry.indices.end(), {rings.front().outerMinus, rings.front().innerMinus, rings.front().innerPlus,
					rings.front().outerMinus, rings.front().innerPlus, rings.front().outerPlus});
			if (contour.closesBack && contour.points.back().halfWidth > 0.0)
				geometry.indices.insert(geometry.indices.end(), {rings.back().outerMinus, rings.back().outerPlus, rings.back().innerPlus,
					rings.back().outerMinus, rings.back().innerPlus, rings.back().innerMinus});
		}
	}

	bool UsesTubeVertices(const PathStrokeStyle &style)
	{
		return style.profile == StrokeProfile::Round ||
			(style.profile == StrokeProfile::Flat && std::isfinite(style.ribbonThickness) && style.ribbonThickness > 0.0f);
	}

	std::uint32_t CrossSectionRingSize(const PathStrokeStyle &style)
	{
		return style.profile == StrokeProfile::Round ? style.radialSegments : 4u;
	}

	std::uint32_t AppendCrossSectionRing(StrokeGeometry &geometry, const glm::dvec3 &centre,
		const EvaluatedSample &sample, const double halfWidth, const PathStrokeStyle &style,
		const bool inner, const double scale)
	{
		const std::uint32_t first = static_cast<std::uint32_t>(geometry.tubeVertices.size());
		const glm::dvec3 normal = sample.normal;
		const glm::dvec3 binormal = sample.binormal;
		if (style.profile == StrokeProfile::Round)
		{
			const double radius = halfWidth * scale;
			for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
			{
				const double angle = 2.0 * std::numbers::pi * radial / style.radialSegments;
				const glm::dvec3 radialVector = std::cos(angle) * normal + std::sin(angle) * binormal;
				const glm::dvec3 position = centre + radialVector * radius;
				geometry.tubeVertices.push_back({glm::vec3(position), glm::vec3(inner ? -radialVector : radialVector),
					SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT),
					static_cast<float>(sample.arcLength)});
			}
			return first;
		}

		const double halfThickness = std::max(0.0, static_cast<double>(style.ribbonThickness)) * 0.5 * scale;
		const double innerHalfThickness = inner
			? std::max(0.0, halfThickness - static_cast<double>(style.width) * 0.5)
			: halfThickness;
		const double depth = inner ? innerHalfThickness : halfThickness;
		const std::array<glm::dvec3, 4> corners = {
			normal * halfWidth * scale + binormal * depth,
			-normal * halfWidth * scale + binormal * depth,
			-normal * halfWidth * scale - binormal * depth,
			normal * halfWidth * scale - binormal * depth};
		for (const glm::dvec3 &corner : corners)
		{
			const glm::dvec3 cornerNormal = SafeNormal(corner, inner ? -normal : normal);
			geometry.tubeVertices.push_back({glm::vec3(centre + corner), glm::vec3(inner ? -cornerNormal : cornerNormal),
				SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT),
				static_cast<float>(sample.arcLength)});
		}
		return first;
	}

	std::size_t BackContourPoint(const DecorationContour &contour)
	{
		if (contour.points.empty())
			return 0u;
		const double backS = contour.points.back().s;
		std::size_t result = contour.points.size() - 1u;
		for (std::size_t point = contour.points.size() - 1u; point > 0u; --point)
			if (contour.points[point - 1u].s == backS && contour.points[point - 1u].halfWidth > contour.points[result].halfWidth)
				result = point - 1u;
		return result;
	}

	void StitchRings(StrokeGeometry &geometry, const std::uint32_t lower, const std::uint32_t upper,
		const std::uint32_t radialSegments, const bool flip)
	{
		for (std::uint32_t radial = 0; radial < radialSegments; ++radial)
		{
			const std::uint32_t next = (radial + 1u) % radialSegments;
			const std::uint32_t a = lower + radial;
			const std::uint32_t b = upper + radial;
			const std::uint32_t c = lower + next;
			const std::uint32_t d = upper + next;
			if (flip)
				geometry.indices.insert(geometry.indices.end(), {a, d, b, a, c, d});
			else
				geometry.indices.insert(geometry.indices.end(), {a, b, d, a, d, c});
		}
	}

	void AppendDecoration(StrokeGeometry &geometry, const DecorationContour &contour, const EvaluatedSample &endpoint,
		const bool start, const PathStrokeStyle &style, StrokeMeshRange &range)
	{
		if (contour.points.empty())
			return;
		range.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
		const glm::dvec3 inward = start ? endpoint.tangent : -endpoint.tangent;
		if (UsesTubeVertices(style))
		{
			if (contour.filled)
				AppendFilledRoundDecoration(geometry, contour, endpoint, inward, style);
			else
				AppendHollowRoundDecoration(geometry, contour, endpoint, inward, style);
		}
		else if (contour.filled)
			AppendFilledRibbonDecoration(geometry, contour, endpoint, inward, style);
		else
			AppendHollowRibbonDecoration(geometry, contour, endpoint, inward, style);
		range.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
	}
} // namespace DefectStudio::detail
