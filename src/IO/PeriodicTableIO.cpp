#include "Core/dspch.hpp"

#include "IO/PeriodicTableIO.hpp"

#include <yaml-cpp/yaml.h>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{

	[[nodiscard]] StructuredError MakePeriodicTableError(
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
			"PeriodicTableIO",
			std::move(code)};
	}

	[[nodiscard]] bool ParseSymbolSequence(
		const YAML::Node &node,
		std::vector<std::string> &outSymbols)
	{
		if (!node || !node.IsSequence())
			return false;

		outSymbols.clear();
		for (const YAML::Node &entry : node)
		{
			std::string symbol;
			if (entry.IsMap())
				symbol = entry["symbol"].as<std::string>("");
			else if (entry.IsScalar())
				symbol = entry.as<std::string>("");

			if (!symbol.empty())
				outSymbols.push_back(std::move(symbol));
		}

		return !outSymbols.empty();
	}


	bool PeriodicTableIO::LoadFromFile(
		const Path &filePath,
		PeriodicTableData &outData,
		StructuredError &outError)
	{
		std::string yamlText;
		std::string error;
		if (!TextFileIO::Load(filePath, yamlText, error))
		{
			outError = MakePeriodicTableError(
				"periodic_table.load_failed",
				"Periodic table config could not be loaded.",
				"Path: " + filePath.String() + " | " + error,
				"Check periodic table YAML file availability.");
			return false;
		}

		try
		{
			YAML::Node root = YAML::Load(yamlText);
			if (!root || !root.IsMap())
			{
				outError = MakePeriodicTableError(
					"periodic_table.invalid_root",
					"Periodic table config could not be parsed.",
					"Periodic table YAML root is not a map: " + filePath.String(),
					"Check periodic table YAML syntax.");
				return false;
			}

			PeriodicTableData data;
			if (!ParseSymbolSequence(root["elements"], data.elements))
			{
				outError = MakePeriodicTableError(
					"periodic_table.missing_elements",
					"Periodic table config has no valid elements.",
					"Periodic table YAML has no valid 'elements' sequence: " + filePath.String(),
					"Add a non-empty elements sequence.");
				return false;
			}
			if (!ParseSymbolSequence(root["lanthanides"], data.lanthanides))
			{
				outError = MakePeriodicTableError(
					"periodic_table.missing_lanthanides",
					"Periodic table config has no valid lanthanides.",
					"Periodic table YAML has no valid 'lanthanides' sequence: " + filePath.String(),
					"Add a non-empty lanthanides sequence.");
				return false;
			}
			if (!ParseSymbolSequence(root["actinides"], data.actinides))
			{
				outError = MakePeriodicTableError(
					"periodic_table.missing_actinides",
					"Periodic table config has no valid actinides.",
					"Periodic table YAML has no valid 'actinides' sequence: " + filePath.String(),
					"Add a non-empty actinides sequence.");
				return false;
			}

			outData = std::move(data);
			return true;
		}
		catch (const std::exception &exception)
		{
			outError = MakePeriodicTableError(
				"periodic_table.parse_failed",
				"Periodic table config could not be parsed.",
				"Path: " + filePath.String() + " | " + exception.what(),
				"Check periodic table YAML syntax.");
			return false;
		}
	}
} // namespace DefectStudio
