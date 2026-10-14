#pragma once

#include <optional>
#include <string>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class DomainLayer;
	class RendererLayer;
	enum class CellSelectionOperation { Repeat, Mirror };

	[[nodiscard]] std::optional<CellSelectionOperation> DrawCellSelectionOperationButtons(bool menu = false);
	void DrawCellSelectionOperationPopups(const std::string &windowId, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commands, const WeakRef<DomainLayer> &domain,
		std::optional<CellSelectionOperation> requested = std::nullopt);
} // namespace DefectStudio
