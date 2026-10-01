#include "Core/dspch.hpp"

#include "Renderer/ProjectSceneWindow.hpp"

#include <algorithm>

#include "Renderer/OpenCrystalStructureAsWindow.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	RendererWindowState *FindProjectSceneWindow(RendererLayer &rendererLayer)
	{
		auto &windows = rendererLayer.GetWindows();
		const auto found = std::find_if(windows.begin(), windows.end(),
			[](const RendererWindowState &window) { return window.isProjectScene; });
		return found == windows.end() ? nullptr : &*found;
	}

	const RendererWindowState *FindProjectSceneWindow(const RendererLayer &rendererLayer)
	{
		const auto &windows = rendererLayer.GetWindows();
		const auto found = std::find_if(windows.begin(), windows.end(),
			[](const RendererWindowState &window) { return window.isProjectScene; });
		return found == windows.end() ? nullptr : &*found;
	}

	RendererWindowState &ResetProjectSceneWindow(
		RendererLayer &rendererLayer, const std::vector<PersistedSceneObject> &objects,
		std::vector<StructuredError> &warnings)
	{
		// Undo records resolve the stable window ID; they must not revive another project's scene.
		rendererLayer.ClearUndoHistory();
		if (FindProjectSceneWindow(rendererLayer) == nullptr)
		{
			OpenEmptyRendererWindow(rendererLayer, kProjectSceneTitle);
			auto &window = rendererLayer.GetWindows().back();
			window.windowId = kProjectSceneWindowId;
			window.isProjectScene = true;
		}
		RendererWindowState &window = *FindProjectSceneWindow(rendererLayer);
		window.pathEdit.Leave();
		window.pathHandleTypeMenuRequested = false;
		window.selectedAtomIndices.clear();
		window.selectedBondIndices.clear();
		window.modalTransform.reset();
		window.modalTransformStartRequested = false;
		window.modalTransformSelection = {};
		window.modalTransformSceneObjectsBefore.reset();
		window.modalTransformStartedFromHandle = false;
		window.scenePathStyleEditBefore.reset();
		window.gizmoDragActive = false;
		window.pinnedMeasurementDragging = false;
		window.freeLabelDragging = false;
		window.sceneArrowDragging = false;
		window.sceneArrowQuickEditActive = false;
		window.selectionDragActive = false;
		ApplyPersistedSceneObjects(window, objects, warnings);
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		window.sceneObjectsDirty = false;
		return window;
	}

	std::vector<PersistedSceneObject> GatherProjectSceneObjects(const RendererLayer &rendererLayer)
	{
		if (const auto *window = FindProjectSceneWindow(rendererLayer))
			return ExtractPersistedSceneObjects(*window);
		return {};
	}
} // namespace DefectStudio
