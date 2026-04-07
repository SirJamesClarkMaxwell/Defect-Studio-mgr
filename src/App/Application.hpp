#pragma once

#include <filesystem>

#include <spdlog/common.h>

namespace DefectStudio
{
struct ApplicationOptions
{
	spdlog::level::level_enum logLevel = spdlog::level::info;
	bool logToFile = false;
	std::filesystem::path logFilePath;
	std::filesystem::path projectPath;
	bool safeMode = false;
	bool resetLayout = false;
};

class Application
{
	public:
	int Run(const ApplicationOptions &options);
};
} // namespace DefectStudio
