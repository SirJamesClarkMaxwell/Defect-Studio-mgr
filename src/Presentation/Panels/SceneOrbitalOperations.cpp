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
		constexpr glm::vec3 kSceneObjectCopyOffset(0.5f, 0.0f, 0.0f);

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
			orbital.centerA += kSceneObjectCopyOffset;
			orbital.centerB += kSceneObjectCopyOffset;
			orbital.anchorAtoms.clear();
		}

		void OffsetAndDetach(RendererWindowState::ScenePlane &plane)
		{
			plane.center += kSceneObjectCopyOffset;
			plane.anchorAtoms.clear();
		}

		template <typename Object>
		void CopySelectedObjects(
			const std::vector<Object> &objects,
			const std::vector<SceneObjectId> &selection,
			std::vector<Object> &clipboard)
		{
			clipboard.clear();
			for (const SceneObjectId id : selection)
			{
				const std::size_t index = FindObjectIndex(objects, id);
				if (index < objects.size())
					clipboard.push_back(objects[index]);
			}
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

	std::vector<RendererWindowState::SceneOrbital> &GetSceneOrbitalClipboard()
	{
		static std::vector<RendererWindowState::SceneOrbital> clipboard;
		return clipboard;
	}

	void CopySceneOrbitalsToClipboard(const RendererWindowState &windowState)
	{
		CopySelectedObjects(
			windowState.sceneOrbitals, windowState.selectedSceneOrbitals, GetSceneOrbitalClipboard());
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
		const std::vector<RendererWindowState::SceneOrbital> &clipboard = GetSceneOrbitalClipboard();
		if (clipboard.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.selectedSceneOrbitals = AppendCopies(
			windowState, clipboard, windowState.sceneOrbitals,
			[](RendererWindowState::SceneOrbital &orbital) { OffsetAndDetach(orbital); });
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
	}

	std::vector<RendererWindowState::ScenePlane> &GetScenePlaneClipboard()
	{
		static std::vector<RendererWindowState::ScenePlane> clipboard;
		return clipboard;
	}

	void CopyScenePlanesToClipboard(const RendererWindowState &windowState)
	{
		CopySelectedObjects(windowState.scenePlanes, windowState.selectedScenePlanes, GetScenePlaneClipboard());
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
		const std::vector<RendererWindowState::ScenePlane> &clipboard = GetScenePlaneClipboard();
		if (clipboard.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.selectedScenePlanes = AppendCopies(
			windowState, clipboard, windowState.scenePlanes,
			[](RendererWindowState::ScenePlane &plane) { OffsetAndDetach(plane); });
	}
} // namespace DefectStudio
