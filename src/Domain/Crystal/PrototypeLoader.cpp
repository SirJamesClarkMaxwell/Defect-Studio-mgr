#include "Core/dspch.hpp"

#include "Domain/Crystal/PrototypeLoader.hpp"

#include <fstream>

#include <yaml-cpp/yaml.h>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	static PrototypeDefinition parsePrototype(const YAML::Node &node)
	{
		PrototypeDefinition proto;
		proto.name = node["name"].as<std::string>();
		proto.description = node["description"].as<std::string>("");
		proto.crystalSystem = node["crystal_system"].as<std::string>("");
		proto.centering = node["centering"].as<std::string>("");
		proto.bondLengthFormula = node["bond_length_formula"].as<std::string>("");

		if (node["sites"])
		{
			for (const auto &site : node["sites"])
			{
				SiteDefinition s;
				s.name = site["name"].as<std::string>();
				for (const auto &position : site["positions"])
				{
					const auto fractional = position.as<std::vector<float>>();
					if (fractional.size() >= 3)
						s.positions.emplace_back(fractional[0], fractional[1], fractional[2]);
				}
				proto.sites.push_back(std::move(s));
			}
		}

		return proto;
	}

	static MaterialDefinition parseMaterial(const YAML::Node &node)
	{
		MaterialDefinition mat;
		mat.name = node["name"].as<std::string>();
		mat.description = node["description"].as<std::string>("");

		if (node["polytypes"])
		{
			for (const auto &p : node["polytypes"])
			{
				mat.polytypes[p.first.as<std::string>()] = p.second.as<std::string>();
			}
		}

		if (node["constants"])
		{
			for (const auto &c : node["constants"])
			{
				std::string funcName = c.first.as<std::string>();
				LatticeConstantSet set;
				set.a = c.second["a"].as<float>(0.0f);
				set.c = c.second["c"].as<float>(0.0f);
				set.u = c.second["u"].as<float>(0.0f);
				mat.constants[funcName] = set;
			}
		}

		if (node["default_site_mapping"])
		{
			for (const auto &m : node["default_site_mapping"])
			{
				std::string polytype = m.first.as<std::string>();
				mat.defaultSiteMapping[polytype] = m.second.as<std::vector<std::string>>();
			}
		}

		return mat;
	}

	Result<PrototypesAndMaterials> PrototypeLoader::LoadBuiltIn()
	{
		const Path dataDirectory = Path::FromResolved(FileSystem::CurrentPath() / "install" / "users" / "default" / "data");

		Result<PrototypesAndMaterials> prototypes = LoadFromFile((dataDirectory / "prototypes.yaml").String());
		if (!prototypes)
			return prototypes.Error();

		// Materials live in their own file because they are user-editable while prototypes are not;
		// a missing materials.yaml leaves the prototype list usable rather than failing the load.
		Result<PrototypesAndMaterials> materials = LoadFromFile((dataDirectory / "materials.yaml").String());
		if (materials)
			prototypes->materials = std::move(materials->materials);

		return prototypes;
	}

	Result<PrototypesAndMaterials> PrototypeLoader::LoadFromFile(const std::string &path)
	{
		try
		{
			YAML::Node doc = YAML::LoadFile(path);
			PrototypesAndMaterials data;

			if (doc["prototypes"])
			{
				for (const auto &p : doc["prototypes"])
				{
					data.prototypes.push_back(parsePrototype(p));
				}
			}

			if (doc["materials"])
			{
				for (const auto &m : doc["materials"])
				{
					data.materials.push_back(parseMaterial(m));
				}
			}

			return data;
		}
		catch (const std::exception &e)
		{
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				std::string("Failed to load prototypes from ") + path,
				e.what(),
				"Check file format and path"
			);
		}
	}
}
