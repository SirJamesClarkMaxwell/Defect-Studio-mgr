#pragma once

#include <string>
#include <vector>

#include "Core/Assets/AssetManager.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/RendererStartupDefinitions.hpp"

namespace DefectStudio
{
	struct RendererAssetBundle
	{
		AtomStyleTable atomStyleTable;
		ElementPropertiesTable elementPropertiesTable;
		RendererStartupLayoutDefinition startupLayout;
		RendererPrimitiveMeshAssets primitiveMeshes;
		std::vector<std::string> periodicTableSymbols;
		std::vector<std::string> lanthanideSymbols;
		std::vector<std::string> actinideSymbols;
	};

	[[nodiscard]] Result<RendererAssetBundle> LoadRendererAssetBundle(const AssetManager &assetManager);
} // namespace DefectStudio
