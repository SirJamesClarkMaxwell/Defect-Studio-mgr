#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename Object>
		[[nodiscard]] std::size_t FindObjectIndex(
			const std::vector<Object> &objects, const SceneObjectId id)
		{
			const auto found = std::find_if(
				objects.begin(), objects.end(),
				[id](const Object &object) { return object.id == id; });
			return found == objects.end()
				? objects.size()
				: static_cast<std::size_t>(std::distance(objects.begin(), found));
		}

		void OffsetAndDetach(RendererWindowState::SceneOrbital &orbital)
		{
			OffsetSceneClipboardCopy(orbital);
		}

		void OffsetAndDetach(RendererWindowState::ScenePlane &plane)
		{
			OffsetSceneClipboardCopy(plane);
		}

		template <typename Object, typename Offset>
		[[nodiscard]] std::vector<SceneObjectId> AppendCopies(
			RendererWindowState &windowState,
			const std::vector<Object> &source,
			std::vector<Object> &destination,
			Offset offset)
		{
			std::vector<SceneObjectId> newIds;
			newIds.reserve(source.size());
			for (const Object &object : source)
			{
				Object copy = object;
				copy.id = windowState.sceneRegistry.AllocateObjectId();
				copy.persistKey.clear();
				offset(copy);
				newIds.push_back(copy.id);
				destination.push_back(std::move(copy));
			}
			return newIds;
		}

		template <typename Object, typename Offset>
		[[nodiscard]] std::vector<SceneObjectId> DuplicateSelectedObjects(
			RendererWindowState &windowState,
			const std::vector<Object> &objects,
			const std::vector<SceneObjectId> &selection,
			std::vector<Object> &destination,
			Offset offset)
		{
			std::vector<Object> source;
			for (const SceneObjectId id : selection)
			{
				const std::size_t index = FindObjectIndex(objects, id);
				if (index < objects.size())
					source.push_back(objects[index]);
			}
			return AppendCopies(windowState, source, destination, offset);
		}
	} // namespace

	SceneObjectClipboard &GetSceneOrbitalClipboard()
	{
		static SceneObjectClipboard clipboard;
		return clipboard;
	}

	SceneObjectId AppendSceneOrbital(RendererWindowState &windowState, RendererWindowState::SceneOrbital orbital)
	{
		orbital.id = windowState.sceneRegistry.AllocateObjectId();
		windowState.sceneOrbitals.push_back(std::move(orbital));
		return windowState.sceneOrbitals.back().id;
	}

	void CopySceneOrbitalsToClipboard(const RendererWindowState &windowState)
	{
		CopySceneObjectClipboard(windowState, &RendererWindowState::sceneOrbitals,
			windowState.selectedSceneOrbitals, GetSceneOrbitalClipboard());
	}

	void DuplicateSelectedSceneOrbitals(RendererWindowState &windowState)
	{
		if (windowState.selectedSceneOrbitals.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.selectedSceneOrbitals = DuplicateSelectedObjects(
			windowState, windowState.sceneOrbitals, windowState.selectedSceneOrbitals,
			windowState.sceneOrbitals,
			[](RendererWindowState::SceneOrbital &orbital) { OffsetAndDetach(orbital); });
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
	}

	void PasteSceneOrbitalsFromClipboard(RendererWindowState &windowState)
	{
		PasteSceneObjectClipboard(windowState, GetSceneOrbitalClipboard(),
			&RendererWindowState::sceneOrbitals, windowState.selectedSceneOrbitals);
	}

	SceneObjectClipboard &GetScenePlaneClipboard()
	{
		static SceneObjectClipboard clipboard;
		return clipboard;
	}

	void CopyScenePlanesToClipboard(const RendererWindowState &windowState)
	{
		CopySceneObjectClipboard(windowState, &RendererWindowState::scenePlanes,
			windowState.selectedScenePlanes, GetScenePlaneClipboard());
	}

	void DuplicateSelectedScenePlanes(RendererWindowState &windowState)
	{
		if (windowState.selectedScenePlanes.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.selectedScenePlanes = DuplicateSelectedObjects(
			windowState, windowState.scenePlanes, windowState.selectedScenePlanes,
			windowState.scenePlanes,
			[](RendererWindowState::ScenePlane &plane) { OffsetAndDetach(plane); });
	}

	void PasteScenePlanesFromClipboard(RendererWindowState &windowState)
	{
		PasteSceneObjectClipboard(windowState, GetScenePlaneClipboard(),
			&RendererWindowState::scenePlanes, windowState.selectedScenePlanes);
	}
} // namespace DefectStudio
