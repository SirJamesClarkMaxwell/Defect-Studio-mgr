#include "Core/dspch.hpp"

#include "Renderer/Path/PathTessellator.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace DefectStudio
{
	namespace
	{
		struct RawSample
		{
			PathSample sample;
			double t = 0.0;
		};

		[[nodiscard]] bool IsFinite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] bool IsFinite(const glm::dvec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		void AddDiagnostic(EvaluatedPath &path, PathDiagnosticCode code, PathElementId element, const char *message)
		{
			const auto found = std::find_if(path.diagnostics.begin(), path.diagnostics.end(), [code, element](const PathDiagnostic &diagnostic) {
				return diagnostic.code == code && diagnostic.element == element;
			});
			if (found == path.diagnostics.end())
				path.diagnostics.push_back({code, element, message});
		}

		[[nodiscard]] bool SettingsAreValid(const TessellationSettings &settings)
		{
			return std::isfinite(settings.worldTolerance) && settings.worldTolerance > 0.0 && settings.maxDepth >= 1 && settings.maxSamplesPerSegment >= 2;
		}

		class SegmentSubdivider
		{
		public:
			SegmentSubdivider(const ScenePath &path, const ResolvedNodes &resolved, std::size_t segment, const TessellationSettings &settings, EvaluatedPath &output)
				: m_Path(path), m_Resolved(resolved), m_Segment(segment), m_Settings(settings), m_Output(output)
			{
			}

			[[nodiscard]] bool Build(std::vector<RawSample> &samples)
			{
				const Result<PathSample> start = EvaluateSegment(m_Path, m_Resolved, m_Segment, 0.0);
				const Result<PathSample> end = EvaluateSegment(m_Path, m_Resolved, m_Segment, 1.0);
				if (!start || !end || !IsFinite(start ? start->position : glm::dvec3(0.0)) || !IsFinite(end ? end->position : glm::dvec3(0.0)))
				{
					AddDiagnostic(m_Output, std::holds_alternative<LineSegmentData>(m_Path.segments[m_Segment].data) ? PathDiagnosticCode::ZeroChord : PathDiagnosticCode::NonFinite,
						m_Path.segments[m_Segment].id, "A segment could not be evaluated for tessellation.");
					return false;
				}
				samples.push_back({start.Value(), 0.0});
				if (std::holds_alternative<LineSegmentData>(m_Path.segments[m_Segment].data))
				{
					samples.push_back({end.Value(), 1.0});
					return true;
				}
				return Subdivide(samples, samples.front(), {end.Value(), 1.0}, 0);
			}

		private:
			[[nodiscard]] bool Subdivide(std::vector<RawSample> &samples, const RawSample &start, const RawSample &end, int depth)
			{
				const double middleT = (start.t + end.t) * 0.5;
				const double firstQuarterT = (start.t + middleT) * 0.5;
				const double lastQuarterT = (middleT + end.t) * 0.5;
				const Result<PathSample> middleResult = EvaluateSegment(m_Path, m_Resolved, m_Segment, middleT);
				const Result<PathSample> firstQuarterResult = EvaluateSegment(m_Path, m_Resolved, m_Segment, firstQuarterT);
				const Result<PathSample> lastQuarterResult = EvaluateSegment(m_Path, m_Resolved, m_Segment, lastQuarterT);
				if (!middleResult || !firstQuarterResult || !lastQuarterResult || !IsFinite(middleResult->position) || !IsFinite(middleResult->tangent) ||
					!IsFinite(firstQuarterResult->position) || !IsFinite(lastQuarterResult->position))
					return Fail();
				const RawSample middle{middleResult.Value(), middleT};
				const glm::dvec3 chordMidpoint = (start.sample.position + end.sample.position) * 0.5;
				const double error = std::max({
					glm::distance(middle.sample.position, chordMidpoint),
					glm::distance(firstQuarterResult->position, glm::mix(start.sample.position, end.sample.position, 0.25)),
					glm::distance(lastQuarterResult->position, glm::mix(start.sample.position, end.sample.position, 0.75))});
				if (!std::isfinite(error))
					return Fail();
				if (error <= m_Settings.worldTolerance)
				{
					samples.push_back(end);
					return true;
				}
				if (depth >= m_Settings.maxDepth)
				{
					AddDiagnostic(m_Output, PathDiagnosticCode::TessellationDepthLimit, m_Path.segments[m_Segment].id, "Adaptive tessellation reached its depth limit.");
					samples.push_back(end);
					return true;
				}
				if (samples.size() + 1 >= static_cast<std::size_t>(m_Settings.maxSamplesPerSegment))
				{
					AddDiagnostic(m_Output, PathDiagnosticCode::TessellationSampleLimit, m_Path.segments[m_Segment].id, "Adaptive tessellation reached its sample limit.");
					samples.push_back(end);
					return true;
				}
				return Subdivide(samples, start, middle, depth + 1) && Subdivide(samples, middle, end, depth + 1);
			}

			[[nodiscard]] bool Fail()
			{
				AddDiagnostic(m_Output, PathDiagnosticCode::NonFinite, m_Path.segments[m_Segment].id, "A segment could not be evaluated for tessellation.");
				return false;
			}

			const ScenePath &m_Path;
			const ResolvedNodes &m_Resolved;
			std::size_t m_Segment;
			const TessellationSettings &m_Settings;
			EvaluatedPath &m_Output;
		};
	} // namespace

	EvaluatedPath Tessellate(const ScenePath &path, const ResolvedNodes &resolved, const TessellationSettings &settings)
	{
		EvaluatedPath output;
		if (!SettingsAreValid(settings))
		{
			AddDiagnostic(output, PathDiagnosticCode::InvalidTessellationSettings, {}, "Tessellation settings must be finite and within their valid ranges.");
			return output;
		}
		output.diagnostics = ValidatePath(path);
		if (!output.diagnostics.empty())
			return output;
		if (resolved.positions.size() != path.nodes.size())
		{
			AddDiagnostic(output, PathDiagnosticCode::NodeCountMismatch, {}, "Resolved node positions do not match the path nodes.");
			return output;
		}
		for (const glm::vec3 &position : resolved.positions)
			if (!IsFinite(position))
			{
				AddDiagnostic(output, PathDiagnosticCode::NonFinite, {}, "Resolved node position is not finite.");
				return output;
			}

		const std::vector<double> cumulative = CumulativeLengths(path, resolved);
		if (cumulative.empty())
		{
			AddDiagnostic(output, PathDiagnosticCode::NonFinite, {}, "Path lengths could not be evaluated.");
			return output;
		}
		output.totalLength = cumulative.back();
		std::vector<std::vector<RawSample>> bySegment;
		bySegment.reserve(path.segments.size());
		for (std::size_t segment = 0; segment < path.segments.size(); ++segment)
		{
			bySegment.emplace_back();
			SegmentSubdivider subdivider(path, resolved, segment, settings, output);
			if (!subdivider.Build(bySegment.back()))
			{
				output.samples.clear();
				return output;
			}
		}

		FrameSeed seed = settings.frameSeed;
		if (seed.mode == FrameSeed::Mode::Auto && !path.segments.empty())
			if (const auto *arc = std::get_if<CircularArcSegmentData>(&path.segments.front().data))
				seed.normal = glm::dvec3(arc->planeNormal);
		PathFrame frame;
		bool hasFrame = false;
		for (std::size_t segment = 0; segment < bySegment.size(); ++segment)
		{
			const std::vector<RawSample> &raw = bySegment[segment];
			double chordLength = 0.0;
			for (std::size_t index = 1; index < raw.size(); ++index)
				chordLength += glm::distance(raw[index - 1].sample.position, raw[index].sample.position);
			const double analyticLength = cumulative[segment + 1] - cumulative[segment];
			const double scale = chordLength > 0.0 ? analyticLength / chordLength : 0.0;
			double localChord = 0.0;
			for (std::size_t index = segment == 0 ? 0 : 1; index < raw.size(); ++index)
			{
				if (index > 0)
					localChord += glm::distance(raw[index - 1].sample.position, raw[index].sample.position);
				const glm::dvec3 tangent = raw[index].sample.tangent;
				frame = hasFrame ? TransportFrame(frame, tangent) : SeedFrame(tangent, seed);
				hasFrame = true;
				const double arcLength = index + 1 == raw.size() ? cumulative[segment + 1] : cumulative[segment] + localChord * scale;
				output.samples.push_back({raw[index].sample.position, frame.tangent, frame.normal, frame.binormal,
					path.segments[segment].id, raw[index].t, arcLength, 0.0});
			}
		}
		for (EvaluatedSample &sample : output.samples)
			sample.normalizedT = output.totalLength > 0.0 ? sample.arcLength / output.totalLength : 0.0;
		return output;
	}
} // namespace DefectStudio
