#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

#include <sstream>

#include <nlohmann/json.hpp>

namespace DefectStudio
{
	std::string ExtractJsonLineFromOutput(const std::string &rawOutput)
	{
		std::istringstream input(rawOutput);
		std::string line;
		while (std::getline(input, line))
		{
			if (line.empty())
				continue;
			if (nlohmann::json::accept(line))
				return line;
		}

		return {};
	}
} // namespace DefectStudio
