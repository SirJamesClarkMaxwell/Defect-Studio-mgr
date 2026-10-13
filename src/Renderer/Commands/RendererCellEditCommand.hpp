#pragma once

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class RendererLayer;

	inline constexpr const char *kReplaceStructurePayloadKey = "cell_edit.replace_structure_payload";
	inline constexpr const char *kReplaceStructureCommandId = "renderer.cell.replace_structure";

	struct ReplaceStructurePayload
	{
		std::string windowId;
		CrystalStructure structure;
		std::string description = "Edit cell";
		bool changesSiteIndices = false;
	};

	[[nodiscard]] Unique<ICommand> CreateReplaceStructureCommand(WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer, AtomStyleTable styles,
		ElementPropertiesTable elements, ReplaceStructurePayload payload);
} // namespace DefectStudio
