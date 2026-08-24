#include "Core/dspch.hpp"

#include "IO/ProjectManifestIO.hpp"

#include <chrono>

#include <yaml-cpp/yaml.h>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::int64_t NowEpochSeconds()
		{
			return std::chrono::duration_cast<std::chrono::seconds>(Time::Now().time_since_epoch()).count();
		}

		[[nodiscard]] bool ParseYaml(const std::string &text, ProjectManifest &outManifest, std::string &outError)
		{
			try
			{
				const YAML::Node root = YAML::Load(text);
				if (!root || !root.IsMap())
				{
					outError = "manifest.yaml root is not a map";
					return false;
				}

				outManifest.uuid = root["uuid"].as<std::string>("");
				outManifest.name = root["name"].as<std::string>("");
				outManifest.createdAt = root["created_at"].as<std::int64_t>(0);
				outManifest.lastModified = root["last_modified"].as<std::int64_t>(0);
				outManifest.formatVersion = root["format_version"].as<int>(1);
				outManifest.bulkDirectory = Path(root["bulk_directory"].as<std::string>(""));

				outManifest.irrepLabelOverrides.clear();
				const YAML::Node irrepLabelsNode = root["irrep_labels"];
				if (irrepLabelsNode && irrepLabelsNode.IsSequence())
				{
					for (const YAML::Node &node : irrepLabelsNode)
					{
						if (!node || !node.IsMap())
							continue;
						const std::string irrep = node["irrep"].as<std::string>("");
						const std::string label = node["label"].as<std::string>("");
						if (irrep.empty())
							continue;
						outManifest.irrepLabelOverrides.emplace_back(irrep, label);
					}
				}

				outManifest.roots.clear();
				const YAML::Node rootsNode = root["roots"];
				if (rootsNode && rootsNode.IsSequence())
				{
					for (const YAML::Node &node : rootsNode)
					{
						if (!node || !node.IsMap())
							continue;
						ProjectRootEntry entry;
						entry.id = node["id"].as<std::string>("");
						entry.path = Path(node["path"].as<std::string>(""));
						entry.label = node["label"].as<std::string>("");
						if (entry.id.empty() || entry.path.Empty())
							continue;
						outManifest.roots.push_back(std::move(entry));
					}
				}
				return true;
			}
			catch (const std::exception &exception)
			{
				outError = exception.what();
				return false;
			}
		}

		[[nodiscard]] bool SerializeYaml(const ProjectManifest &manifest, std::string &outText, std::string &outError)
		{
			try
			{
				YAML::Emitter emit;
				emit << YAML::BeginMap;
				emit << YAML::Key << "uuid" << YAML::Value << manifest.uuid;
				emit << YAML::Key << "name" << YAML::Value << manifest.name;
				emit << YAML::Key << "created_at" << YAML::Value << manifest.createdAt;
				emit << YAML::Key << "last_modified" << YAML::Value << manifest.lastModified;
				emit << YAML::Key << "format_version" << YAML::Value << manifest.formatVersion;
				emit << YAML::Key << "bulk_directory" << YAML::Value << manifest.bulkDirectory.String();
				emit << YAML::Key << "roots" << YAML::Value << YAML::BeginSeq;
				for (const ProjectRootEntry &entry : manifest.roots)
				{
					emit << YAML::BeginMap;
					emit << YAML::Key << "id" << YAML::Value << entry.id;
					emit << YAML::Key << "path" << YAML::Value << entry.path.String();
					emit << YAML::Key << "label" << YAML::Value << entry.label;
					emit << YAML::EndMap;
				}
				emit << YAML::EndSeq;
				emit << YAML::Key << "irrep_labels" << YAML::Value << YAML::BeginSeq;
				for (const auto &[irrep, label] : manifest.irrepLabelOverrides)
				{
					emit << YAML::BeginMap;
					emit << YAML::Key << "irrep" << YAML::Value << irrep;
					emit << YAML::Key << "label" << YAML::Value << label;
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

	Path ProjectManifestIO::ManifestPath(const Path &projectDirectory)
	{
		return projectDirectory / "manifest.yaml";
	}

	ProjectManifest ProjectManifestIO::CreateNew(const std::string &name)
	{
		ProjectManifest manifest;
		manifest.uuid = ToString(GenerateUuid());
		manifest.name = name;
		manifest.createdAt = NowEpochSeconds();
		manifest.lastModified = manifest.createdAt;
		manifest.formatVersion = 1;
		return manifest;
	}

	bool ProjectManifestIO::Load(const Path &projectDirectory, ProjectManifest &outManifest, std::string &outError)
	{
		std::string text;
		if (!TextFileIO::Load(ManifestPath(projectDirectory), text, outError))
			return false;
		return ParseYaml(text, outManifest, outError);
	}

	bool ProjectManifestIO::Save(const Path &projectDirectory, ProjectManifest &manifest, std::string &outError)
	{
		manifest.lastModified = NowEpochSeconds();
		std::string text;
		if (!SerializeYaml(manifest, text, outError))
			return false;
		return TextFileIO::Save(ManifestPath(projectDirectory), text, outError);
	}
} // namespace DefectStudio
