#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"

#include <algorithm>
#include <utility>

#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	std::optional<glm::vec3> ResolveFreeLabelAnchor(
		const RendererWindowState &window, const RendererWindowState::FreeLabel &label)
	{
		if (label.anchorAtom)
		{
			if (*label.anchorAtom < window.structure.atoms.size())
				return window.structure.atoms[*label.anchorAtom].cartesianPosition;
			return std::nullopt;
		}
		if (label.anchorVacancy && *label.anchorVacancy < window.structure.vacancies.size())
			return window.structure.vacancies[*label.anchorVacancy].cartesianPosition;
		return std::nullopt;
	}

	void RefreshAnchoredFreeLabels(RendererWindowState &window)
	{
		for (RendererWindowState::FreeLabel &label : window.freeLabels)
			if (const auto anchor = ResolveFreeLabelAnchor(window, label))
				label.worldPosition = *anchor + label.anchorOffset;
	}

	std::vector<SceneObjectId> AddVacancyLabels(
		RendererWindowState &window, const std::vector<std::size_t> &vacancies)
	{
		std::vector<SceneObjectId> added;
		for (std::size_t index = 0; index < window.structure.vacancies.size(); ++index)
		{
			if (!vacancies.empty() && std::find(vacancies.begin(), vacancies.end(), index) == vacancies.end())
				continue;
			if (std::any_of(window.freeLabels.begin(), window.freeLabels.end(), [index](const auto &label) {
					return label.anchorVacancy == index;
				}))
				continue;
			RendererWindowState::FreeLabel label;
			label.id = window.sceneRegistry.AllocateObjectId();
			label.text = window.structure.vacancies[index].label;
			label.anchorVacancy = index;
			label.worldPosition = window.structure.vacancies[index].cartesianPosition;
			added.push_back(label.id);
			window.freeLabels.push_back(std::move(label));
		}
		if (!added.empty())
			SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		return added;
	}
} // namespace DefectStudio
