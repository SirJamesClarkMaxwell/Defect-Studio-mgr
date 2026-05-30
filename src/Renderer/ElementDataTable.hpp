#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	struct ElementDisplayData
	{
		glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
		float radiusDisplay = 0.40f;
		float radiusCovalent = 0.77f;
	};

	class ElementDataTable
	{
	public:
		bool LoadFromDirectory(const std::string &elementDataDirectory)
		{
			m_Data.clear();

			const std::filesystem::path manifestPath = std::filesystem::path(elementDataDirectory) / "manifest.yaml";
			if (!std::filesystem::exists(manifestPath))
			{
				DS_LOG_WARN("ElementDataTable: manifest not found at {}", manifestPath.string());
				return false;
			}

			YAML::Node manifest;
			try
			{
				manifest = YAML::LoadFile(manifestPath.string());
			}
			catch (const std::exception &exception)
			{
				DS_LOG_ERROR("ElementDataTable: failed to parse manifest: {}", exception.what());
				return false;
			}

			const YAML::Node elements = manifest["elements"];
			if (!elements || !elements.IsSequence())
				return false;

			for (const YAML::Node &entry : elements)
			{
				const std::string symbol = entry["element"].as<std::string>("");
				const std::string file = entry["file"].as<std::string>("");
				if (symbol.empty() || file.empty())
					continue;

				const std::filesystem::path atomPath = std::filesystem::path(elementDataDirectory) / file;
				if (!std::filesystem::exists(atomPath))
					continue;

				YAML::Node atomYaml;
				try
				{
					atomYaml = YAML::LoadFile(atomPath.string());
				}
				catch (...)
				{
					continue;
				}

				const YAML::Node display = atomYaml["display"];
				if (!display)
					continue;

				ElementDisplayData data;
				if (display["color"] && display["color"].IsSequence() && display["color"].size() >= 3)
				{
					data.color = glm::vec3(
						display["color"][0].as<float>(0.7f),
						display["color"][1].as<float>(0.7f),
						display["color"][2].as<float>(0.7f));
				}
				data.radiusDisplay = display["radius_display"].as<float>(0.40f);
				data.radiusCovalent = display["radius_covalent"].as<float>(0.77f);
				m_Data[symbol] = data;
			}

			DS_LOG_INFO("ElementDataTable: loaded {} elements from {}", m_Data.size(), elementDataDirectory);
			return true;
		}

		[[nodiscard]] const ElementDisplayData &Get(const std::string &symbol) const
		{
			auto found = m_Data.find(symbol);
			if (found != m_Data.end())
				return found->second;
			return m_Fallback;
		}

		[[nodiscard]] glm::vec3 Color(const std::string &symbol) const
		{
			return Get(symbol).color;
		}

		[[nodiscard]] float DisplayRadius(const std::string &symbol) const
		{
			return Get(symbol).radiusDisplay;
		}

		[[nodiscard]] float CovalentRadius(const std::string &symbol) const
		{
			return Get(symbol).radiusCovalent;
		}

	private:
		std::unordered_map<std::string, ElementDisplayData> m_Data;
		ElementDisplayData m_Fallback;
	};
} // namespace DefectStudio
