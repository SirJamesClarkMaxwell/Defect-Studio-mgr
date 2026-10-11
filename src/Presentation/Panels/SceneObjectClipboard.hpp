#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneSelection.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	struct SceneClipboardObject
	{
		PersistedSceneObject persisted;
		// Older persisted types omit the outliner's two visibility columns.
		bool visible = true;
		bool renderable = true;
		// Vacancy indices are window-local; keep species/position beside the persisted form.
		std::vector<std::optional<PersistedAtomRef>> vacancyRefs;
		std::string sourceStructureId;
		std::string sourceWindowId;
	};
	using SceneObjectClipboard = std::vector<SceneClipboardObject>;

	[[nodiscard]] std::optional<PersistedAtomRef> SceneClipboardVacancyReference(
		const RendererStructureData &structure, std::size_t index);
	[[nodiscard]] std::vector<PersistedSceneObject> ResolveSceneClipboardObjects(
		const SceneObjectClipboard &clipboard, const RendererStructureData &structure);
	void ReportSceneClipboardWarnings(const std::vector<StructuredError> &warnings);
	[[nodiscard]] bool SceneClipboardPastesIntoSource(
		const SceneClipboardObject &entry, const RendererWindowState &window);

	template <typename Object>
	void OffsetSceneClipboardCopy(Object &copy)
	{
		const glm::vec3 offset(0.5f, 0.0f, 0.0f);
		if constexpr (std::is_same_v<Object, RendererWindowState::FreeLabel>)
		{
			copy.worldPosition += offset;
			copy.anchorOffset += offset;
		}
		else if constexpr (std::is_same_v<Object, RendererWindowState::PinnedMeasurement>)
			copy.worldOffset += offset;
		else if constexpr (std::is_same_v<Object, RendererWindowState::ScenePlane>)
		{
			copy.center += offset;
			copy.anchorAtoms.clear();
		}
		else if constexpr (std::is_same_v<Object, RendererWindowState::SceneOrbital>)
		{
			copy.centerA += offset;
			copy.centerB += offset;
			copy.anchorAtoms.clear();
			for (auto &component : copy.lcaoComponents)
			{
				component.center += offset;
				component.anchorAtom = std::numeric_limits<std::size_t>::max();
			}
		}
	}

	// Use the project's file conversion and atom resolver without replacing the target's objects.
	template <typename Object>
	void CopySceneObjectClipboard(const RendererWindowState &window,
		std::vector<Object> RendererWindowState::*member, const std::vector<SceneObjectId> &selection,
		SceneObjectClipboard &clipboard)
	{
		RendererWindowState source;
		source.structure = window.structure;
		for (const SceneObjectId id : selection)
			for (const Object &object : window.*member)
				if (object.id == id) (source.*member).push_back(object);
		if constexpr (std::is_same_v<Object, RendererWindowState::FreeLabel>)
			RefreshAnchoredFreeLabels(source);
		else if constexpr (std::is_same_v<Object, RendererWindowState::SceneOrbital>)
			ResolveAnchoredOrbitals(source);
		else if constexpr (std::is_same_v<Object, RendererWindowState::ScenePlane>)
			ResolveAnchoredScenePlanes(source);
		const auto persisted = ExtractPersistedSceneObjects(static_cast<const RendererWindowState &>(source));
		clipboard.clear();
		for (std::size_t index = 0; index < persisted.size(); ++index)
		{
			const Object &object = (source.*member)[index];
			SceneClipboardObject entry{persisted[index], object.visible, object.renderable, {}};
			entry.sourceStructureId = window.structure.domainStructureId;
			entry.sourceWindowId = window.windowId;
			if constexpr (std::is_same_v<Object, RendererWindowState::FreeLabel>)
				if (object.anchorVacancy)
					entry.vacancyRefs.push_back(SceneClipboardVacancyReference(source.structure, *object.anchorVacancy));
			clipboard.push_back(std::move(entry));
		}
	}

	template <typename Object>
	void PasteSceneObjectClipboard(RendererWindowState &window, const SceneObjectClipboard &clipboard,
		std::vector<Object> RendererWindowState::*member, std::vector<SceneObjectId> &selection)
	{
		if (clipboard.empty()) return;
		RendererWindowState restored;
		restored.structure = window.structure;
		std::vector<StructuredError> warnings;
		const auto objects = ResolveSceneClipboardObjects(clipboard, window.structure);
		bool inserted = false;
		for (std::size_t index = 0; index < objects.size(); ++index)
		{
			ApplyPersistedSceneObjects(restored, {objects[index]}, warnings);
			if ((restored.*member).empty()) continue;
			if (!inserted)
			{
				PushPinnedMeasurementUndoSnapshot(window);
				ClearAllSceneSelection(window);
				inserted = true;
			}
			Object copy = std::move((restored.*member).front());
			copy.id = window.sceneRegistry.AllocateObjectId();
			copy.persistKey = GenerateScenePersistKey();
			copy.visible = clipboard[index].visible;
			copy.renderable = clipboard[index].renderable;
			if (SceneClipboardPastesIntoSource(clipboard[index], window)) OffsetSceneClipboardCopy(copy);
			selection.push_back(copy.id);
			(window.*member).push_back(std::move(copy));
		}
		ReportSceneClipboardWarnings(warnings);
		if (!inserted) return;
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		SceneSystem::SyncLabelSelection(window.sceneRegistry, window);
	}
} // namespace DefectStudio
