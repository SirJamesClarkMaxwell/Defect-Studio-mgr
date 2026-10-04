#include "Core/dspch.hpp"

#include "Renderer/Path/PathDecorationMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace DefectStudio::detail
{
	namespace
	{
		[[nodiscard]] std::uint32_t BevelSegmentCount(const PathStrokeStyle &style)
		{
			constexpr std::uint32_t maxSegments = std::numeric_limits<std::uint32_t>::max() / 8u - 1u;
			return std::min(std::max(1u, style.ribbonBevelSegments), maxSegments);
		}

		[[nodiscard]] glm::dvec3 SafeNormal(const glm::dvec3 &value, const glm::dvec3 &fallback)
		{
			return glm::dot(value, value) > 1.0e-18 ? glm::normalize(value) : fallback;
		}

		// StitchRings is shared with Round and has no style argument; repeated adjacent face normals
		// are the Flat ring's marker, while Round keeps its radial normals and original stitching.
		[[nodiscard]] bool HasFlatFaceVertices(const StrokeGeometry &geometry, const std::uint32_t ring,
			const std::uint32_t ringSize)
		{
			if (ringSize < 2u || ringSize % 2u != 0u)
				return false;
			for (std::uint32_t radial = 0; radial < ringSize; radial += 2u)
				if (geometry.tubeVertices[ring + radial].normal != geometry.tubeVertices[ring + radial + 1u].normal)
					return false;
			return true;
		}

		void AppendRingFan(StrokeGeometry &geometry, const std::uint32_t ring, const PathStrokeStyle &style,
			const EvaluatedSample &sample, const glm::dvec3 &outward, const bool flip)
		{
			const std::uint32_t ringSize = CrossSectionRingSize(style);
			const bool flatRing = HasFlatFaceVertices(geometry, ring, ringSize);
			const std::uint32_t boundary = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (std::uint32_t radial = 0; radial < ringSize; ++radial)
			{
				auto vertex = geometry.tubeVertices[ring + radial];
				vertex.normal = glm::vec3(outward);
				vertex.smoothingGroup = 0;
				geometry.tubeVertices.push_back(vertex);
			}
			const std::uint32_t centre = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			glm::dvec3 centrePosition(0.0);
			for (std::uint32_t radial = 0; radial < ringSize; ++radial)
				centrePosition += glm::dvec3(geometry.tubeVertices[ring + radial].position);
			centrePosition /= static_cast<double>(ringSize);
			geometry.tubeVertices.push_back({glm::vec3(centrePosition), glm::vec3(outward),
				geometry.tubeVertices[ring].color, static_cast<float>(sample.normalizedT), static_cast<float>(sample.arcLength)});
			for (std::uint32_t radial = 0; radial < ringSize; radial += flatRing ? 2u : 1u)
			{
				const std::uint32_t next = boundary + (flatRing ? radial + 1u : (radial + 1u) % ringSize);
				if (flip)
					geometry.indices.insert(geometry.indices.end(), {centre, boundary + radial, next});
				else
					geometry.indices.insert(geometry.indices.end(), {centre, next, boundary + radial});
			}
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

		void SetRingNormals(StrokeGeometry &geometry, const std::uint32_t ring,
			const std::uint32_t ringSize, const glm::dvec3 &normal)
		{
			for (std::uint32_t radial = 0; radial < ringSize; ++radial)
				geometry.tubeVertices[ring + radial].normal = glm::vec3(normal);
		}

		void StitchFlatCollapsedRings(StrokeGeometry &geometry, const std::uint32_t lower,
			const std::uint32_t upper, const std::uint32_t ringSize, const bool flip)
		{
			for (std::uint32_t radial = 0; radial < ringSize; radial += 2u)
			{
				const std::uint32_t next = radial + 1u;
				const std::uint32_t a = lower + radial;
				const std::uint32_t b = upper + radial;
				const std::uint32_t c = lower + next;
				const std::uint32_t d = upper + next;
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

		void AppendFilledRoundDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style,
			const bool start)
		{
			const bool flip = start;
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			for (const DecorationContourPoint &point : contour.points)
				AppendCrossSectionRing(geometry, endpoint.position + inward * point.s, endpoint, point.halfWidth, style);
			const bool sameSBackClosure = contour.closesBack && contour.points.size() > 1u &&
				contour.points.back().halfWidth == 0.0 &&
				contour.points[contour.points.size() - 2u].s == contour.points.back().s;
			if (style.profile == StrokeProfile::Flat && sameSBackClosure)
			{
				// The closure uses the same start-side winding reversal as the body. Its collapsed
				// ring is a cap boundary, however, so its normals must follow that reversal too.
				SetRingNormals(geometry,
					first + static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style),
					CrossSectionRingSize(style), inward);
			}
			for (std::size_t ring = 0; ring + 1u < contour.points.size(); ++ring)
			{
				const std::uint32_t lower = first + static_cast<std::uint32_t>(ring) * CrossSectionRingSize(style);
				const std::uint32_t upper = lower + CrossSectionRingSize(style);
				if (style.profile == StrokeProfile::Flat &&
					(contour.points[ring].halfWidth == 0.0 || contour.points[ring + 1u].halfWidth == 0.0))
					StitchFlatCollapsedRings(geometry, lower, upper, CrossSectionRingSize(style), flip);
				else
					StitchRings(geometry, lower, upper, CrossSectionRingSize(style), flip);
			}
			if (contour.points.front().halfWidth > 0.0)
				AppendRingFan(geometry, first, style, endpoint, -inward,
					style.profile == StrokeProfile::Flat ? !start : false);
			if (contour.closesBack && contour.points.back().halfWidth > 0.0)
				AppendRingFan(geometry, first + static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style),
					style, endpoint, inward, style.profile == StrokeProfile::Flat ? start : true);
		}

		void AppendHollowRoundDecoration(StrokeGeometry &geometry, const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const PathStrokeStyle &style,
			const bool start)
		{
			const bool flip = start;
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
					StitchFlatCollapsedRings(geometry, outer, outer + CrossSectionRingSize(style),
						CrossSectionRingSize(style), flip);
					StitchFlatCollapsedRings(geometry, inner, inner + CrossSectionRingSize(style),
						CrossSectionRingSize(style), !flip);
				}
				else
				{
					StitchRings(geometry, outer, outer + CrossSectionRingSize(style), CrossSectionRingSize(style), flip);
					StitchRings(geometry, inner, inner + CrossSectionRingSize(style), CrossSectionRingSize(style), !flip);
				}
			}
			if (contour.points.front().halfWidth > 0.0)
				StitchRings(geometry, firstOuter, firstInner, CrossSectionRingSize(style), !flip);
			else if (style.profile == StrokeProfile::Flat)
				StitchFlatCollapsedRings(geometry, firstOuter, firstInner, CrossSectionRingSize(style), !flip);
			if (contour.closesBack && contour.points.back().halfWidth > 0.0)
				StitchRings(geometry, firstOuter + static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style),
					firstInner + static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style),
					CrossSectionRingSize(style), flip);
			else if (contour.closesBack && style.profile == StrokeProfile::Flat)
			{
				const std::uint32_t last = static_cast<std::uint32_t>(contour.points.size() - 1u) * CrossSectionRingSize(style);
				StitchFlatCollapsedRings(geometry, firstOuter + last, firstInner + last,
					CrossSectionRingSize(style), flip);
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

	void AppendFlatCap(StrokeGeometry &geometry, const std::uint32_t ring, const PathStrokeStyle &style,
		const EvaluatedSample &sample, const bool end)
	{
		AppendRingFan(geometry, ring, style, sample, end ? sample.tangent : -sample.tangent,
			end);
	}

	bool UsesTubeVertices(const PathStrokeStyle &style)
	{
		return style.profile == StrokeProfile::Round ||
			(style.profile == StrokeProfile::Flat && std::isfinite(style.ribbonThickness) && style.ribbonThickness > 0.0f);
	}

	std::uint32_t CrossSectionRingSize(const PathStrokeStyle &style)
	{
		if (style.profile == StrokeProfile::Round)
			return style.radialSegments;
		if (style.profile != StrokeProfile::Flat || !std::isfinite(style.ribbonBevel) || style.ribbonBevel <= 0.0f)
			return 8u;
		const std::uint32_t bevelSegments = BevelSegmentCount(style);
		return 8u * (bevelSegments + 1u);
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
		if (style.profile == StrokeProfile::Flat && std::isfinite(style.ribbonBevel) && style.ribbonBevel > 0.0f)
		{
			const double maxBevel = std::max(0.0, std::min(halfWidth, std::max(0.0, static_cast<double>(style.ribbonThickness) * 0.5)));
			const double bevel = std::min(static_cast<double>(style.ribbonBevel), maxBevel) * scale;
			const double halfWidthScaled = halfWidth * scale;
			const double bevelAlongWidth = std::min(bevel, halfWidthScaled);
			const double bevelAlongDepth = std::min(bevel, depth);
			const std::uint32_t bevelSegments = BevelSegmentCount(style);
			if (bevelSegments > 0u)
			{
				const double shape = std::clamp(
					std::isfinite(style.ribbonBevelShape) ? static_cast<double>(style.ribbonBevelShape) : 0.5,
					0.0, 1.0);
				const auto appendFace = [&](const glm::dvec3 &firstPosition, const glm::dvec3 &secondPosition,
					const glm::dvec3 &faceNormal) {
					const glm::vec3 normalValue = glm::vec3(inner ? -faceNormal : faceNormal);
					geometry.tubeVertices.push_back({glm::vec3(centre + firstPosition), normalValue,
						SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT),
						static_cast<float>(sample.arcLength)});
					geometry.tubeVertices.push_back({glm::vec3(centre + secondPosition), normalValue,
						SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT),
						static_cast<float>(sample.arcLength)});
				};
				const auto appendBevelCorner = [&](const glm::dvec3 &corner, const glm::dvec3 &startOffset,
					const glm::dvec3 &endOffset) {
					const auto pointAt = [&](const double fraction) {
						double startCoordinate = 0.0;
						double endCoordinate = 0.0;
						if (shape == 0.0)
						{
							if (fraction <= 0.5)
								startCoordinate = 1.0 - 2.0 * fraction;
							else
								endCoordinate = 2.0 * fraction - 1.0;
						}
						else if (shape == 1.0)
						{
							if (fraction <= 0.5)
							{
								startCoordinate = 1.0;
								endCoordinate = 2.0 * fraction;
							}
							else
							{
								startCoordinate = 2.0 - 2.0 * fraction;
								endCoordinate = 1.0;
							}
						}
						else
						{
							const double superellipseExponent = -std::log(2.0) /
								std::log(std::sqrt(shape));
							const double power = 2.0 / superellipseExponent;
							const double angle = 0.5 * std::numbers::pi * fraction;
							startCoordinate = std::pow(std::cos(angle), power);
							endCoordinate = std::pow(std::sin(angle), power);
						}
						return corner + startOffset * startCoordinate + endOffset * endCoordinate;
					};
					glm::dvec3 previous = pointAt(0.0);
					for (std::uint32_t segment = 0; segment < bevelSegments; ++segment)
					{
						const glm::dvec3 next = pointAt(static_cast<double>(segment + 1u) / bevelSegments);
						const glm::dvec3 edge = next - previous;
						const glm::dvec3 edgeNormal = glm::dot(edge, binormal) * normal - glm::dot(edge, normal) * binormal;
						const glm::dvec3 faceNormal = SafeNormal(edgeNormal, inner ? -normal : normal);
						appendFace(previous, next, faceNormal);
						previous = next;
					}
				};

				appendFace(corners[0] - normal * bevelAlongWidth, corners[1] + normal * bevelAlongWidth, binormal);
				appendBevelCorner(corners[1], normal * bevelAlongWidth, -binormal * bevelAlongDepth);
				appendFace(corners[1] - binormal * bevelAlongDepth, corners[2] + binormal * bevelAlongDepth, -normal);
				appendBevelCorner(corners[2], binormal * bevelAlongDepth, normal * bevelAlongWidth);
				appendFace(corners[2] + normal * bevelAlongWidth, corners[3] - normal * bevelAlongWidth, -binormal);
				appendBevelCorner(corners[3], -normal * bevelAlongWidth, binormal * bevelAlongDepth);
				appendFace(corners[3] + binormal * bevelAlongDepth, corners[0] - binormal * bevelAlongDepth, normal);
				appendBevelCorner(corners[0], -binormal * bevelAlongDepth, -normal * bevelAlongWidth);
				return first;
			}
			const std::array<glm::dvec3, 8> positions = {
				normal * (halfWidthScaled - bevelAlongWidth) + binormal * depth,
				normal * (-halfWidthScaled + bevelAlongWidth) + binormal * depth,
				normal * -halfWidthScaled + binormal * (depth - bevelAlongDepth),
				normal * -halfWidthScaled + binormal * (-depth + bevelAlongDepth),
				normal * (-halfWidthScaled + bevelAlongWidth) - binormal * depth,
				normal * (halfWidthScaled - bevelAlongWidth) - binormal * depth,
				normal * halfWidthScaled + binormal * (-depth + bevelAlongDepth),
				normal * halfWidthScaled + binormal * (depth - bevelAlongDepth)};
			const std::array<glm::dvec3, 8> faceNormals = {
				binormal, -normal + binormal, -normal, -normal - binormal,
				-binormal, normal - binormal, normal, normal + binormal};
			for (std::uint32_t face = 0; face < 8u; ++face)
			{
				const glm::dvec3 faceNormal = SafeNormal(faceNormals[face], inner ? -normal : normal);
				for (const std::uint32_t point : {face, (face + 1u) % 8u})
					geometry.tubeVertices.push_back({glm::vec3(centre + positions[point]),
						glm::vec3(inner ? -faceNormal : faceNormal),
						SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT),
						static_cast<float>(sample.arcLength)});
			}
			return first;
		}
		const std::array<std::uint32_t, 8> cornerIndices = {0u, 1u, 1u, 2u, 2u, 3u, 3u, 0u};
		const std::array<glm::dvec3, 8> faceNormals = {
			binormal, binormal, -normal, -normal, -binormal, -binormal, normal, normal};
		for (std::uint32_t radial = 0; radial < 8u; ++radial)
		{
			const glm::dvec3 faceNormal = SafeNormal(faceNormals[radial], inner ? -normal : normal);
			geometry.tubeVertices.push_back({glm::vec3(centre + corners[cornerIndices[radial]]),
				glm::vec3(inner ? -faceNormal : faceNormal),
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
		const bool flatRing = HasFlatFaceVertices(geometry, lower, radialSegments);
		for (std::uint32_t radial = 0; radial < radialSegments; radial += flatRing ? 2u : 1u)
		{
			const std::uint32_t next = flatRing ? radial + 1u : (radial + 1u) % radialSegments;
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
				AppendFilledRoundDecoration(geometry, contour, endpoint, inward, style, start);
			else
				AppendHollowRoundDecoration(geometry, contour, endpoint, inward, style, start);
		}
		else if (contour.filled)
			AppendFilledRibbonDecoration(geometry, contour, endpoint, inward, style);
		else
			AppendHollowRibbonDecoration(geometry, contour, endpoint, inward, style);
		range.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
	}
} // namespace DefectStudio::detail
