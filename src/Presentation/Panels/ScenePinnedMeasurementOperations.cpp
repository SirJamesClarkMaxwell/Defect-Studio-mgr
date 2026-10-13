#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneObjectEditActions.hpp"

namespace DefectStudio
{
	SceneObjectClipboard &GetScenePinnedMeasurementClipboard()
	{
		static SceneObjectClipboard clipboard;
		return clipboard;
	}

	void CopyScenePinnedMeasurementsToClipboard(const RendererWindowState &window)
	{
		CopySceneObjectClipboard(window, &RendererWindowState::pinnedMeasurements,
			window.selectedPinnedMeasurements, GetScenePinnedMeasurementClipboard());
	}

	void PasteScenePinnedMeasurementsFromClipboard(RendererWindowState &window)
	{
		PasteSceneObjectClipboard(window, GetScenePinnedMeasurementClipboard(),
			&RendererWindowState::pinnedMeasurements, window.selectedPinnedMeasurements);
	}

	void DuplicateSelectedScenePinnedMeasurements(RendererWindowState &window, const glm::vec3 &offset)
	{
		SceneObjectClipboard copies;
		CopySceneObjectClipboard(window, &RendererWindowState::pinnedMeasurements,
			window.selectedPinnedMeasurements, copies);
		PasteSceneObjectClipboard(window, copies, &RendererWindowState::pinnedMeasurements,
			window.selectedPinnedMeasurements, offset);
	}

	void EraseScenePinnedMeasurements(RendererWindowState &window, const std::vector<SceneObjectId> &ids)
	{
		std::erase_if(window.pinnedMeasurements, [&ids](const auto &pin) {
			return std::find(ids.begin(), ids.end(), pin.id) != ids.end();
		});
		window.selectedPinnedMeasurements.clear();
		window.pinnedMeasurementDragging = false;
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
	}
} // namespace DefectStudio
