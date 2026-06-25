#pragma once

#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	class PymatgenBridge;
	struct PymatgenStructureData;

	// Tymczasowy most do Pythona dla ladowania struktur krystalicznych.
	// T07: zastapic przez pelny Domain model + IOLayer pipeline.
	[[nodiscard]] RendererStructureData BuildRendererStructureFromPythonData(
		const PymatgenStructureData &structureData,
		const Path &filePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable);

	[[nodiscard]] Result<RendererStructureData> LoadRendererStructureViaPython(
		const Path &filePath,
		std::string name,
		PymatgenBridge &bridge,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable);
}
