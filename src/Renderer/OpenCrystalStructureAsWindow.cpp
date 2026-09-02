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
		bool showGrid)
	{
		// Same three-step sequence RendererRuntimeOpenCoordinator::onJobCompleted runs for
		// file-imported structures, made callable for in-app-built ones: RegenerateAutoBonds ->
		// Workspace().Structures().Add -> BuildRendererStructureData. sourcePath is empty - this
		// structure was built in-memory, not loaded from a file.
		RegenerateAutoBonds(structure, elementPropertiesTable);

		Ref<const StructureRecord> structureRecord =
			domainLayer.Workspace().Structures().Add(std::move(structure), Path{}, displayName);

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
			rendererLayer.AddWindow(std::move(window));
		}
		else
		{
			DS_LOG_ERROR("Open Crystal Structure: failed to build renderer window for '{}'", displayName);
		}
	}
} // namespace DefectStudio
