#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"

#include <algorithm>
#include <utility>

#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio
{
	bool IsFreeLabelVisible(const RendererWindowState::FreeLabel &label,
		const RendererStructureData &structure, std::span<const SceneHideVolume> hideVolumes)
	{
		if (!label.visible || PointCoveredByHideVolumes(hideVolumes, label.worldPosition, structure)) return false;
		if (label.anchorAtom && *label.anchorAtom < structure.atoms.size() &&
			!structure.atoms[*label.anchorAtom].visible) return false;
		if (label.anchorVacancy && *label.anchorVacancy < structure.vacancies.size())
		{
			const auto &vacancy = structure.vacancies[*label.anchorVacancy];
			if (vacancy.hidden || PointCoveredByHideVolumes(hideVolumes, vacancy.cartesianPosition, structure)) return false;
		}
		return true;
	}

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

	std::string ChargeMarkerText(const int charge)
	{
		return charge > 0 ? "+" + std::to_string(charge) : std::to_string(charge);
	}

	std::vector<RendererVacancyData> ChargeMarkerSpheres(std::span<const RendererWindowState::FreeLabel> labels,
		const RendererStructureData &structure, std::span<const SceneHideVolume> hideVolumes)
	{
		std::vector<RendererVacancyData> spheres;
		for (const auto &label : labels)
		{
			if (!label.charge || !IsFreeLabelVisible(label, structure, hideVolumes))
				continue;
			RendererVacancyData sphere;
			sphere.cartesianPosition = label.worldPosition;
			sphere.color = *label.charge > 0 ? glm::vec3(0.90f, 0.25f, 0.25f)
				: *label.charge < 0 ? glm::vec3(0.30f, 0.50f, 0.95f) : glm::vec3(0.6f);
			sphere.radius = 0.55f; // a bit larger than a vacancy, so one sitting on a vacancy stays visible
			spheres.push_back(sphere);
		}
		return spheres;
	}
} // namespace DefectStudio
