#include "Core/dspch.hpp"
#include "IO/PhononPanelSettingsIOCases.hpp"

#include <cmath>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace DefectStudio
{
	std::optional<SpectrumAxisUnit> ExperimentalSpectrumUnit(std::string_view unit)
	{
		if (unit == "eV") return SpectrumAxisUnit::ElectronVolt;
		return std::nullopt;
	}

	namespace PhononSettingsDetail
	{
		void ReadExperimental(const YAML::Node &node, const Path &project, PhononCase &item, std::vector<std::string> &warnings)
		{
			if (!node) return;
			if (!node.IsSequence()) { warnings.push_back("Ignored experimental spectra: expected a sequence"); return; }
			for (const auto &entry : node)
				try
				{
					ExperimentalSpectrumSource source;
					source.file = ReadPath(entry["file"], project);
					if (source.file.Empty()) throw std::invalid_argument("empty file path");
					if (entry["x"]) source.xColumn = entry["x"].as<int>();
					if (entry["y"]) source.yColumn = entry["y"].as<int>();
					if (source.xColumn < 0 || source.yColumn < 0) throw std::invalid_argument("negative column index");
					if (entry["unit"]) source.unit = entry["unit"].as<std::string>();
					if (!ExperimentalSpectrumUnit(source.unit)) throw std::invalid_argument("unknown unit: " + source.unit);
					source.label = entry["label"] ? entry["label"].as<std::string>() : source.file.filename().Utf8();
					if (entry["zpl"] && !entry["zpl"].IsNull())
					{
						source.zplEv = entry["zpl"].as<double>();
						if (!std::isfinite(*source.zplEv)) throw std::invalid_argument("non-finite ZPL");
					}
					if (entry["visible"]) source.visible = entry["visible"].as<bool>();
					item.experimentalSpectra.push_back(std::move(source));
				}
				catch (const std::exception &error) { warnings.push_back("Ignored experimental spectrum: " + std::string(error.what())); }
		}

		void WriteExperimental(YAML::Node &node, const Path &project, const PhononCase &item)
		{
			node["experimental"] = YAML::Node(YAML::NodeType::Sequence);
			for (const auto &source : item.experimentalSpectra)
			{
				YAML::Node entry(YAML::NodeType::Map);
				entry["file"] = StoredPath(source.file, project);
				entry["x"] = source.xColumn; entry["y"] = source.yColumn;
				entry["unit"] = source.unit; entry["label"] = source.label;
				if (source.zplEv) entry["zpl"] = *source.zplEv;
				entry["visible"] = source.visible;
				node["experimental"].push_back(entry);
			}
		}
	}
}
