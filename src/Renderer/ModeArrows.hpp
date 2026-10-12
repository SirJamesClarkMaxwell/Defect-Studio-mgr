#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	// task/104d: arrows of one phonon mode in a renderer window - plain vectors per atom, NOT a
	// structure comparison (review 2026-10-06 item 8). The Phonons panel fills it; the backend draws
	// it with the same shaft/cone meshes as the displacement-comparison arrows.
	struct ModeArrowInstance
	{
		std::size_t atomIndex = 0;
		glm::vec3 vectorAngstrom = glm::vec3(0.0f);
	};

	struct ModeArrowsState
	{
		std::vector<ModeArrowInstance> arrows;
		bool visible = true;
		// Skip arrows of hidden atoms (H / Alt+H), same rule as the comparison arrows.
		bool onlyForVisibleAtoms = true;
		glm::vec3 lowColor = glm::vec3(0.25f, 0.75f, 0.35f);
		glm::vec3 highColor = glm::vec3(0.85f, 0.2f, 0.2f);
	};

	struct ArrowSegment
	{
		glm::vec3 base = glm::vec3(0.0f);
		glm::vec3 tip = glm::vec3(0.0f);
		glm::vec4 color = glm::vec4(1.0f);
	};

	// One segment per drawable arrow: base = the atom's CURRENT cartesianPosition (so arrows ride
	// along with the mode animation), tip = base + vectorAngstrom. Colour is a linear ramp
	// lowColor -> highColor on |vector| / (largest |vector| among the drawn arrows), alpha 1.
	// Skipped: state not visible, atomIndex out of range, hidden atom when onlyForVisibleAtoms,
	// non-finite or |vector| <= 1e-4 Angstrom. Order follows `arrows`.
	[[nodiscard]] std::vector<ArrowSegment> BuildModeArrowSegments(
		const RendererStructureData &structure, const ModeArrowsState &state);
} // namespace DefectStudio
