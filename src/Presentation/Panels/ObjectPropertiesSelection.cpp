#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesSelection.hpp"

namespace DefectStudio
{
	bool ObjectPropertiesSections::Empty() const
	{
		return !atoms && !labels && !orbitals && !planes && !densities && !paths && !vacancies && !defectFrame;
	}

	ObjectPropertiesSections ResolveObjectPropertiesSections(const RendererWindowState &windowState)
	{
		ObjectPropertiesSections sections;
		sections.atoms = !windowState.selectedAtomIndices.empty();
		sections.labels = !windowState.selectedPinnedMeasurements.empty() || !windowState.selectedFreeLabels.empty();
		sections.orbitals = !windowState.selectedSceneOrbitals.empty();
		sections.planes = !windowState.selectedScenePlanes.empty();
		sections.densities = !windowState.selectedSceneDensities.empty();
		sections.paths = !windowState.selectedScenePaths.empty();
		sections.vacancies = !windowState.selectedVacancies.empty();
		sections.defectFrame = windowState.defectFrameSelected && windowState.structure.defectFrame.has_value();
		return sections;
	}
} // namespace DefectStudio
