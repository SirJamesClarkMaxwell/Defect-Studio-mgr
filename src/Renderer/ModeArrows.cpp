#include "Core/dspch.hpp"
#include "Renderer/ModeArrows.hpp"

#include <algorithm>
#include <cmath>

namespace DefectStudio
{
	std::vector<ArrowSegment> BuildModeArrowSegments(const RendererStructureData &structure, const ModeArrowsState &state)
	{
		std::vector<ArrowSegment> segments;
		if (!state.visible)
			return segments;
		float longest = 0.0f;
		std::vector<float> lengths;
		for (const auto &arrow : state.arrows)
		{
			if (arrow.atomIndex >= structure.atoms.size())
				continue;
			const auto &atom = structure.atoms[arrow.atomIndex];
			const float length = glm::length(arrow.vectorAngstrom);
			if ((state.onlyForVisibleAtoms && !atom.visible) || !std::isfinite(length) || length <= 1e-4f)
				continue;
			segments.push_back({atom.cartesianPosition, atom.cartesianPosition + arrow.vectorAngstrom, glm::vec4(1.0f)});
			lengths.push_back(length);
			longest = std::max(longest, length);
		}
		for (std::size_t i = 0; i < segments.size(); ++i)
			segments[i].color = glm::vec4(glm::mix(state.lowColor, state.highColor, lengths[i] / longest), 1.0f);
		return segments;
	}
}
