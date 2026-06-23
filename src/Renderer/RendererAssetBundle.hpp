#pragma once

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
	};

	[[nodiscard]] Result<RendererAssetBundle> LoadRendererAssetBundle(const AssetManager &assetManager);
} // namespace DefectStudio
