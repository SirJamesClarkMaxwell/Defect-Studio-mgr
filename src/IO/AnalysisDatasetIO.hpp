#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	enum class AnalysisDatasetKind { Phonons, Dispersion, Ccd };
	struct AnalysisDatasetRef
	{
		AnalysisDatasetKind kind;
		Path directory;
		std::string label;
		std::string schema;
	};
	[[nodiscard]] std::optional<AnalysisDatasetRef> ReadDatasetRef(const Path &directory);
	// Children of defectstudio directories, without symlinks or descending into datasets.
	[[nodiscard]] std::vector<AnalysisDatasetRef> FindDatasets(const Path &root, const std::function<bool()> &cancelled);
	[[nodiscard]] Path DefaultDatasetDirectory(const std::vector<Path> &inputs, std::string_view kindDirName,
		const Path &projectDirectory, const std::function<bool(const Path &)> &isWritable);
	[[nodiscard]] bool IsDirectoryWritable(const Path &directory);
}
