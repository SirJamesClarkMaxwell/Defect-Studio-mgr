#include "Core/dspch.hpp"

#include "IO/ProjectRootsIO.hpp"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include "Core/Utils/Path.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] Path LastProjectRootTxtPath(const Path &manifestFilePath)
		{
			return manifestFilePath.parent_path() / "last_project_root.txt";
		}

		// Same dev-machine default ProjectTreePanel used before this file existed - preserved so a
		// fresh install (no project_roots.yaml, no last_project_root.txt) behaves identically.
		[[nodiscard]] Path HardcodedFreshInstallDefault()
		{
			return Path("O:\\hBN\\V2CBCN");
		}

		[[nodiscard]] bool ParseYaml(const std::string &text, std::vector<ProjectRootEntry> &outRoots, std::string &outError)
		{
			try
			{
				const YAML::Node root = YAML::Load(text);
				if (!root || !root["roots"] || !root["roots"].IsSequence())
				{
					outError = "project_roots.yaml has no 'roots' sequence";
					return false;
				}

				outRoots.clear();
				for (const YAML::Node &node : root["roots"])
				{
					if (!node || !node.IsMap())
						continue;
					ProjectRootEntry entry;
					entry.id = node["id"].as<std::string>("");
					entry.path = Path(node["path"].as<std::string>(""));
					entry.label = node["label"].as<std::string>("");
					if (entry.id.empty() || entry.path.Empty())
						continue;
					outRoots.push_back(std::move(entry));
				}
				return true;
			}
			catch (const std::exception &exception)
			{
				outError = exception.what();
				return false;
			}
		}

		[[nodiscard]] bool SerializeYaml(const std::vector<ProjectRootEntry> &roots, std::string &outText, std::string &outError)
		{
			try
			{
				YAML::Emitter emit;
				emit << YAML::BeginMap;
				emit << YAML::Key << "roots" << YAML::Value << YAML::BeginSeq;
				for (const ProjectRootEntry &entry : roots)
				{
					emit << YAML::BeginMap;
					emit << YAML::Key << "id" << YAML::Value << entry.id;
					emit << YAML::Key << "path" << YAML::Value << entry.path.String();
					emit << YAML::Key << "label" << YAML::Value << entry.label;
					emit << YAML::EndMap;
				}
				emit << YAML::EndSeq;
				emit << YAML::EndMap;
				outText = emit.c_str();
				return true;
			}
			catch (const std::exception &exception)
			{
				outError = exception.what();
				return false;
			}
		}
	} // namespace

	Path ProjectRootsIO::DefaultFilePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "project_roots.yaml");
	}

	std::string ProjectRootsIO::GenerateRootId()
	{
		std::random_device randomDevice;
		std::uniform_int_distribution<std::uint32_t> distribution(0, 255);
		std::array<std::uint8_t, 8> bytes{};
		for (std::uint8_t &byte : bytes)
			byte = static_cast<std::uint8_t>(distribution(randomDevice));

		std::ostringstream stream;
		stream << "root-" << std::hex << std::setfill('0');
		for (const std::uint8_t byte : bytes)
			stream << std::setw(2) << static_cast<int>(byte);
		return stream.str();
	}

	bool ProjectRootsIO::Load(const Path &filePath, std::vector<ProjectRootEntry> &outRoots, std::string &outError)
	{
		std::string text;
		std::string loadError;
		if (TextFileIO::Load(filePath, text, loadError))
			return ParseYaml(text, outRoots, outError);

		// File doesn't exist yet - one-time migration from whatever placeholder predates this
		// file, then persist the result so this fallback never runs again.
		outRoots.clear();

		std::string legacyText;
		std::string legacyError;
		if (TextFileIO::Load(LastProjectRootTxtPath(filePath), legacyText, legacyError))
		{
			while (!legacyText.empty() && (legacyText.back() == '\n' || legacyText.back() == '\r'))
				legacyText.pop_back();
			const Path legacyRoot(legacyText);
			if (!legacyRoot.Empty() && FileSystem::Exists(legacyRoot.Native()))
			{
				ProjectRootEntry entry;
				entry.id = GenerateRootId();
				entry.path = legacyRoot;
				entry.label = legacyRoot.filename().String();
				outRoots.push_back(std::move(entry));
			}
		}

		if (outRoots.empty())
		{
			const Path fallback = HardcodedFreshInstallDefault();
			if (FileSystem::Exists(fallback.Native()))
			{
				ProjectRootEntry entry;
				entry.id = GenerateRootId();
				entry.path = fallback;
				entry.label = "default";
				outRoots.push_back(std::move(entry));
			}
		}

		std::string saveError;
		if (!Save(filePath, outRoots, saveError))
			outError = "project_roots.yaml migration seed could not be saved: " + saveError;
		return true;
	}

	bool ProjectRootsIO::Save(const Path &filePath, const std::vector<ProjectRootEntry> &roots, std::string &outError)
	{
		std::string text;
		if (!SerializeYaml(roots, text, outError))
			return false;
		return TextFileIO::Save(filePath, text, outError);
	}
} // namespace DefectStudio
