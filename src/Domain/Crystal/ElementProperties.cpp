#include "Core/dspch.hpp"

#include "Domain/Crystal/ElementProperties.hpp"

#include <filesystem>

#include <yaml-cpp/yaml.h>

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	bool ElementPropertiesTable::LoadFromFile(const std::string &path)
	{
		m_Data.clear();

		if (path.empty())
		{
			DS_LOG_WARN("ElementPropertiesTable: empty path");
			return false;
		}

		const std::filesystem::path propertiesPath(path);
		if (!std::filesystem::exists(propertiesPath))
		{
			DS_LOG_WARN("ElementPropertiesTable: file not found [{}]", propertiesPath.string());
			return false;
		}

		YAML::Node root;
		try
		{
			root = YAML::LoadFile(propertiesPath.string());
		}
		catch (const std::exception &exception)
		{
			DS_LOG_ERROR(
				"ElementPropertiesTable: failed to parse [{}]: {}",
				propertiesPath.string(),
				exception.what());
			return false;
		}

		const YAML::Node elementsNode = root["elements"];
		if (!elementsNode || !elementsNode.IsMap())
		{
			DS_LOG_ERROR(
				"ElementPropertiesTable: invalid yaml format in [{}], expected map node 'elements'",
				propertiesPath.string());
			return false;
		}

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
			properties.covalentRadius = propertiesNode["covalent_radius"].as<float>(m_Fallback.covalentRadius);
			properties.vanDerWaalsRadius = propertiesNode["vdw_radius"].as<float>(m_Fallback.vanDerWaalsRadius);
			m_Data[symbol] = properties;
		}

		DS_LOG_INFO(
			"ElementPropertiesTable: loaded {} entries from {}",
			m_Data.size(),
			propertiesPath.string());
		return !m_Data.empty();
	}

	const ElementProperties &ElementPropertiesTable::Get(const std::string &symbol) const
	{
		const auto found = m_Data.find(symbol);
		if (found != m_Data.end())
			return found->second;
		return m_Fallback;
	}

	float ElementPropertiesTable::CovalentRadius(const std::string &symbol) const
	{
		return Get(symbol).covalentRadius;
	}
} // namespace DefectStudio
