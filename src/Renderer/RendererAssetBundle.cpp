#include "Core/dspch.hpp"

#include "Renderer/RendererAssetBundle.hpp"

#include "IO/AtomStyleIO.hpp"
#include "IO/ElementPropertiesIO.hpp"
#include "IO/PeriodicTableIO.hpp"
#include "IO/RendererMeshIO.hpp"
#include "IO/RendererStartupLayoutIO.hpp"
#include "Core/Logging/Logger.hpp"

namespace DefectStudio
{

	struct RendererAssetPaths
	{
		Path atomStylesPath;
		Path elementPropertiesPath;
		Path startupLayoutPath;
		Path sphereMeshPath;
		Path cylinderMeshPath;
		Path coneMeshPath;
	};

	[[nodiscard]] StructuredError MakeRendererAssetError(
		std::string code,
		std::string userMessage,
		std::string technicalDetails,
		std::string suggestion)
	{
		return StructuredError{
			ErrorCategory::IO,
			Severity::Error,
			std::move(userMessage),
			std::move(technicalDetails),
			std::move(suggestion),
			"RendererAssetBundle",
			std::move(code)};
	}

	[[nodiscard]] Result<Path> ResolveAssetPath(
		const AssetManager &assetManager,
		const char *logicalPath)
	{
		Result<ResolvedAsset> resolved = assetManager.ResolvePath(logicalPath);
		if (resolved.HasValue())
			return resolved.Value().resolvedPath;

		StructuredError error = resolved.Error();
		error.source = "RendererAssetBundle";
		return error;
	}

	[[nodiscard]] Result<RendererAssetPaths> ResolveRendererAssetPaths(const AssetManager &assetManager)
	{
		RendererAssetPaths paths;
		Result<Path> atomStylesPath = ResolveAssetPath(assetManager, "assets/renderer/atom_styles.yaml");
		if (!atomStylesPath.HasValue())
			return atomStylesPath.Error();
		paths.atomStylesPath = atomStylesPath.Value();

		Result<Path> elementPropertiesPath = ResolveAssetPath(assetManager, "data/elements/element_properties.yaml");
		if (!elementPropertiesPath.HasValue())
			return elementPropertiesPath.Error();
		paths.elementPropertiesPath = elementPropertiesPath.Value();

		Result<Path> startupLayoutPath = ResolveAssetPath(assetManager, "data/renderer/startup_layout.yaml");
		if (!startupLayoutPath.HasValue())
			return startupLayoutPath.Error();
		paths.startupLayoutPath = startupLayoutPath.Value();

		Result<Path> sphereMeshPath = ResolveAssetPath(assetManager, "assets/renderer/meshes/sphere.obj");
		if (!sphereMeshPath.HasValue())
			return sphereMeshPath.Error();
		paths.sphereMeshPath = sphereMeshPath.Value();

		Result<Path> cylinderMeshPath = ResolveAssetPath(assetManager, "assets/renderer/meshes/cylinder.obj");
		if (!cylinderMeshPath.HasValue())
			return cylinderMeshPath.Error();
		paths.cylinderMeshPath = cylinderMeshPath.Value();

		Result<Path> coneMeshPath = ResolveAssetPath(assetManager, "assets/renderer/meshes/cone.obj");
		if (!coneMeshPath.HasValue())
			return coneMeshPath.Error();
		paths.coneMeshPath = coneMeshPath.Value();

		return paths;
	}

	[[nodiscard]] Result<void> LoadAtomStyles(
		const Path &path,
		AtomStyleTable &outTable)
	{
		std::unordered_map<std::string, AtomRenderStyle> styles;
		VacancyRenderStyle vacancyStyle;
		std::string error;
		if (!AtomStyleIO::LoadFromFile(path, styles, vacancyStyle, error))
		{
			return MakeRendererAssetError(
				"renderer.assets.atom_styles.load_failed",
				"Renderer atom style data could not be loaded.",
				"Path: " + path.String() + " | " + error,
				"Check atom style YAML syntax and file availability.");
		}

		outTable.ReplaceStyles(std::move(styles), vacancyStyle);
		return {};
	}

	[[nodiscard]] Result<void> LoadElementProperties(
		const Path &path,
		ElementPropertiesTable &outTable)
	{
		std::unordered_map<std::string, ElementProperties> entries;
		std::string error;
		if (!ElementPropertiesIO::LoadFromFile(path, entries, error))
		{
			return MakeRendererAssetError(
				"renderer.assets.element_properties.load_failed",
				"Renderer element properties could not be loaded.",
				"Path: " + path.String() + " | " + error,
				"Check element properties YAML syntax and file availability.");
		}

		outTable.ReplaceData(std::move(entries));
		return {};
	}

	[[nodiscard]] Result<void> LoadStartupLayout(
		const Path &path,
		RendererStartupLayoutDefinition &outLayout)
	{
		std::string error;
		if (!RendererStartupLayoutIO::LoadFromFile(path, outLayout, error))
		{
			return MakeRendererAssetError(
				"renderer.assets.startup_layout.load_failed",
				"Renderer startup layout could not be loaded.",
				"Path: " + path.String() + " | " + error,
				"Check startup layout YAML and relative POSCAR paths.");
		}
		return {};
	}

	[[nodiscard]] Result<void> LoadPrimitiveMesh(
		const Path &path,
		RendererStaticMeshData &outMesh,
		bool generateGradientFromZ,
		const char *errorCode,
		const char *userMessage,
		const char *suggestion)
	{
		std::string error;
		if (!RendererMeshIO::LoadObjFromFile(path, outMesh, error, generateGradientFromZ))
		{
			return MakeRendererAssetError(
				errorCode,
				userMessage,
				"Path: " + path.String() + " | " + error,
				suggestion);
		}
		return {};
	}


	Result<RendererAssetBundle> LoadRendererAssetBundle(const AssetManager &assetManager)
	{
		Result<RendererAssetPaths> pathsResult = ResolveRendererAssetPaths(assetManager);
		if (!pathsResult.HasValue())
			return pathsResult.Error();

		const RendererAssetPaths &paths = pathsResult.Value();
		RendererAssetBundle bundle;

		Result<void> atomStylesResult = LoadAtomStyles(paths.atomStylesPath, bundle.atomStyleTable);
		if (!atomStylesResult.HasValue())
			return atomStylesResult.Error();
		bundle.atomStylesPath = paths.atomStylesPath;

		Result<void> elementPropertiesResult = LoadElementProperties(paths.elementPropertiesPath, bundle.elementPropertiesTable);
		if (!elementPropertiesResult.HasValue())
			return elementPropertiesResult.Error();

		Result<void> startupLayoutResult = LoadStartupLayout(paths.startupLayoutPath, bundle.startupLayout);
		if (!startupLayoutResult.HasValue())
			return startupLayoutResult.Error();

		Result<void> sphereMeshResult = LoadPrimitiveMesh(
			paths.sphereMeshPath,
			bundle.primitiveMeshes.sphere,
			false,
			"renderer.assets.mesh.sphere.load_failed",
			"Renderer sphere mesh asset could not be loaded.",
			"Check sphere OBJ syntax and availability.");
		if (!sphereMeshResult.HasValue())
			return sphereMeshResult.Error();

		Result<void> cylinderMeshResult = LoadPrimitiveMesh(
			paths.cylinderMeshPath,
			bundle.primitiveMeshes.cylinder,
			true,
			"renderer.assets.mesh.cylinder.load_failed",
			"Renderer cylinder mesh asset could not be loaded.",
			"Check cylinder OBJ syntax and availability.");
		if (!cylinderMeshResult.HasValue())
			return cylinderMeshResult.Error();

		Result<void> coneMeshResult = LoadPrimitiveMesh(
			paths.coneMeshPath,
			bundle.primitiveMeshes.cone,
			true,
			"renderer.assets.mesh.cone.load_failed",
			"Renderer cone mesh asset could not be loaded.",
			"Check cone OBJ syntax and availability.");
		if (!coneMeshResult.HasValue())
			return coneMeshResult.Error();

		Result<Path> periodicTablePath = ResolveAssetPath(assetManager, "assets/config/periodic_table.yaml");
		if (periodicTablePath)
		{
			PeriodicTableData periodicTableData;
			StructuredError periodicTableError;
			if (!PeriodicTableIO::LoadFromFile(periodicTablePath.Value(), periodicTableData, periodicTableError))
			{
				DS_LOG_WARN("PeriodicTableIO: {}", periodicTableError.technicalDetails);
			}
			else
			{
				bundle.periodicTableSymbols = std::move(periodicTableData.elements);
				bundle.lanthanideSymbols = std::move(periodicTableData.lanthanides);
				bundle.actinideSymbols = std::move(periodicTableData.actinides);
			}
		}

		return bundle;
	}
} // namespace DefectStudio
