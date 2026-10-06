#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	struct VacancyBondPair
	{
		bool vacancyPair = false;
		std::size_t first = 0; // atom for atom-vacancy, smaller vacancy for vacancy-vacancy
		std::size_t second = 0;

		friend bool operator==(const VacancyBondPair &, const VacancyBondPair &) = default;
	};

	// Atom-radii buffer: the tube's rim lies on the atom sphere, as in renderBonds.
	[[nodiscard]] inline float VacancyBondAtomBuffer(float atomRadius, float tubeRadius)
	{
		if (!std::isfinite(atomRadius) || atomRadius <= 0.0f ||
			!std::isfinite(tubeRadius) || tubeRadius < 0.0f)
			return 0.0f;
		const float ratio = tubeRadius / atomRadius;
		return std::sqrt(std::max(1.0f - ratio * ratio, 0.0f));
	}

	// Recognise the generated style and bindings, including tasks 69/74's buffered endpoints.
	// Names, saved widths and endpoint colours are authored data, not provenance.
	[[nodiscard]] inline std::optional<VacancyBondPair> GeneratedVacancyBondPair(const ScenePath &path)
	{
		const PathStrokeStyle &style = path.style;
		if (path.nodes.size() != 2 || path.segments.size() != 1 ||
			!std::holds_alternative<LineSegmentData>(path.segments.front().data) ||
			!std::holds_alternative<PathTransformBinding::Free>(path.transformBinding.value) ||
			style.profile != StrokeProfile::Round || style.cap != PathLineCap::Butt ||
			style.depthMode != PathDepthMode::DepthTest || style.alpha != 1.0f || style.dash.enabled ||
			style.startDecoration.kind != PathDecorationKind::None ||
			style.endDecoration.kind != PathDecorationKind::None ||
			!style.gradient.enabled || style.gradient.stops.size() != 2 ||
			style.gradient.stops.front().position != 0.0f || style.gradient.stops.back().position != 1.0f ||
			style.gradient.stops.front().alpha != 1.0f || style.gradient.stops.back().alpha != 1.0f)
			return std::nullopt;

		const auto vacancyEnd = [](const PathNode &node) -> std::optional<std::size_t> {
			if (!std::holds_alternative<PathBinding::CopyVacancy>(node.binding.value))
				return std::nullopt;
			const auto &binding = std::get<PathBinding::CopyVacancy>(node.binding.value);
			if (binding.offset != glm::vec3(0.0f) || (binding.buffer != 0.0f && binding.buffer != 1.0f))
				return std::nullopt;
			return binding.vacancyIndex;
		};
		const auto atomEnd = [](const PathNode &node) -> std::optional<std::size_t> {
			if (!std::holds_alternative<PathBinding::CopyPosition>(node.binding.value))
				return std::nullopt;
			const auto &binding = std::get<PathBinding::CopyPosition>(node.binding.value);
			// Surface buffers span [0, 1] as the tube radius changes; this includes legacy 0/0.9.
			if (binding.offset != glm::vec3(0.0f) || !std::isfinite(binding.buffer) ||
				binding.buffer < 0.0f || binding.buffer > 1.0f)
				return std::nullopt;
			return binding.atomIndex;
		};
		const auto firstVacancy = vacancyEnd(path.nodes.front());
		const auto secondVacancy = vacancyEnd(path.nodes.back());
		if (firstVacancy && secondVacancy && *firstVacancy != *secondVacancy)
			return VacancyBondPair{true, std::min(*firstVacancy, *secondVacancy), std::max(*firstVacancy, *secondVacancy)};
		if (const auto atom = atomEnd(path.nodes.front()); atom && secondVacancy)
			return VacancyBondPair{false, *atom, *secondVacancy};
		if (const auto atom = atomEnd(path.nodes.back()); atom && firstVacancy)
			return VacancyBondPair{false, *atom, *firstVacancy};
		return std::nullopt;
	}
}
