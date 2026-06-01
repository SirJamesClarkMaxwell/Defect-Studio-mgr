#include "Core/dspch.hpp"

#include "IO/ElementPropertiesIO.hpp"

#include <yaml-cpp/yaml.h>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		bool parseElementEntries(
			const YAML::Node &elementsNode,
			std::unordered_map<std::string, ElementProperties> &outEntries)
		{
			outEntries.clear();
			for (const auto &entry : elementsNode)
			{
				if (!entry.first.IsScalar() || !entry.second.IsMap())
					continue;

				const std::string symbol = entry.first.as<std::string>("");
				if (symbol.empty())
					continue;

				const YAML::Node propertiesNode = entry.second;
				ElementProperties properties;
				properties.atomicNumber = propertiesNode["atomic_number"].as<int>(0);
				properties.mass = propertiesNode["mass"].as<float>(0.0f);
				properties.covalentRadius = propertiesNode["covalent_radius"].as<float>(0.77f);
				properties.vanDerWaalsRadius = propertiesNode["vdw_radius"].as<float>(1.50f);
				outEntries[symbol] = properties;
			}

			return !outEntries.empty();
		}
	} // namespace

	bool ElementPropertiesIO::LoadFromFile(
		const Path &path,
		std::unordered_map<std::string, ElementProperties> &outEntries,
		std::string &outError)
	{
		std::string text;
		if (!TextFileIO::Load(path, text, outError))
			return false;

		return ParseYaml(text, outEntries, outError);
	}

	bool ElementPropertiesIO::ParseYaml(
		std::string_view yamlText,
		std::unordered_map<std::string, ElementProperties> &outEntries,
		std::string &outError)
	{
		try
		{
			YAML::Node root = YAML::Load(std::string(yamlText));
			if (!root || !root.IsMap())
			{
				outError = "Element properties YAML root is not a map";
				return false;
			}

			const YAML::Node elementsNode = root["elements"];
			if (!elementsNode || !elementsNode.IsMap())
			{
				outError = "Element properties YAML has no 'elements' map";
				return false;
			}

			if (!parseElementEntries(elementsNode, outEntries))
			{
				outError = "Element properties YAML does not contain valid entries";
				return false;
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
