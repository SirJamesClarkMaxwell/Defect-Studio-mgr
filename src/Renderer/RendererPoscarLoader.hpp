#pragma once

#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	// TODO(T07): Ten parser zostanie zastapiony przez Python bridge (PymatgenBridge).
	// Zostaw jako fallback gdy Python jest niedostepny.
	[[nodiscard]] Result<RendererStructureData> LoadRendererStructureFromPoscar(
		const Path &filePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable);
}
