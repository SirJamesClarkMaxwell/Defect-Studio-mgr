#include "Core/dspch.hpp"
#include "IO/AnalysisDatasetIO.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	std::optional<AnalysisDatasetRef> ReadDatasetRef(const Path &directory)
	{
		try
		{
			std::string text, error;
			if (!TextFileIO::Load(directory / "summary.json", text, error)) return std::nullopt;
			const auto summary = nlohmann::json::parse(text);
			const auto schema = summary.at("schema").get<std::string>();
			AnalysisDatasetKind kind;
			if (schema == "ds.phonon_vibronic.v1") kind = AnalysisDatasetKind::Phonons;
			else if (schema == "ds.phonon_dispersion.v1") kind = AnalysisDatasetKind::Dispersion;
			else if (schema == "ds.ccd_analysis.v1") kind = AnalysisDatasetKind::Ccd;
			else return std::nullopt;
			return AnalysisDatasetRef{kind, Path::FromResolved(directory.Native().lexically_normal()),
				summary.value("label", directory.filename().Utf8()), schema};
		}
		catch (const std::exception &) { return std::nullopt; }
	}

	std::vector<AnalysisDatasetRef> FindDatasets(const Path &root, const std::function<bool()> &cancelled)
	{
		std::vector<AnalysisDatasetRef> datasets;
		if (root.Empty() || (cancelled && cancelled())) return datasets;
		std::error_code error;
		if (!std::filesystem::is_directory(std::filesystem::symlink_status(root.Native(), error)) || error) return datasets;
		// Include root itself when the caller starts at a dataset child of defectstudio.
		if (root.parent_path().filename().Utf8() == "defectstudio")
			if (auto dataset = ReadDatasetRef(root)) { datasets.push_back(std::move(*dataset)); return datasets; }
		std::filesystem::recursive_directory_iterator it(root.Native(), std::filesystem::directory_options::skip_permission_denied, error), end;
		while (!error && it != end)
		{
			if (cancelled && cancelled()) break;
			const auto status = it->symlink_status(error);
			if (!error && std::filesystem::is_directory(status) && it->path().parent_path().filename() == "defectstudio")
				if (auto dataset = ReadDatasetRef(Path::FromResolved(it->path())))
				{
					datasets.push_back(std::move(*dataset));
					it.disable_recursion_pending();
				}
			error.clear();
			it.increment(error);
		}
		std::sort(datasets.begin(), datasets.end(), [](const auto &a, const auto &b) { return a.directory.Native() < b.directory.Native(); });
		return datasets;
	}

	Path DefaultDatasetDirectory(const std::vector<Path> &inputs, std::string_view kindDirName,
		const Path &projectDirectory, const std::function<bool(const Path &)> &isWritable)
	{
		const Path fallback = projectDirectory.Empty() ? Path{} : projectDirectory.Join("defectstudio").Join(kindDirName);
		FilePath common;
		for (const auto &input : inputs)
		{
			if (input.Empty()) continue;
			std::error_code error;
			const auto native = FileSystem::Absolute(input.Native(), error).lexically_normal();
			if (error) return fallback;
			if (common.empty()) common = native;
			else
			{
				FilePath shared;
				auto a = common.begin(), b = native.begin();
				for (; a != common.end() && b != native.end() && Platform::FileSystemNamesEqual(*a, *b); ++a, ++b) shared /= *a;
				common = std::move(shared);
				if (common.empty()) return fallback;
			}
		}
		if (common.empty()) return fallback;
		const auto destination = common / "defectstudio";
		auto ancestor = destination;
		std::error_code error;
		while (!ancestor.empty() && !std::filesystem::exists(ancestor, error))
		{
			if (error || ancestor == ancestor.parent_path()) return fallback;
			ancestor = ancestor.parent_path();
		}
		if (error || ancestor.empty() || !isWritable || !isWritable(Path::FromResolved(ancestor))) return fallback;
		return Path::FromResolved(destination).Join(kindDirName);
	}

	bool IsDirectoryWritable(const Path &directory)
	{
		if (directory.Empty()) return false;
		try
		{
			const auto probe = directory.Join(".ds-write-probe-" + ToString(GenerateUuid()));
			std::ofstream out(probe.Native(), std::ios::binary);
			if (!out) return false;
			out.put('\0');
			out.close();
			const bool wrote = static_cast<bool>(out);
			std::error_code error;
			const bool removed = FileSystem::Remove(probe.Native(), error);
			return wrote && removed && !error;
		}
		catch (const std::exception &) { return false; }
	}
}
