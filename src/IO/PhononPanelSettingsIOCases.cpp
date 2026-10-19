#include "Core/dspch.hpp"
#include "IO/PhononPanelSettingsIOCases.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include "IO/AnalysisDatasetIO.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		const std::vector<std::string> GlobalInputs = {"dispersionDir", "dispersionInPlane", "dispersionPoints"};
		const std::vector<std::string> CaseView = {"selectedMode", "plZpl", "irrepFilter", "minHuangRhys",
			"animationSource", "mixAngleDegrees", "ccdMotion", "ccdSweepQ", "ccdExtent"};

		YAML::Node Subset(const YAML::Node &map, const std::vector<std::string> &keys, bool include)
		{
			YAML::Node result(YAML::NodeType::Map);
			if (map && map.IsMap())
				for (const auto &entry : map)
				{
					const auto key = entry.first.as<std::string>();
					if ((std::find(keys.begin(), keys.end(), key) != keys.end()) == include)
						result[key] = YAML::Clone(entry.second);
				}
			return result;
		}

		YAML::Node CaseNode(const PhononCaseSettings &settings, const Path &projectDirectory)
		{
			PhononPanelSettings flat;
			static_cast<PhononCaseSettings &>(flat) = settings;
			const auto node = YAML::Load(PhononSettingsDetail::SerializeFlat(flat, projectDirectory));
			YAML::Node result(YAML::NodeType::Map);
			result["inputs"] = Subset(node["inputs"], GlobalInputs, false);
			result["resultDir"] = node["resultDir"];
			result["ccd"] = node["datasets"]["ccd"];
			result["view"] = Subset(node["view"], CaseView, true);
			return result;
		}

		bool HasCaseData(const PhononCaseSettings &settings, const Path &projectDirectory)
		{
			return YAML::Dump(CaseNode(settings, projectDirectory)) != YAML::Dump(CaseNode({}, projectDirectory));
		}

		std::string CaseName(const PhononCaseSettings &settings)
		{
			if (!settings.resultDirectory.Empty())
				if (ReadDatasetRef(settings.resultDirectory))
				{
					std::string text, error;
					if (TextFileIO::Load(settings.resultDirectory / "summary.json", text, error))
						try
						{
							const auto summary = nlohmann::json::parse(text);
							if (summary.contains("label") && summary["label"].is_string())
							{
								const auto label = summary["label"].get<std::string>();
								if (!label.empty()) return label;
							}
						}
						catch (const std::exception &) {}
				}
			if (!settings.groundStateDirectory.Empty())
				return settings.groundStateDirectory.parent_path().filename().Utf8() + "/" + settings.groundStateDirectory.filename().Utf8();
			return "Przypadek 1";
		}

		void Migrate(PhononPanelSettings &settings, const Path &projectDirectory)
		{
			if (!HasCaseData(settings, projectDirectory)) return;
			PhononCase item{"case-1", CaseName(settings), {}, settings};
			if (!settings.resultDirectory.Empty()) item.datasets.push_back(settings.resultDirectory);
			if (!settings.selectedCcdDirectory.Empty() &&
				std::find(item.datasets.begin(), item.datasets.end(), settings.selectedCcdDirectory) == item.datasets.end())
				item.datasets.push_back(settings.selectedCcdDirectory);
			settings.cases.push_back(std::move(item));
			settings.activeCaseId = "case-1";
			settings.nextCaseNumber = std::max(settings.nextCaseNumber, 2);
		}

		int CaseNumber(const std::string &id)
		{
			if (!id.starts_with("case-")) return 0;
			int number = 0;
			const auto parsed = std::from_chars(id.data() + 5, id.data() + id.size(), number);
			if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() || number < 1) return 0;
			if (number == std::numeric_limits<int>::max())
				throw std::invalid_argument("case id exhausts nextCaseNumber");
			return number;
		}

		bool ParseCase(const YAML::Node &node, const Path &projectDirectory, PhononCase &item,
			std::vector<std::string> &warnings, std::string &error)
		{
			YAML::Node flat(YAML::NodeType::Map);
			if (node["inputs"]) flat["inputs"] = node["inputs"];
			if (node["resultDir"]) flat["resultDir"] = node["resultDir"];
			if (node["view"]) flat["view"] = node["view"];
			if (node["ccd"]) flat["datasets"]["ccd"] = node["ccd"];
			if (node["datasets"]) flat["datasets"]["compared"] = node["datasets"];
			PhononPanelSettings parsed;
			if (!PhononSettingsDetail::ParseFlat(YAML::Dump(flat), projectDirectory, parsed, warnings, error)) return false;
			item.settings = parsed;
			item.datasets = std::move(parsed.comparedDirectories);
			return true;
		}
	}

	bool PhononPanelSettingsIO::Parse(const std::string &text, const Path &projectDirectory,
		PhononPanelSettings &outSettings, std::vector<std::string> &outWarnings, std::string &outError)
	{
		PhononPanelSettings settings;
		if (!PhononSettingsDetail::ParseFlat(text, projectDirectory, settings, outWarnings, outError))
		{
			outSettings = {};
			return false;
		}
		try
		{
			if (text.find_first_not_of(" \t\r\n") == std::string::npos)
			{
				outSettings = {};
				return true;
			}
			const auto root = YAML::Load(text);
			int version = root["cases"] ? 2 : 1;
			try { if (root["formatVersion"]) version = root["formatVersion"].as<int>(); }
			catch (const YAML::Exception &) {}
			if (version < 2)
				Migrate(settings, projectDirectory);
			else
			{
				const auto cases = root["cases"];
				if (cases && !cases.IsSequence())
					outWarnings.push_back("Ignored phonon setting cases: expected a sequence");
				else if (cases)
					for (const auto &node : cases)
					{
						try
						{
							PhononCase item;
							item.id = node["id"].as<std::string>();
							if (item.id.empty() || std::any_of(settings.cases.begin(), settings.cases.end(),
								[&](const auto &other) { return other.id == item.id; }))
								throw std::invalid_argument("empty or duplicate case id");
							item.name = node["name"] ? node["name"].as<std::string>() : item.id;
							std::vector<std::string> warnings;
							if (!ParseCase(node, projectDirectory, item, warnings, outError))
								throw std::invalid_argument(outError);
							outWarnings.insert(outWarnings.end(), warnings.begin(), warnings.end());
							settings.nextCaseNumber = std::max(settings.nextCaseNumber, CaseNumber(item.id) + 1);
							settings.cases.push_back(std::move(item));
						}
						catch (const std::exception &exception)
						{
							outWarnings.push_back("Ignored phonon case: " + std::string(exception.what()));
							outError.clear();
						}
					}
				try
				{
					if (root["nextCaseNumber"])
					{
						const int next = root["nextCaseNumber"].as<int>();
						if (next < settings.nextCaseNumber)
							throw std::invalid_argument("nextCaseNumber must exceed every existing case number");
						settings.nextCaseNumber = next;
					}
				}
				catch (const std::exception &exception)
				{
					outWarnings.push_back("Ignored phonon setting nextCaseNumber: " + std::string(exception.what()));
				}
				if (!settings.cases.empty())
				{
					std::string active;
					try { if (root["activeCase"]) active = root["activeCase"].as<std::string>(); }
					catch (const YAML::Exception &exception) { outWarnings.push_back(exception.what()); }
					auto found = std::find_if(settings.cases.begin(), settings.cases.end(), [&](const auto &item) { return item.id == active; });
					if (found == settings.cases.end()) found = settings.cases.begin();
					settings.activeCaseId = found->id;
					static_cast<PhononCaseSettings &>(settings) = found->settings;
				}
			}
			outSettings = std::move(settings);
			return true;
		}
		catch (const std::exception &exception)
		{
			outSettings = {};
			outError = exception.what();
			return false;
		}
	}

	std::string PhononPanelSettingsIO::Serialize(const PhononPanelSettings &settings, const Path &projectDirectory)
	{
		auto snapshot = settings;
		if (snapshot.cases.empty()) Migrate(snapshot, projectDirectory);
		for (const auto &item : snapshot.cases)
			snapshot.nextCaseNumber = std::max(snapshot.nextCaseNumber, CaseNumber(item.id) + 1);
		auto root = YAML::Load(PhononSettingsDetail::SerializeFlat(snapshot, projectDirectory));
		root["inputs"] = Subset(root["inputs"], GlobalInputs, true);
		root["view"] = Subset(root["view"], CaseView, false);
		root.remove("resultDir");
		root["datasets"].remove("ccd");
		root["activeCase"] = snapshot.activeCaseId;
		root["nextCaseNumber"] = snapshot.nextCaseNumber;
		root["cases"] = YAML::Node(YAML::NodeType::Sequence);
		for (const auto &item : snapshot.cases)
		{
			const auto &values = item.id == snapshot.activeCaseId ? static_cast<const PhononCaseSettings &>(snapshot) : item.settings;
			auto node = CaseNode(values, projectDirectory);
			node["id"] = item.id;
			node["name"] = item.name;
			PhononPanelSettings paths;
			paths.comparedDirectories = item.datasets;
			node["datasets"] = YAML::Load(PhononSettingsDetail::SerializeFlat(paths, projectDirectory))["datasets"]["compared"];
			root["cases"].push_back(node);
		}
		return YAML::Dump(root);
	}
}
