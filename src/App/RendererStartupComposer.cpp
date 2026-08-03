#include "Core/dspch.hpp"

#include "App/RendererStartupComposer.hpp"

#include "Core/Assets/AssetManager.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/BondGenerator.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/DomainIds.hpp"
#include "Renderer/RendererAssetBundle.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"
#include "ScientificRuntime/ScientificRuntimeLayer.hpp"

namespace DefectStudio
{
	RendererStartupComposition ComposeRendererStartup(
		AssetManager &assetManager,
		ScientificRuntimeLayer *scientificRuntimeLayer,
		DomainLayer *domainLayer)
	{
		RendererStartupComposition composition;
		Result<RendererAssetBundle> assetsResult = LoadRendererAssetBundle(assetManager);
		if (!assetsResult.HasValue())
		{
			const StructuredError &error = assetsResult.Error();
			DS_LOG_ERROR(
				"Renderer startup assets failed to load [{}]: {}",
				error.code.empty() ? "unknown" : error.code,
				error.technicalDetails);
			return composition;
		}

		composition.assets = std::move(assetsResult).Value();
		if (scientificRuntimeLayer == nullptr || !scientificRuntimeLayer->IsPythonBridgeAvailable())
		{
			DS_LOG_ERROR("Renderer startup scene skipped: Scientific Python runtime is unavailable");
			return composition;
		}

		std::vector<RendererStartupWindowInput> startupWindowInputs;
		startupWindowInputs.reserve(composition.assets.startupLayout.windows.size());
		for (const RendererStartupWindowDefinition &definition : composition.assets.startupLayout.windows)
		{
			Result<CrystalStructure> loadedStructure = scientificRuntimeLayer->LoadCrystalStructure(definition.poscarPath);
			if (!loadedStructure.HasValue())
			{
				const StructuredError &error = loadedStructure.Error();
				DS_LOG_ERROR(
					"Renderer startup structure '{}' failed to load via Python [{}]: {}",
					definition.structureName,
					error.code.empty() ? "unknown" : error.code,
					error.technicalDetails);
				continue;
			}

			CrystalStructure domainStructureValue = std::move(loadedStructure).Value();
			RegenerateAutoBonds(domainStructureValue, composition.assets.elementPropertiesTable);

			const StructureRecord *structureRecord = nullptr;
			if (domainLayer != nullptr)
			{
				structureRecord = &domainLayer->Workspace().Structures().Add(
					std::move(domainStructureValue),
					definition.poscarPath,
					definition.structureName);
			}

			const CrystalStructure &domainStructure = structureRecord != nullptr
				? structureRecord->structure
				: domainStructureValue;

			RendererStartupWindowInput input;
			input.definition = definition;
			input.structure = BuildRendererStructureData(
				domainStructure,
				definition.poscarPath,
				definition.structureName,
				composition.assets.atomStyleTable,
				structureRecord != nullptr ? ToString(structureRecord->id) : std::string{});
			startupWindowInputs.push_back(std::move(input));
		}

		composition.windows = BuildRendererStartupWindows(std::move(startupWindowInputs));
		return composition;
	}

	RendererStartupConfig BuildRendererStartupConfig(
		const ApplicationConfig &config,
		RendererStartupComposition composition)
	{
		RendererStartupConfig startupConfig;
		startupConfig.assetsDirectory = config.paths.assetsDirectory;
		startupConfig.shaderDirectory = Path::FromResolved(
			FileSystem::CurrentPath() / "src" / "Renderer" / "OpenGl" / "Shaders");
		startupConfig.startupWindows = std::move(composition.windows);
		startupConfig.primitiveMeshes = std::move(composition.assets.primitiveMeshes);
		startupConfig.periodicTableSymbols = std::move(composition.assets.periodicTableSymbols);
		startupConfig.lanthanideSymbols = std::move(composition.assets.lanthanideSymbols);
		startupConfig.actinideSymbols = std::move(composition.assets.actinideSymbols);
		startupConfig.loadDefaultScene = !startupConfig.startupWindows.empty();
		return startupConfig;
	}
} // namespace DefectStudio
