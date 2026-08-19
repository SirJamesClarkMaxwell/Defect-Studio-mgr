#pragma once

#include "Renderer/RendererTypes.hpp"
#include "Renderer/Scene/SceneRegistry.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	// The ECS<->flat-array boundary. RendererStructureData::atoms/bonds and
	// RendererWindowState::selectedAtomIndices stay the GPU-instanced-rendering hot path
	// (OpenGlRendererBackend reads them unchanged) - SceneSystem is the only place that syncs
	// ECS component state into them.
	namespace SceneSystem
	{
		// (Re)builds one entity per atom/bond from `structure`, destroying any entities from a
		// previous structure first. Call whenever a window's structure is (re)loaded.
		void SyncSceneWithStructure(SceneRegistry &scene, const RendererStructureData &structure);

		// Reads SelectionComponent/VisibilityComponent off every atom entity and writes the
		// result into windowState.selectedAtomIndices / windowState.structure.atoms[i].visible.
		// Every mutation path (click/box/circle-select, hide/show-all/view-modifiers) ends with
		// this call.
		void PushSelectionAndVisibilityToWindowState(const SceneRegistry &scene, RendererWindowState &windowState);
	} // namespace SceneSystem
} // namespace DefectStudio
