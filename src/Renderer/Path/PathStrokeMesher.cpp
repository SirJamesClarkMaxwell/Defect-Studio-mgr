#include "Core/dspch.hpp"

#include "Renderer/Path/PathStrokeMesher.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Renderer/Path/PathDash.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool IsFinite(const glm::dvec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] bool ValidGradient(const PathGradient &gradient)
		{
			if (!gradient.enabled)
				return true;
			double previous = 0.0;
			bool first = true;
			for (const PathGradientStop &stop : gradient.stops)
			{
				if (!std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
					!std::isfinite(stop.alpha) || !std::isfinite(stop.color.x) || !std::isfinite(stop.color.y) || !std::isfinite(stop.color.z) ||
					(!first && stop.position < previous))
					return false;
				previous = stop.position;
				first = false;
			}
			return true;
		}

		void AddDiagnostic(StrokeGeometry &geometry, const PathDiagnosticCode code, const char *message)
		{
			geometry.diagnostics.push_back({code, {}, message});
		}

		[[nodiscard]] EvaluatedSample AtLength(const EvaluatedPath &path, const double length)
		{
			auto upper = std::lower_bound(path.samples.begin(), path.samples.end(), length,
				[](const EvaluatedSample &sample, const double value) { return sample.arcLength < value; });
			if (upper == path.samples.begin() || upper == path.samples.end())
				return upper == path.samples.end() ? path.samples.back() : path.samples.front();
			const EvaluatedSample &a = *(upper - 1);
			const EvaluatedSample &b = *upper;
			const double span = b.arcLength - a.arcLength;
			const double t = span > 0.0 ? (length - a.arcLength) / span : 0.0;
			// Two adjacent frames can be near-opposite where a path doubles back, and the midpoint of two
			// opposite unit vectors is zero - normalising that is the one NaN source on this path.
			const auto blend = [t](const glm::dvec3 &from, const glm::dvec3 &to) {
				const glm::dvec3 mixed = glm::mix(from, to, t);
				return glm::dot(mixed, mixed) > 1e-12 ? glm::normalize(mixed) : from;
			};
			EvaluatedSample result = a;
			result.position = glm::mix(a.position, b.position, t);
			result.tangent = blend(a.tangent, b.tangent);
			result.normal = blend(a.normal, b.normal);
			result.binormal = blend(a.binormal, b.binormal);
			result.arcLength = length;
			result.normalizedT = path.totalLength > 0.0 ? length / path.totalLength : 0.0;
			return result;
		}

		[[nodiscard]] std::vector<EvaluatedSample> PieceSamples(const EvaluatedPath &path, const DashInterval &interval)
		{
			std::vector<EvaluatedSample> result;
			result.push_back(AtLength(path, interval.start));
			for (const EvaluatedSample &sample : path.samples)
				if (sample.arcLength > interval.start && sample.arcLength < interval.end)
					result.push_back(sample);
			result.push_back(AtLength(path, interval.end));
			return result;
		}

		// One quad band between two rings of `radialSegments` vertices. Both ring starts are explicit
		// because a cap's rings are appended after the whole shaft, not adjacent to the ring they attach
		// to - assuming `upper == lower + radialSegments` there stitched the wrong pair and left a hole.
		// `flip` reverses the winding for a band that grows against the tangent, i.e. the start cap.
		void StitchRings(StrokeGeometry &geometry, const std::uint32_t lower, const std::uint32_t upper,
			const std::uint32_t radialSegments, const bool flip = false)
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

		// Closes a ring with a centre fan. `outward` is the face normal of the resulting disc; `flip`
		// picks the winding so the disc faces away from the body it closes.
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

		// A real round cap: latitude rings revolved a quarter turn off the boundary ring. A single
		// centre fan would have been a flat disc, which is a Butt cap with extra triangles.
		void AppendHemisphereCap(StrokeGeometry &geometry, const EvaluatedSample &sample,
			const PathStrokeStyle &style, const bool end, const std::uint32_t boundaryRing)
		{
			constexpr std::uint32_t kCapRings = 3;
			const double radius = static_cast<double>(style.width) * 0.5;
			const glm::dvec3 axis = end ? sample.tangent : -sample.tangent;
			std::uint32_t lower = boundaryRing;
			for (std::uint32_t ring = 1; ring <= kCapRings; ++ring)
			{
				const double theta = 0.5 * std::numbers::pi * static_cast<double>(ring) / static_cast<double>(kCapRings);
				const double ringRadius = radius * std::cos(theta);
				const glm::dvec3 centre = sample.position + axis * (radius * std::sin(theta));
				const std::uint32_t upper = static_cast<std::uint32_t>(geometry.tubeVertices.size());
				for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
				{
					const double angle = 2.0 * std::numbers::pi * static_cast<double>(radial) / static_cast<double>(style.radialSegments);
					const glm::dvec3 radialVector = std::cos(angle) * sample.normal + std::sin(angle) * sample.binormal;
					// The apex ring is a single point on the axis; its normal comes from the axis, not from
					// a zero-length offset.
					const glm::dvec3 position = centre + radialVector * ringRadius;
					const glm::dvec3 offset = position - sample.position;
					geometry.tubeVertices.push_back({glm::vec3(position),
						glm::vec3(glm::dot(offset, offset) > 1e-18 ? glm::normalize(offset) : axis),
						SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT), static_cast<float>(sample.arcLength)});
				}
				StitchRings(geometry, lower, upper, style.radialSegments, !end);
				lower = upper;
			}
		}

		void AppendTubePiece(StrokeGeometry &geometry, std::vector<EvaluatedSample> samples, const PathStrokeStyle &style)
		{
			if (samples.size() < 2)
				return;
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			const float radius = style.width * 0.5f;
			if (style.cap == PathLineCap::Square)
			{
				samples.front().position -= samples.front().tangent * static_cast<double>(radius);
				samples.back().position += samples.back().tangent * static_cast<double>(radius);
			}
			for (const EvaluatedSample &sample : samples)
			{
				for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
				{
					const double angle = 2.0 * std::numbers::pi * static_cast<double>(radial) / static_cast<double>(style.radialSegments);
					const glm::dvec3 normal = std::cos(angle) * sample.normal + std::sin(angle) * sample.binormal;
					geometry.tubeVertices.push_back({glm::vec3(sample.position + normal * static_cast<double>(radius)), glm::vec3(normal),
						SampleStrokeColor(style, sample.normalizedT), static_cast<float>(sample.normalizedT), static_cast<float>(sample.arcLength)});
				}
			}
			for (std::size_t ring = 0; ring + 1u < samples.size(); ++ring)
			{
				const std::uint32_t lower = first + static_cast<std::uint32_t>(ring) * style.radialSegments;
				StitchRings(geometry, lower, lower + style.radialSegments, style.radialSegments);
			}
			if (style.cap == PathLineCap::Round)
				for (const bool end : {false, true})
					AppendHemisphereCap(geometry, end ? samples.back() : samples.front(), style, end,
						first + (end ? static_cast<std::uint32_t>(samples.size() - 1u) * style.radialSegments : 0u));
		}

		void AppendRibbonPiece(StrokeGeometry &geometry, const std::vector<EvaluatedSample> &samples, const PathStrokeStyle &style)
		{
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
			for (const EvaluatedSample &sample : samples)
				for (const float side : {-1.0f, 1.0f})
					geometry.ribbonVertices.push_back({glm::vec3(sample.position), glm::vec3(sample.tangent), glm::vec3(sample.normal),
						SampleStrokeColor(style, sample.normalizedT), side, static_cast<float>(sample.normalizedT), static_cast<float>(sample.arcLength), style.width * 0.5f});
			for (std::uint32_t index = 0; index + 1u < samples.size(); ++index)
			{
				const std::uint32_t a = first + index * 2u;
				geometry.indices.insert(geometry.indices.end(), {a, a + 2u, a + 3u, a, a + 3u, a + 1u});
			}
		}

		void AppendDecoration(StrokeGeometry &geometry, const DecorationContour &contour, const EvaluatedSample &endpoint,
			const bool start, const PathStrokeStyle &style, StrokeMeshRange &range)
		{
			if (contour.points.empty())
				return;
			range.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
			const glm::dvec3 inward = start ? endpoint.tangent : -endpoint.tangent;
			if (style.profile == StrokeProfile::Round)
			{
				const std::uint32_t first = static_cast<std::uint32_t>(geometry.tubeVertices.size());
				for (const DecorationContourPoint &point : contour.points)
					for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
					{
						const double angle = 2.0 * std::numbers::pi * radial / style.radialSegments;
						const glm::dvec3 radialVector = std::cos(angle) * endpoint.normal + std::sin(angle) * endpoint.binormal;
						const glm::dvec3 position = endpoint.position + inward * point.s + radialVector * point.halfWidth;
						geometry.tubeVertices.push_back({glm::vec3(position), glm::vec3(radialVector), SampleStrokeColor(style, endpoint.normalizedT),
							static_cast<float>(endpoint.normalizedT), static_cast<float>(endpoint.arcLength)});
					}
				for (std::size_t ring = 0; ring + 1u < contour.points.size(); ++ring)
				{
					const std::uint32_t lower = first + static_cast<std::uint32_t>(ring) * style.radialSegments;
					StitchRings(geometry, lower, lower + style.radialSegments, style.radialSegments);
				}
				// A contour that starts wide - Bar, Square - leaves an open disc facing the tip; close it.
				if (contour.points.front().halfWidth > 0.0)
					AppendRingFan(geometry, first, style, endpoint, -inward, false);
				if (contour.closesBack && contour.points.back().halfWidth > 0.0)
					AppendRingFan(geometry, first + static_cast<std::uint32_t>(contour.points.size() - 1u) * style.radialSegments,
						style, endpoint, inward, true);
			}
			else
			{
				const std::uint32_t first = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
				for (const DecorationContourPoint &point : contour.points)
					for (const float side : {-1.0f, 1.0f})
					{
						const bool cameraFacing = style.profile == StrokeProfile::CameraFacing;
						const glm::dvec3 position = cameraFacing
							? endpoint.position + inward * point.s
							: endpoint.position + inward * point.s + endpoint.normal * point.halfWidth * static_cast<double>(side);
						geometry.ribbonVertices.push_back({glm::vec3(position), glm::vec3(endpoint.tangent), glm::vec3(endpoint.normal),
							SampleStrokeColor(style, endpoint.normalizedT), cameraFacing ? side : 0.0f,
							static_cast<float>(endpoint.normalizedT), static_cast<float>(endpoint.arcLength),
							cameraFacing ? static_cast<float>(point.halfWidth) : 0.0f});
					}
				for (std::uint32_t point = 0; point + 1u < contour.points.size(); ++point)
				{
					const std::uint32_t a = first + point * 2u;
					geometry.indices.insert(geometry.indices.end(), {a, a + 2u, a + 3u, a, a + 3u, a + 1u});
				}
			}
			range.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
		}
	} // namespace

	glm::vec4 SampleStrokeColor(const PathStrokeStyle &style, const double normalizedT)
	{
		if (!style.gradient.enabled || style.gradient.stops.empty())
			return glm::vec4(style.color, style.alpha);
		const auto &stops = style.gradient.stops;
		if (normalizedT <= stops.front().position)
			return glm::vec4(stops.front().color, stops.front().alpha);
		if (normalizedT >= stops.back().position)
			return glm::vec4(stops.back().color, stops.back().alpha);
		for (std::size_t index = 1; index < stops.size(); ++index)
			if (normalizedT <= stops[index].position)
			{
				const PathGradientStop &a = stops[index - 1];
				const PathGradientStop &b = stops[index];
				const double fraction = (normalizedT - a.position) / (b.position - a.position);
				return glm::vec4(glm::mix(a.color, b.color, static_cast<float>(fraction)), glm::mix(a.alpha, b.alpha, static_cast<float>(fraction)));
			}
		return glm::vec4(style.color, style.alpha);
	}

	StrokeGeometry BuildStroke(const EvaluatedPath &evaluated, const PathStrokeStyle &style)
	{
		StrokeGeometry geometry;
		if (!std::isfinite(style.width) || style.width <= 0.0f || style.radialSegments < 3u)
		{
			AddDiagnostic(geometry, PathDiagnosticCode::InvalidStrokeStyle, "Stroke width or radial segment count is invalid.");
			return geometry;
		}
		if (!ValidGradient(style.gradient))
		{
			AddDiagnostic(geometry, PathDiagnosticCode::InvalidGradient, "Gradient stops must be finite, ordered and within [0, 1].");
			return geometry;
		}
		if (evaluated.samples.size() < 2u || !std::isfinite(evaluated.totalLength) || evaluated.totalLength <= 0.0)
			return geometry;
		for (const EvaluatedSample &sample : evaluated.samples)
			if (!IsFinite(sample.position) || !IsFinite(sample.tangent) || !IsFinite(sample.normal) || !IsFinite(sample.binormal))
				return geometry;
		const DecorationContour startContour = BuildDecorationContour(style.startDecoration, style.width);
		const DecorationContour endContour = BuildDecorationContour(style.endDecoration, style.width);
		geometry.shaftRange = TrimmedRange(evaluated.totalLength, style);
		AppendDecoration(geometry, startContour, evaluated.samples.front(), true, style, geometry.startDecoration);
		AppendDecoration(geometry, endContour, evaluated.samples.back(), false, style, geometry.endDecoration);
		if (geometry.shaftRange.IsEmpty())
		{
			if (startContour.trim + endContour.trim >= evaluated.totalLength)
				AddDiagnostic(geometry, PathDiagnosticCode::DecorationsExceedPathLength, "Endpoint decorations leave no shaft.");
			return geometry;
		}
		const std::vector<DashInterval> intervals = BuildDashIntervals(geometry.shaftRange.start, geometry.shaftRange.end, style.dash);
		geometry.dashedLength = DashCoverage(intervals);
		geometry.shaft.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
		for (const DashInterval &interval : intervals)
		{
			const std::vector<EvaluatedSample> samples = PieceSamples(evaluated, interval);
			if (style.profile == StrokeProfile::Round)
				AppendTubePiece(geometry, samples, style);
			else
				AppendRibbonPiece(geometry, samples, style);
		}
		geometry.shaft.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - geometry.shaft.firstIndex;
		return geometry;
	}
} // namespace DefectStudio
