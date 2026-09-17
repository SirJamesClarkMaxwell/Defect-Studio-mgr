#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesSelection.hpp"

namespace DefectStudio
{
	bool ObjectPropertiesSections::Empty() const
	{
		return !atoms && !labels && !arrows && !orbitals && !planes;
	}

	ObjectPropertiesSections ResolveObjectPropertiesSections(const RendererWindowState &windowState)
	{
		ObjectPropertiesSections sections;
		sections.atoms = !windowState.selectedAtomIndices.empty();
		sections.labels = !windowState.selectedPinnedMeasurements.empty() || !windowState.selectedFreeLabels.empty();
		sections.arrows = !windowState.selectedSceneArrows.empty();
		sections.orbitals = !windowState.selectedSceneOrbitals.empty();
		sections.planes = !windowState.selectedScenePlanes.empty();
		return sections;
	}
} // namespace DefectStudio
