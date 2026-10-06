#include "Core/dspch.hpp"

#include "Renderer/OpenCrystalStructureAsWindow.hpp"

#include <utility>
#include <vector>

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/Crystal/BondGenerator.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio
{
	void OpenCrystalStructureAsWindow(
		CrystalStructure structure,
		const std::string &displayName,
		DomainLayer &domainLayer,
		RendererLayer &rendererLayer,
		const ElementPropertiesTable &elementPropertiesTable,
		const AtomStyleTable &atomStyleTable,
		bool showCellBox,
		bool showGrid,
		bool exportPotcar)
	{
		// Same three-step sequence RendererRuntimeOpenCoordinator::onJobCompleted runs for
		// file-imported structures, made callable for in-app-built ones: RegenerateAutoBonds ->
		// Workspace().Structures().Add -> BuildRendererStructureData. sourcePath is empty - this
		// structure was built in-memory, not loaded from a file.
		RegenerateAutoBonds(structure, elementPropertiesTable);

		Ref<const StructureRecord> structureRecord =
			domainLayer.Workspace().Structures().Add(std::move(structure), Path{}, displayName);

		if (exportPotcar)
		{
			if (auto record = domainLayer.Workspace().Structures().FindMutable(structureRecord->id).lock())
				record->exportPotcar = true;
		}

		RendererStartupWindowInput input;
		input.definition.title = displayName;
		input.definition.structureName = displayName;
		input.definition.poscarPath = Path{};
		input.structure = BuildRendererStructureData(
			structureRecord->structure,
			Path{},
			displayName,
			atomStyleTable,
			ToString(structureRecord->id));

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));
		if (!windows.empty())
		{
			RendererWindowState window = std::move(windows.front());
			window.showCellBox = showCellBox;
			window.showGrid = showGrid;
			window.structureId = structureRecord->id;
			rendererLayer.AddWindow(std::move(window));
		}
		else
		{
			DS_LOG_ERROR("Open Crystal Structure: failed to build renderer window for '{}'", displayName);
		}
	}

	void OpenEmptyRendererWindow(RendererLayer &rendererLayer, const std::string &title)
	{
		// Same bootstrap as every other open path, just with a default-constructed
		// RendererStructureData: no atoms, no bonds, no domain structure to build it from.
		RendererStartupWindowInput input;
		input.definition.title = title;
		input.definition.structureName = title;
		input.definition.poscarPath = Path{};

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));
		if (windows.empty())
		{
			DS_LOG_ERROR("Open Empty Window: failed to build renderer window '{}'", title);
			return;
		}
		rendererLayer.AddWindow(std::move(windows.front()));
	}

	void OpenRegisteredStructureAsWindow(
		StructureId id,
		DomainLayer &domainLayer,
		RendererLayer &rendererLayer,
		const AtomStyleTable &atomStyleTable)
	{
		Ref<const StructureRecord> structureRecord = domainLayer.Workspace().Structures().Find(id).lock();
		if (structureRecord == nullptr)
		{
			DS_LOG_ERROR("Open Registered Structure: structure id {} not found in registry", ToString(id));
			return;
		}

		RendererStartupWindowInput input;
		input.definition.title = structureRecord->displayName;
		input.definition.structureName = structureRecord->displayName;
		input.definition.poscarPath = structureRecord->sourcePath;
		input.structure = BuildRendererStructureData(
			structureRecord->structure,
			structureRecord->sourcePath,
			structureRecord->displayName,
			atomStyleTable,
			ToString(id));

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));
		if (!windows.empty())
		{
			RendererWindowState window = std::move(windows.front());
			window.structureId = id;
			rendererLayer.AddWindow(std::move(window));
		}
		else
		{
			DS_LOG_ERROR("Open Registered Structure: failed to build renderer window for id {}", ToString(id));
		}
	}
} // namespace DefectStudio
