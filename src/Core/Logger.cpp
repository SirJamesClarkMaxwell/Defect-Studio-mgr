#include "Core/dspch.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Core/Logger.hpp"

namespace DefectStudio
{
std::shared_ptr<spdlog::logger> &LoggerStorage()
{
	static std::shared_ptr<spdlog::logger> s_Logger;
	return s_Logger;
}

std::shared_ptr<spdlog::logger> &Logger::Access()
{
	if (LoggerStorage() == nullptr)
	{
		LoggerOptions defaultOptions;
		Initialize(defaultOptions);
	}

	return LoggerStorage();
}

void Logger::Initialize(const LoggerOptions &options)
{
	std::vector<spdlog::sink_ptr> sinks;
	sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

	if (options.logToFile)
	{
		std::filesystem::path logFilePath = options.logFilePath;
		if (logFilePath.empty())
		{
			logFilePath = std::filesystem::path("logs") / "DefectStudio.log";
		}
		if (!logFilePath.parent_path().empty())
		{
			std::filesystem::create_directories(logFilePath.parent_path());
		}
		sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true));
	}

	LoggerStorage() = std::make_shared<spdlog::logger>("defectstudio", sinks.begin(), sinks.end());
	spdlog::set_default_logger(LoggerStorage());
	spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
	spdlog::set_level(options.level);
	spdlog::flush_on(spdlog::level::warn);
	LoggerStorage()->set_level(options.level);
	LoggerStorage()->flush_on(spdlog::level::warn);
}

void Logger::Shutdown()
{
	spdlog::shutdown();
	LoggerStorage().reset();
}

std::shared_ptr<spdlog::logger> &Logger::Get()
{
	return Access();
}
} // namespace DefectStudio