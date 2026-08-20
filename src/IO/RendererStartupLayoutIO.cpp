#include "Core/dspch.hpp"

#include "IO/RendererStartupLayoutIO.hpp"

#include <yaml-cpp/yaml.h>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] Path ResolvePath(const Path &baseDirectory, const std::string &rawPath)
		{
			const FilePath asPath(rawPath);
			if (asPath.is_absolute())
				return Path::FromResolved(asPath);
			return Path::FromResolved(baseDirectory.Native() / asPath);
		}
	} // namespace

	bool RendererStartupLayoutIO::LoadFromFile(
		const Path &path,
		RendererStartupLayoutDefinition &outLayout,
		std::string &outError)
	{
		std::string text;
		if (!TextFileIO::Load(path, text, outError))
			return false;

		const Path baseDirectory = path.parent_path();
		return ParseYaml(text, baseDirectory, outLayout, outError);
	}

	bool RendererStartupLayoutIO::ParseYaml(
		std::string_view yamlText,
		const Path &baseDirectory,
		RendererStartupLayoutDefinition &outLayout,
		std::string &outError)
	{
		try
		{
			YAML::Node root = YAML::Load(std::string(yamlText));
			if (!root || !root.IsMap())
			{
				outError = "Renderer startup layout YAML root is not a map";
				return false;
			}

			const YAML::Node windowsNode = root["windows"];
			if (!windowsNode || !windowsNode.IsSequence())
			{
				outError = "Renderer startup layout has no 'windows' sequence";
				return false;
			}

			outLayout.windows.clear();
			outLayout.windows.reserve(windowsNode.size());
			for (const YAML::Node &windowNode : windowsNode)
			{
				if (!windowNode || !windowNode.IsMap())
					continue;

				RendererStartupWindowDefinition definition;
				definition.title = windowNode["title"].as<std::string>("");
				definition.structureName = windowNode["structure_name"].as<std::string>("");
				const std::string poscarPath = windowNode["poscar_path"].as<std::string>("");

				if (definition.title.empty() || definition.structureName.empty() || poscarPath.empty())
					continue;

				const YAML::Node directionNode = windowNode["direction"];
				if (directionNode && directionNode.IsSequence() && directionNode.size() >= 3)
				{
					definition.direction.x = directionNode[0].as<float>(definition.direction.x);
					definition.direction.y = directionNode[1].as<float>(definition.direction.y);
					definition.direction.z = directionNode[2].as<float>(definition.direction.z);
				}
				definition.distanceMultiplier = windowNode["distance_multiplier"].as<float>(definition.distanceMultiplier);
				definition.hideStep = windowNode["hide_step"].as<std::size_t>(definition.hideStep);
				definition.poscarPath = ResolvePath(baseDirectory, poscarPath);

				outLayout.windows.push_back(std::move(definition));
			}

			return true;
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}
} // namespace DefectStudio
