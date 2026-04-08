#include "Core/dspch.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Core/Logger.hpp"

namespace DefectStudio
{
	spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:
				return spdlog::level::trace;
			case LogLevel::Debug:
				return spdlog::level::debug;
			case LogLevel::Info:
				return spdlog::level::info;
			case LogLevel::Warn:
				return spdlog::level::warn;
			case LogLevel::Error:
				return spdlog::level::err;
			case LogLevel::Critical:
				return spdlog::level::critical;
		}

		return spdlog::level::info;
	}

	const char *ToString(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:
				return "trace";
			case LogLevel::Debug:
				return "debug";
			case LogLevel::Info:
				return "info";
			case LogLevel::Warn:
				return "warn";
			case LogLevel::Error:
				return "error";
			case LogLevel::Critical:
				return "critical";
		}

		return "info";
	}

	Ref<spdlog::logger> &LoggerStorage()
	{
		static Ref<spdlog::logger> s_Logger;
		return s_Logger;
	}

	Ref<spdlog::logger> &Logger::Access()
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
		const spdlog::level::level_enum logLevel = ToSpdlogLevel(options.level);

		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

		if (options.logToFile)
		{
			std::filesystem::path logFilePath = options.logFilePath;
			if (logFilePath.empty())
				logFilePath = std::filesystem::path("logs") / "DefectStudio.log";

			if (!logFilePath.parent_path().empty())
				std::filesystem::create_directories(logFilePath.parent_path());

			sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true));
		}

		LoggerStorage() = CreateRef<spdlog::logger>("defectstudio", sinks.begin(), sinks.end());
		spdlog::set_default_logger(LoggerStorage());
		spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
		spdlog::set_level(logLevel);
		spdlog::flush_on(spdlog::level::warn);
		LoggerStorage()->set_level(logLevel);
		LoggerStorage()->flush_on(spdlog::level::warn);
	}

	void Logger::Shutdown()
	{
		spdlog::shutdown();
		LoggerStorage().reset();
	}

	Ref<spdlog::logger> &Logger::Get()
	{
		return Access();
	}
} // namespace DefectStudio