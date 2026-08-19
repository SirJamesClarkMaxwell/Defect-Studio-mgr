#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

#include <array>
#include <optional>
#include <sstream>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::optional<float> ReadOptionalFloat(const nlohmann::json &payload, const char *key)
		{
			if (!payload.contains(key) || payload.at(key).is_null())
				return std::nullopt;
			return payload.at(key).get<float>();
		}

		[[nodiscard]] std::optional<std::array<bool, 3>> ReadSelectiveDynamics(const nlohmann::json &payload)
		{
			if (!payload.contains("selective_dynamics") || payload.at("selective_dynamics").is_null())
				return std::nullopt;

			const auto values = payload.at("selective_dynamics");
			if (!values.is_array() || values.size() != 3u)
				return std::nullopt;

			return std::array<bool, 3>{
				values.at(0).get<bool>(),
				values.at(1).get<bool>(),
				values.at(2).get<bool>()};
		}
	} // namespace

	PymatgenStructureData ParseStructurePayloadJson(const nlohmann::json &payload)
	{
		PymatgenStructureData result;
		result.reducedFormula = payload.value("reduced_formula", std::string{});

		const auto lattice = payload.at("lattice");
		for (int row = 0; row < 3; ++row)
		{
			const auto vector = lattice.at(row);
			result.lattice[row] = glm::vec3(
				vector.at(0).get<float>(),
				vector.at(1).get<float>(),
				vector.at(2).get<float>());
		}

		const auto sites = payload.at("sites");
		result.sites.reserve(sites.size());
		for (const auto &site : sites)
		{
			const auto fractional = site.at("fractional");
			const auto cartesian = site.at("cartesian");

			PymatgenStructureSite parsedSite;
			parsedSite.element = site.value("element", std::string{});
			parsedSite.fractionalPosition = glm::vec3(
				fractional.at(0).get<float>(),
				fractional.at(1).get<float>(),
				fractional.at(2).get<float>());
			parsedSite.cartesianPosition = glm::vec3(
				cartesian.at(0).get<float>(),
				cartesian.at(1).get<float>(),
				cartesian.at(2).get<float>());
			parsedSite.selectiveDynamics = ReadSelectiveDynamics(site);
			parsedSite.charge = ReadOptionalFloat(site, "charge");
			parsedSite.magmom = ReadOptionalFloat(site, "magmom");
			parsedSite.occupancy = site.value("occupancy", 1.0f);
			result.sites.push_back(std::move(parsedSite));
		}

		return result;
	}

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

	PythonExampleScript ResolvePythonExampleScript(const char *fileName)
	{
		const Path relativePath = Path("scripts") / "python" / "examples" / fileName;
		Path cursor = Path::FromResolved(FileSystem::CurrentPath());
		for (int depth = 0; depth < 10; ++depth)
		{
			const Path candidate = cursor / relativePath;
			if (FileSystem::Exists(candidate.Native()))
				return PythonExampleScript{candidate, cursor};

			const Path parent = cursor.parent_path();
			if (parent.Empty() || parent == cursor)
				break;
			cursor = parent;
		}

		return PythonExampleScript{relativePath, Path::FromResolved(FileSystem::CurrentPath())};
	}
} // namespace DefectStudio
