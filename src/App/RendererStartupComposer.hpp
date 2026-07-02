#pragma once

#include <vector>

#include "App/ApplicationState.hpp"
#include "Renderer/RendererAssetBundle.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class AssetManager;
	class DomainLayer;
	class ScientificRuntimeLayer;

	struct RendererStartupComposition
	{
		RendererAssetBundle assets;
		std::vector<RendererWindowState> windows;
	};

	[[nodiscard]] RendererStartupComposition ComposeRendererStartup(
		AssetManager &assetManager,
		ScientificRuntimeLayer *scientificRuntimeLayer,
		DomainLayer *domainLayer);

	[[nodiscard]] RendererStartupConfig BuildRendererStartupConfig(
		const ApplicationConfig &config,
		RendererStartupComposition composition);
} // namespace DefectStudio
