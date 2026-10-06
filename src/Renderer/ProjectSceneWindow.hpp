#pragma once

#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class RendererLayer;

	// The one structure-free renderer window a project owns ("Project Scene",
	// docs/superpowers/specs/2026-09-30-project-scene-persistence-design.md). It is identified by
	// RendererWindowState::isProjectScene, never by a nil StructureId: an ad-hoc empty window opened
	// from the "+" button is also structure-free and must NOT be saved into the project.
	inline constexpr const char *kProjectSceneWindowId = "project-scene";
	inline constexpr const char *kProjectSceneTitle = "Project Scene";

	// The project-scene window, or null when the layer has none.
	[[nodiscard]] RendererWindowState *FindProjectSceneWindow(RendererLayer &rendererLayer);
	[[nodiscard]] const RendererWindowState *FindProjectSceneWindow(const RendererLayer &rendererLayer);

	// Makes the layer hold exactly one project-scene window whose scene objects are exactly
	// `objects`: creates it (windowId kProjectSceneWindowId, title kProjectSceneTitle, built through
	// the same bootstrap as OpenEmptyRendererWindow) when missing, otherwise reuses it after clearing
	// every scene object, selection and Edit Mode session the previous project left in it. Clears
	// `sceneObjectsDirty`. Per-object load problems are appended to `warnings`, same contract as
	// ApplyPersistedSceneObjects. Returns the window.
	RendererWindowState &ResetProjectSceneWindow(
		RendererLayer &rendererLayer, const std::vector<PersistedSceneObject> &objects,
		std::vector<StructuredError> &warnings);

	// The project-scene window's objects in persisted form; empty when there is no such window.
	// Never reads any other window - structure-backed and ad-hoc empty windows are not project scene.
	[[nodiscard]] std::vector<PersistedSceneObject> GatherProjectSceneObjects(const RendererLayer &rendererLayer);
	// Save entry point: ensure stable keys on the project-scene window before extraction.
	[[nodiscard]] std::vector<PersistedSceneObject> GatherProjectSceneObjects(RendererLayer &rendererLayer);
} // namespace DefectStudio
