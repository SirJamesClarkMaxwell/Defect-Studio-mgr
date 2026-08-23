#pragma once

#include <string>
#include <vector>

#include "Core/Assets/AssetManager.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/RendererStartupDefinitions.hpp"

namespace DefectStudio
{
	struct RendererAssetBundle
	{
		AtomStyleTable atomStyleTable;
		// Resolved source path for atomStyleTable - kept so the Element Catalog editor (ElementCatalogPanel)
		// can save edits back to the same file without re-resolving the logical asset path itself.
		Path atomStylesPath;
		ElementPropertiesTable elementPropertiesTable;
		RendererStartupLayoutDefinition startupLayout;
		RendererPrimitiveMeshAssets primitiveMeshes;
		std::vector<std::string> periodicTableSymbols;
		std::vector<std::string> lanthanideSymbols;
		std::vector<std::string> actinideSymbols;
	};

	[[nodiscard]] Result<RendererAssetBundle> LoadRendererAssetBundle(const AssetManager &assetManager);
} // namespace DefectStudio
