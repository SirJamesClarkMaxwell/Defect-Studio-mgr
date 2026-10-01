#include "Core/dspch.hpp"

#include "Renderer/Path/PathStrokeMesher.hpp"

#include "Renderer/Path/PathDecorationMesher.hpp"
#include "Renderer/Path/PathSolidMesher.hpp"

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

		struct GradientPieceSample
		{
			EvaluatedSample sample;
			glm::vec4 color{1.0f};
			bool isGradientStop = false;
		};

		[[nodiscard]] std::vector<GradientPieceSample> GradientSamples(const EvaluatedPath &path,
			const DashInterval &interval, const PathStrokeStyle &style)
		{
			std::vector<GradientPieceSample> result;
			for (const EvaluatedSample &sample : PieceSamples(path, interval))
				result.push_back({sample, SampleStrokeColor(style, sample.normalizedT), false});

			for (const PathGradientStop &stop : style.gradient.stops)
			{
				const double length = static_cast<double>(stop.position) * path.totalLength;
				if (length < interval.start || length > interval.end)
					continue;
				const auto existing = std::find_if(result.begin(), result.end(), [length](const GradientPieceSample &sample) {
					return !sample.isGradientStop && sample.sample.arcLength == length;
				});
				const glm::vec4 color(stop.color, stop.alpha);
				if (existing != result.end())
				{
					existing->color = color;
					existing->isGradientStop = true;
				}
				else
					result.push_back({AtLength(path, length), color, true});
			}

			std::stable_sort(result.begin(), result.end(), [](const GradientPieceSample &a, const GradientPieceSample &b) {
				return a.sample.arcLength < b.sample.arcLength;
			});
			return result;
		}

		[[nodiscard]] EvaluatedSample DecorationBackSample(const EvaluatedSample &sampled, const EvaluatedSample &endpoint,
			const DecorationContour &contour, const bool start)
		{
			if (contour.points.empty())
				return sampled;
			const DecorationContourPoint &back = contour.points[detail::BackContourPoint(contour)];
			EvaluatedSample result = sampled;
			const glm::dvec3 inward = start ? endpoint.tangent : -endpoint.tangent;
			result.position = endpoint.position + inward * back.s;
			result.tangent = endpoint.tangent;
			result.normal = endpoint.normal;
			result.binormal = endpoint.binormal;
			return result;
		}

		[[nodiscard]] bool NeedsDecorationHandoff(const EvaluatedSample &sampled, const EvaluatedSample &endpoint,
			const DecorationContour &contour, const bool start)
		{
			if (contour.points.empty())
				return false;
			const EvaluatedSample desired = DecorationBackSample(sampled, endpoint, contour, start);
			return glm::distance(sampled.position, desired.position) > 1.0e-12 ||
				glm::distance(sampled.tangent, desired.tangent) > 1.0e-12 ||
				glm::distance(sampled.normal, desired.normal) > 1.0e-12 ||
				glm::distance(sampled.binormal, desired.binormal) > 1.0e-12;
		}

		// A real round cap: latitude rings revolved a quarter turn off the boundary ring. A single
		// centre fan would have been a flat disc, which is a Butt cap with extra triangles.
		void AppendHemisphereCap(StrokeGeometry &geometry, const EvaluatedSample &sample,
			const PathStrokeStyle &style, const bool end, const std::uint32_t boundaryRing,
			const glm::vec4 *color, const bool startHandoff)
		{
			constexpr std::uint32_t kCapRings = 3;
			const double radius = static_cast<double>(style.width) * 0.5;
			const std::uint32_t ringSize = detail::CrossSectionRingSize(style);
			const glm::dvec3 axis = end ? sample.tangent : -sample.tangent;
			std::uint32_t lower = boundaryRing;
			for (std::uint32_t ring = 1; ring <= kCapRings; ++ring)
			{
				const double theta = 0.5 * std::numbers::pi * static_cast<double>(ring) / static_cast<double>(kCapRings);
				const glm::dvec3 centre = sample.position + axis * (radius * std::sin(theta));
				const std::uint32_t upper = detail::AppendCrossSectionRing(
					geometry, centre, sample, radius, style, false, std::cos(theta));
				if (color != nullptr)
					for (std::uint32_t radial = 0; radial < ringSize; ++radial)
						geometry.tubeVertices[upper + radial].color = *color;
				for (std::uint32_t radial = 0; radial < ringSize; ++radial)
				{
					// The apex ring is a single point on the axis; its normal comes from the axis, not from
					// a zero-length offset.
					const glm::dvec3 position = glm::dvec3(geometry.tubeVertices[upper + radial].position);
					const glm::dvec3 offset = position - sample.position;
					geometry.tubeVertices[upper + radial].normal = glm::vec3(
						glm::dot(offset, offset) > 1e-18 ? glm::normalize(offset) : axis);
				}
				detail::StitchRings(geometry, lower, upper, ringSize,
					style.profile == StrokeProfile::Flat ? end : (startHandoff ? end : !end));
				lower = upper;
			}
		}

		void AppendTubePiece(StrokeGeometry &geometry, std::vector<EvaluatedSample> samples, const PathStrokeStyle &style,
			const bool capStart, const bool capEnd, const bool startHandoff,
			const std::vector<glm::vec4> *colors = nullptr)
		{
			if (samples.size() < 2)
				return;
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			const float radius = style.width * 0.5f;
			const std::uint32_t ringSize = detail::CrossSectionRingSize(style);
			if (style.cap == PathLineCap::Square)
			{
				samples.front().position -= samples.front().tangent * static_cast<double>(radius);
				samples.back().position += samples.back().tangent * static_cast<double>(radius);
			}
			const bool hasColors = colors != nullptr && colors->size() == samples.size();
			for (std::size_t index = 0; index < samples.size(); ++index)
			{
				const std::uint32_t ring = detail::AppendCrossSectionRing(geometry, samples[index].position,
					samples[index], radius, style);
				if (hasColors)
					for (std::uint32_t radial = 0; radial < ringSize; ++radial)
						geometry.tubeVertices[ring + radial].color = (*colors)[index];
			}
			for (std::size_t ring = 0; ring + 1u < samples.size(); ++ring)
			{
				const std::uint32_t lower = first + static_cast<std::uint32_t>(ring) * ringSize;
				const std::uint32_t upper = first + static_cast<std::uint32_t>(ring + 1u) * ringSize;
				detail::StitchRings(geometry, lower, upper, ringSize,
					style.profile == StrokeProfile::Flat || (startHandoff && ring == 0u));
			}
			if (style.cap == PathLineCap::Round)
			{
				if (capStart)
					AppendHemisphereCap(geometry, samples.front(), style, false, first,
						hasColors ? &(*colors)[0] : nullptr, startHandoff);
				if (capEnd)
					AppendHemisphereCap(geometry, samples.back(), style, true,
						first + static_cast<std::uint32_t>(samples.size() - 1u) * ringSize,
						hasColors ? &colors->back() : nullptr, false);
			}
		}

		void AppendRibbonPiece(StrokeGeometry &geometry, const std::vector<EvaluatedSample> &samples, const PathStrokeStyle &style,
			const std::vector<glm::vec4> *colors = nullptr)
		{
			const std::uint32_t first = static_cast<std::uint32_t>(geometry.ribbonVertices.size());
			const bool hasColors = colors != nullptr && colors->size() == samples.size();
			for (std::size_t index = 0; index < samples.size(); ++index)
			{
				const EvaluatedSample &sample = samples[index];
				for (const float side : {-1.0f, 1.0f})
					geometry.ribbonVertices.push_back({glm::vec3(sample.position), glm::vec3(sample.tangent), glm::vec3(sample.normal),
						hasColors ? (*colors)[index] : SampleStrokeColor(style, sample.normalizedT), side,
						static_cast<float>(sample.normalizedT), static_cast<float>(sample.arcLength), style.width * 0.5f});
			}
			for (std::uint32_t index = 0; index + 1u < samples.size(); ++index)
			{
				const std::uint32_t a = first + index * 2u;
				geometry.indices.insert(geometry.indices.end(), {a, a + 2u, a + 3u, a, a + 3u, a + 1u});
			}
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
				if (b.position == a.position)
					return glm::vec4(b.color, b.alpha);
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
		const bool plainStroke = startContour.points.empty() && endContour.points.empty();
		const bool straightPlainSolid = plainStroke && evaluated.samples.size() == 2u &&
			style.profile == StrokeProfile::Flat && std::isfinite(style.ribbonThickness) &&
			style.ribbonThickness > 0.0f;
		const bool bevelledDecoratedSolid = !plainStroke &&
			detail::UsesThickFlatSolidBevel(style, startContour, endContour);
		if (straightPlainSolid || bevelledDecoratedSolid)
		{
			detail::ThickFlatMesh mesh;
			if (geometry.shaftRange.IsEmpty())
			{
				if (startContour.trim + endContour.trim >= evaluated.totalLength)
					AddDiagnostic(geometry, PathDiagnosticCode::DecorationsExceedPathLength, "Endpoint decorations leave no shaft.");
				detail::AppendAttachedThickFlatDecoration(mesh, startContour, evaluated.samples.front(), true,
					style, detail::ThickFlatFaceOwner::StartDecoration, false);
				detail::AppendAttachedThickFlatDecoration(mesh, endContour, evaluated.samples.back(), false,
					style, detail::ThickFlatFaceOwner::EndDecoration, false);
				detail::FinalizeThickFlatMesh(mesh, style, geometry);
				return geometry;
			}

			const std::vector<DashInterval> intervals = BuildDashIntervals(
				geometry.shaftRange.start, geometry.shaftRange.end, style.dash);
			geometry.dashedLength = DashCoverage(intervals);
			const bool hasStartAttachment = !intervals.empty() && intervals.front().start == geometry.shaftRange.start;
			const bool hasEndAttachment = !intervals.empty() && intervals.back().end == geometry.shaftRange.end;
			const bool hasGradientSamples = style.gradient.enabled && !style.gradient.stops.empty();
			for (const DashInterval &interval : intervals)
			{
				std::vector<EvaluatedSample> samples;
				std::vector<glm::vec4> sampleColors;
				if (hasGradientSamples)
				{
					const std::vector<GradientPieceSample> gradientSamples = GradientSamples(evaluated, interval, style);
					samples.reserve(gradientSamples.size());
					sampleColors.reserve(gradientSamples.size());
					for (const GradientPieceSample &gradientSample : gradientSamples)
					{
						samples.push_back(gradientSample.sample);
						sampleColors.push_back(gradientSample.color);
					}
				}
				else
					samples = PieceSamples(evaluated, interval);
				const bool atStart = interval.start == geometry.shaftRange.start;
				const bool atEnd = interval.end == geometry.shaftRange.end;
				const bool attachStart = atStart && !startContour.points.empty();
				const bool attachEnd = atEnd && !endContour.points.empty();
				if (attachStart)
					samples.front() = DecorationBackSample(samples.front(), evaluated.samples.front(), startContour, true);
				if (attachEnd)
					samples.back() = DecorationBackSample(samples.back(), evaluated.samples.back(), endContour, false);
				detail::AppendThickFlatPiece(mesh, samples, style, !attachStart, !attachEnd,
					hasGradientSamples ? &sampleColors : nullptr);
			}
			detail::AppendAttachedThickFlatDecoration(mesh, startContour, evaluated.samples.front(), true,
				style, detail::ThickFlatFaceOwner::StartDecoration, hasStartAttachment);
			detail::AppendAttachedThickFlatDecoration(mesh, endContour, evaluated.samples.back(), false,
				style, detail::ThickFlatFaceOwner::EndDecoration, hasEndAttachment);
			detail::FinalizeThickFlatMesh(mesh, style, geometry);
			return geometry;
		}
		detail::AppendDecoration(geometry, startContour, evaluated.samples.front(), true, style, geometry.startDecoration);
		detail::AppendDecoration(geometry, endContour, evaluated.samples.back(), false, style, geometry.endDecoration);
		if (geometry.shaftRange.IsEmpty())
		{
			if (startContour.trim + endContour.trim >= evaluated.totalLength)
				AddDiagnostic(geometry, PathDiagnosticCode::DecorationsExceedPathLength, "Endpoint decorations leave no shaft.");
			return geometry;
		}
		const std::vector<DashInterval> intervals = BuildDashIntervals(geometry.shaftRange.start, geometry.shaftRange.end, style.dash);
		geometry.dashedLength = DashCoverage(intervals);
		geometry.shaft.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
		const bool hasGradientSamples = style.gradient.enabled && !style.gradient.stops.empty();
		for (const DashInterval &interval : intervals)
		{
			std::vector<EvaluatedSample> samples;
			std::vector<glm::vec4> sampleColors;
			if (hasGradientSamples)
			{
				const std::vector<GradientPieceSample> gradientSamples = GradientSamples(evaluated, interval, style);
				samples.reserve(gradientSamples.size());
				sampleColors.reserve(gradientSamples.size());
				for (const GradientPieceSample &gradientSample : gradientSamples)
				{
					samples.push_back(gradientSample.sample);
					sampleColors.push_back(gradientSample.color);
				}
			}
			else
				samples = PieceSamples(evaluated, interval);
			const bool atStart = interval.start == geometry.shaftRange.start;
			const bool atEnd = interval.end == geometry.shaftRange.end;
			const bool startHandoff = atStart && NeedsDecorationHandoff(samples.front(), evaluated.samples.front(), startContour, true);
			const bool endHandoff = atEnd && NeedsDecorationHandoff(samples.back(), evaluated.samples.back(), endContour, false);
			if (startHandoff)
				samples.front() = DecorationBackSample(samples.front(), evaluated.samples.front(), startContour, true);
			if (endHandoff)
				samples.back() = DecorationBackSample(samples.back(), evaluated.samples.back(), endContour, false);
			if (detail::UsesTubeVertices(style))
				AppendTubePiece(geometry, samples, style, !(atStart && startContour.closesBack),
					!(atEnd && endContour.closesBack), startHandoff, hasGradientSamples ? &sampleColors : nullptr);
			else
				AppendRibbonPiece(geometry, samples, style, hasGradientSamples ? &sampleColors : nullptr);
		}
		geometry.shaft.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - geometry.shaft.firstIndex;
		return geometry;
	}
} // namespace DefectStudio
