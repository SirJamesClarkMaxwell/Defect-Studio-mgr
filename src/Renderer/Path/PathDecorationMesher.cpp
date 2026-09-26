#include "Core/dspch.hpp"

#include "Renderer/Path/PathStrokeMesher.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace DefectStudio::detail
{
	[[nodiscard]] std::size_t BackContourPoint(const DecorationContour &contour);
	void StitchRings(StrokeGeometry &geometry, std::uint32_t lower, std::uint32_t upper,
		std::uint32_t radialSegments, bool flip = false);

	namespace
	{
		void AppendRingFan(StrokeGeometry &geometry, const std::uint32_t ring, const PathStrokeStyle &style,
			const EvaluatedSample &sample, const glm::dvec3 &outward, const bool flip)
		{
			const std::uint32_t centre = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			glm::dvec3 centrePosition(0.0);
			for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
				centrePosition += glm::dvec3(geometry.tubeVertices[ring + radial].position);
			centrePosition /= static_cast<double>(style.radialSegments);
			geometry.tubeVertices.push_back({glm::vec3(centrePosition), glm::vec3(outward),
				SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT), static_cast<float>(sample.arcLength)});
			for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
			{
				const std::uint32_t next = ring + (radial + 1u) % style.radialSegments;
				if (flip)
					geometry.indices.insert(geometry.indices.end(), {centre, ring + radial, next});
				else
					geometry.indices.insert(geometry.indices.end(), {centre, next, ring + radial});
			}
		}

		void AppendTubeRing(StrokeGeometry &geometry, const DecorationContourPoint &point,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style,
			const bool inner)
		{
			const double wallRadius = style.width * 0.5;
			const double radius = inner ? std::max(0.0, point.halfWidth - wallRadius) : point.halfWidth;
			for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
			{
				const double angle = 2.0 * std::numbers::pi * radial / style.radialSegments;
				const glm::dvec3 radialVector = std::cos(angle) * endpoint.normal + std::sin(angle) * endpoint.binormal;
				const glm::dvec3 position = endpoint.position + inward * point.s + radialVector * radius;
				geometry.tubeVertices.push_back({glm::vec3(position), glm::vec3(inner ? -radialVector : radialVector),
					SampleStrokeColor(style, endpoint.normalizedT), static_cast<float>(endpoint.normalizedT),
					static_cast<float>(endpoint.arcLength)});
			}
		}

		void AppendFilledRoundDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style,
			std::vector<std::uint32_t> &backRing)
		{
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (const DecorationContourPoint &point : contour.points)
			{
				AppendTubeRing(geometry, point, endpoint, inward, style, false);
			}
			for (std::size_t ring = 0; ring + 1u < contour.points.size(); ++ring)
				StitchRings(geometry, first + static_cast<std::uint32_t>(ring) * style.radialSegments,
					first + static_cast<std::uint32_t>(ring + 1u) * style.radialSegments, style.radialSegments);
			if (contour.points.front().halfWidth > 0.0)
				AppendRingFan(geometry, first, style, endpoint, -inward, false);
			if (contour.closesBack && contour.points.back().halfWidth > 0.0)
				AppendRingFan(geometry, first + static_cast<std::uint32_t>(contour.points.size() - 1u) * style.radialSegments,
					style, endpoint, inward, true);
			const std::uint32_t back = first + static_cast<std::uint32_t>(BackContourPoint(contour)) * style.radialSegments;
			for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
				backRing.push_back(back + radial);
		}

		void AppendHollowRoundDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style,
			std::vector<std::uint32_t> &backRing)
		{
			const std::uint32_t firstOuter = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (const DecorationContourPoint &point : contour.points)
				AppendTubeRing(geometry, point, endpoint, inward, style, false);
			const std::uint32_t firstInner = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (const DecorationContourPoint &point : contour.points)
				AppendTubeRing(geometry, point, endpoint, inward, style, true);

			for (std::size_t ring = 0; ring + 1u < contour.points.size(); ++ring)
			{
				const std::uint32_t outer = firstOuter + static_cast<std::uint32_t>(ring) * style.radialSegments;
				const std::uint32_t inner = firstInner + static_cast<std::uint32_t>(ring) * style.radialSegments;
				StitchRings(geometry, outer, outer + style.radialSegments, style.radialSegments);
				StitchRings(geometry, inner, inner + style.radialSegments, style.radialSegments, true);
			}
			if (contour.points.front().halfWidth > 0.0)
				StitchRings(geometry, firstOuter, firstInner, style.radialSegments, true);
			if (contour.closesBack && contour.points.back().halfWidth > 0.0)
				StitchRings(geometry, firstOuter + static_cast<std::uint32_t>(contour.points.size() - 1u) * style.radialSegments,
					firstInner + static_cast<std::uint32_t>(contour.points.size() - 1u) * style.radialSegments,
					style.radialSegments);

			const std::uint32_t back = firstOuter + static_cast<std::uint32_t>(BackContourPoint(contour)) * style.radialSegments;
			for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
				backRing.push_back(back + radial);
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
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style,
			std::vector<std::uint32_t> &backRing)
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
			const std::uint32_t back = first + static_cast<std::uint32_t>(BackContourPoint(contour)) * 2u;
			backRing = {back, back + 1u};
		}

		void AppendHollowRibbonDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style,
			std::vector<std::uint32_t> &backRing)
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
			const std::size_t backIndex = BackContourPoint(contour);
			const Ring &back = rings[backIndex];
			backRing = {back.outerMinus, back.outerPlus};
		}
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
		const bool start, const PathStrokeStyle &style, StrokeMeshRange &range, std::vector<std::uint32_t> &backRing)
	{
		backRing.clear();
		if (contour.points.empty())
			return;
		range.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
		const glm::dvec3 inward = start ? endpoint.tangent : -endpoint.tangent;
		if (style.profile == StrokeProfile::Round)
		{
			if (contour.filled)
				AppendFilledRoundDecoration(geometry, contour, endpoint, inward, style, backRing);
			else
				AppendHollowRoundDecoration(geometry, contour, endpoint, inward, style, backRing);
		}
		else if (contour.filled)
			AppendFilledRibbonDecoration(geometry, contour, endpoint, inward, style, backRing);
		else
			AppendHollowRibbonDecoration(geometry, contour, endpoint, inward, style, backRing);
		range.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
	}
} // namespace DefectStudio::detail
