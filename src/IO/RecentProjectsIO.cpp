#include "Core/dspch.hpp"

#include "IO/RecentProjectsIO.hpp"

#include <algorithm>
#include <chrono>

#include <yaml-cpp/yaml.h>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::int64_t NowEpochSeconds()
		{
			return std::chrono::duration_cast<std::chrono::seconds>(Time::Now().time_since_epoch()).count();
		}
	} // namespace

	Path RecentProjectsIO::DefaultFilePath()
	{
		return Path::FromResolved(
			FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "recent_projects.yaml");
	}

	bool RecentProjectsIO::Load(const Path &filePath, std::vector<RecentProjectEntry> &outEntries, std::string &outError)
	{
		std::string text;
		if (!TextFileIO::Load(filePath, text, outError))
		{
			outEntries.clear();
			return false;
		}

		try
		{
			const YAML::Node root = YAML::Load(text);
			outEntries.clear();
			if (!root || !root["projects"] || !root["projects"].IsSequence())
				return true;

			for (const YAML::Node &node : root["projects"])
			{
				if (!node || !node.IsMap())
					continue;
				RecentProjectEntry entry;
				entry.projectDirectory = Path(node["path"].as<std::string>(""));
				entry.lastOpenedAt = node["last_opened_at"].as<std::int64_t>(0);
				if (entry.projectDirectory.Empty())
					continue;
				outEntries.push_back(std::move(entry));
			}
			return true;
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	bool RecentProjectsIO::Save(const Path &filePath, const std::vector<RecentProjectEntry> &entries, std::string &outError)
	{
		try
		{
			YAML::Emitter emit;
			emit << YAML::BeginMap;
			emit << YAML::Key << "projects" << YAML::Value << YAML::BeginSeq;
			for (const RecentProjectEntry &entry : entries)
			{
				emit << YAML::BeginMap;
				emit << YAML::Key << "path" << YAML::Value << entry.projectDirectory.String();
				emit << YAML::Key << "last_opened_at" << YAML::Value << entry.lastOpenedAt;
				emit << YAML::EndMap;
			}
			emit << YAML::EndSeq;
			emit << YAML::EndMap;
			return TextFileIO::Save(filePath, emit.c_str(), outError);
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			return false;
		}
	}

	void RecentProjectsIO::Touch(std::vector<RecentProjectEntry> &entries, const Path &projectDirectory, std::size_t maxEntries)
	{
		const std::string key = projectDirectory.String();
		std::erase_if(entries, [&key](const RecentProjectEntry &entry) { return entry.projectDirectory.String() == key; });

		RecentProjectEntry entry;
		entry.projectDirectory = projectDirectory;
		entry.lastOpenedAt = NowEpochSeconds();
		entries.insert(entries.begin(), std::move(entry));

		if (entries.size() > maxEntries)
			entries.resize(maxEntries);
	}
} // namespace DefectStudio
