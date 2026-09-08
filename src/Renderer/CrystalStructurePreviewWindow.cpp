#include "Core/dspch.hpp"

#include "Renderer/CrystalStructurePreviewWindow.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/BondGenerator.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] RendererWindowState *FindWindow(RendererLayer &rendererLayer, const std::string &windowId)
		{
			if (windowId.empty())
				return nullptr;
			std::vector<RendererWindowState> &windows = rendererLayer.GetWindows();
			const auto it = std::find_if(
				windows.begin(),
				windows.end(),
				[&](const RendererWindowState &window) { return window.windowId == windowId; });
			return it != windows.end() ? &*it : nullptr;
		}
	} // namespace

	std::string ShowCrystalStructurePreview(
		const std::string &existingWindowId,
		const CrystalStructure &structure,
		const std::string &displayName,
		RendererLayer &rendererLayer,
		const ElementPropertiesTable &elementPropertiesTable,
		const AtomStyleTable &atomStyleTable,
		bool showCellBox,
		bool showGrid,
		const std::optional<glm::mat3> &overlayCellVectors,
		const std::string &sessionId)
	{
		CrystalStructure bonded = structure;
		RegenerateAutoBonds(bonded, elementPropertiesTable);

		// Empty domainStructureId on purpose: nothing about this structure lives in the domain, and
		// ResolveAtomEditTarget rejects a window without one rather than editing a phantom record.
		RendererStructureData structureData =
			BuildRendererStructureData(bonded, Path{}, displayName, atomStyleTable, std::string{});
		if (overlayCellVectors.has_value())
			structureData.overlayCellEdges = BuildCellEdges(*overlayCellVectors);

		if (RendererWindowState *existing = FindWindow(rendererLayer, existingWindowId))
		{
			// Refresh in place, keeping the camera. Re-framing on every keystroke would make the
			// preview jump around while the user is still typing the number that caused it.
			existing->title = displayName;
			existing->structure = std::move(structureData);
			existing->showCellBox = showCellBox;
			existing->showGrid = showGrid;
			existing->sessionId = sessionId;
			SceneSystem::SyncSceneWithStructure(existing->sceneRegistry, existing->structure);
			SceneSystem::PushSelectionAndVisibilityToWindowState(existing->sceneRegistry, *existing);
			return existingWindowId;
		}

		RendererStartupWindowInput input;
		input.definition.title = displayName;
		input.definition.structureName = displayName;
		input.definition.poscarPath = Path{};
		input.structure = std::move(structureData);

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));
		if (windows.empty())
		{
			DS_LOG_ERROR("Structure preview: failed to build renderer window for '{}'", displayName);
			return {};
		}

		RendererWindowState window = std::move(windows.front());
		window.showCellBox = showCellBox;
		window.showGrid = showGrid;
		window.sessionId = sessionId;
		rendererLayer.AddWindow(std::move(window));

		// AddWindow rewrites windowId on a collision with an already-open window, so the id is read
		// back from the window it actually created rather than from the one handed to it.
		const std::vector<RendererWindowState> &openWindows = rendererLayer.GetWindows();
		return openWindows.empty() ? std::string{} : openWindows.back().windowId;
	}

	void CloseCrystalStructurePreview(const std::string &windowId, RendererLayer &rendererLayer)
	{
		if (windowId.empty())
			return;
		rendererLayer.RemoveWindow(windowId);
	}
} // namespace DefectStudio
