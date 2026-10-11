#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneObjectClipboard.hpp"

#include <limits>
#include "Core/Logging/Logger.hpp"

namespace DefectStudio
{
	bool SceneClipboardPastesIntoSource(const SceneClipboardObject &entry, const RendererWindowState &window)
	{
		if (!entry.sourceStructureId.empty())
			return entry.sourceStructureId == window.structure.domainStructureId;
		return entry.sourceWindowId.empty() || entry.sourceWindowId == window.windowId;
	}

	std::optional<PersistedAtomRef> SceneClipboardVacancyReference(
		const RendererStructureData &structure, const std::size_t index)
	{
		if (index >= structure.vacancies.size()) return std::nullopt;
		const auto &vacancy = structure.vacancies[index];
		return PersistedAtomRef{index, vacancy.sourceSpecies, vacancy.cartesianPosition};
	}

	namespace
	{
		std::optional<std::size_t> ResolveVacancy(const RendererStructureData &structure,
			const std::optional<PersistedAtomRef> &reference)
		{
			if (!reference) return std::nullopt;
			float nearest = kSceneAtomLinkToleranceAngstrom;
			std::optional<std::size_t> result;
			for (std::size_t index = 0; index < structure.vacancies.size(); ++index)
			{
				const auto &vacancy = structure.vacancies[index];
				const float distance = glm::distance(vacancy.cartesianPosition, reference->position);
				if (vacancy.sourceSpecies == reference->element && distance <= nearest)
				{
					result = index;
					nearest = distance;
					if (index == reference->index) return result;
				}
			}
			return result;
		}
	}

	std::vector<PersistedSceneObject> ResolveSceneClipboardObjects(
		const SceneObjectClipboard &clipboard, const RendererStructureData &structure)
	{
		std::vector<PersistedSceneObject> objects;
		for (const auto &entry : clipboard)
		{
			PersistedSceneObject object = entry.persisted;
			std::visit([](auto &value) { value.persistKey.clear(); }, object);
			if (std::holds_alternative<PersistedFreeLabel>(object))
			{
				auto &label = std::get<PersistedFreeLabel>(object);
				label.fnvKey.clear(); // a pasted charge marker is a free-standing copy, not a second FNV centre
				if (label.anchorVacancy)
				{
					const auto resolved = entry.vacancyRefs.empty() ? std::nullopt
						: ResolveVacancy(structure, entry.vacancyRefs.front());
					label.anchorVacancy = resolved && *resolved <= static_cast<std::size_t>(std::numeric_limits<int>::max())
						? std::optional<int>(static_cast<int>(*resolved)) : std::nullopt;
				}
			}
			else if (std::holds_alternative<PersistedScenePath>(object))
			{
				auto &path = std::get<PersistedScenePath>(object);
				for (std::size_t index = 0; index < path.nodes.size(); ++index)
					if (path.nodes[index].binding.kind == "CopyVacancy")
					{
						const auto resolved = index < entry.vacancyRefs.size()
							? ResolveVacancy(structure, entry.vacancyRefs[index]) : std::nullopt;
						// Retain CopyVacancy membership; the resolver reports a broken binding and uses the saved node.
						path.nodes[index].binding.vacancyIndex = resolved.value_or(std::numeric_limits<std::size_t>::max());
					}
			}
			objects.push_back(std::move(object));
		}
		return objects;
	}

	void ReportSceneClipboardWarnings(const std::vector<StructuredError> &warnings)
	{
		for (const auto &warning : warnings)
			DS_LOG_WARN("Wklejanie obiektu sceny: {}", warning.technicalDetails);
	}
} // namespace DefectStudio
