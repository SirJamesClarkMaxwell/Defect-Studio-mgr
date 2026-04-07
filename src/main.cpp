#include "Core/dspch.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

#include <spdlog/common.h>

#include "App/Application.hpp"
#include "Core/Logger.hpp"

#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

namespace
{
DefectStudio::ApplicationOptions ParseArguments(int argc, char **argv)
{
	DefectStudio::ApplicationOptions options;

	for (int index = 1; index < argc; ++index)
	{
		std::string_view argument = argv[index];
		if (argument == "--help" || argument == "-h")
		{
			std::cout << "Usage: DefectStudio [--log-level=<trace|debug|info|warn|error>] [--log-file[=path]] "
			             "[--project=path] [--safe-mode] [--reset-layout]\n";
			std::exit(0);
		}
		if (argument.rfind("--log-level=", 0) == 0)
		{
			options.logLevel = spdlog::level::from_str(std::string(argument.substr(12)));
			continue;
		}
		if (argument == "--log-file")
		{
			options.logToFile = true;
			options.logFilePath = std::filesystem::path("logs") / "DefectStudio.log";
			continue;
		}
		if (argument.rfind("--log-file=", 0) == 0)
		{
			options.logToFile = true;
			options.logFilePath = std::filesystem::path(argument.substr(11));
			continue;
		}
		if (argument.rfind("--project=", 0) == 0)
		{
			options.projectPath = std::filesystem::path(argument.substr(10));
			continue;
		}
		if (argument == "--safe-mode")
		{
			options.safeMode = true;
			continue;
		}
		if (argument == "--reset-layout")
		{
			options.resetLayout = true;
		}
	}

	return options;
}
} // namespace

int main(int argc, char **argv)
{
	ZoneScoped;
	TracyMessageL("DefectStudio startup");

	auto options = ParseArguments(argc, argv);
	DefectStudio::LoggerOptions loggerOptions;
	loggerOptions.level = options.logLevel;
	loggerOptions.logToFile = options.logToFile;
	loggerOptions.logFilePath = options.logFilePath;
	DefectStudio::Logger::Initialize(loggerOptions);

	DefectStudio::Application app;
	const int result = app.Run(options);
	DefectStudio::Logger::Shutdown();
	return result;
}
