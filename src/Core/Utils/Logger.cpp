#include "Core/dspch.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Core/Logging/LogRegistrySink.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
namespace DefectStudio
{
	static Ref<spdlog::logger> s_Logger = nullptr;
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



	void ResetLoggerStorage()
	{
		auto &logger = Logger::Get();
		if (logger != nullptr)
		{
			logger->flush();
			spdlog::drop(logger->name());
			logger.reset();
		}
	}


	void Logger::Initialize(const LoggerOptions &options)
	{
		const spdlog::level::level_enum logLevel = ToSpdlogLevel(options.level);
		ResetLoggerStorage();

		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(CreateRef<spdlog::sinks::stdout_color_sink_mt>());
		sinks.push_back(CreateRef<LogRegistrySink>());

		std::string fileLoggingError;
		std::string resolvedLogFilePath;
		if (options.logToFile)
		{
			Path logFilePath = options.logFilePath;
			if (logFilePath.empty())
				logFilePath = Path("logs") / "DefectStudio.log";
			resolvedLogFilePath = logFilePath.string();

			try
			{
				if (!logFilePath.parent_path().empty())
					FileSystem::CreateDirectories(logFilePath.parent_path().Native());
				sinks.push_back(CreateRef<spdlog::sinks::basic_file_sink_mt>(resolvedLogFilePath, true));
			}
			catch (const std::exception &exception)
			{
				fileLoggingError = exception.what();
			}
		}

		s_Logger = CreateRef<spdlog::logger>("defectstudio", sinks.begin(), sinks.end());
		spdlog::set_default_logger(s_Logger);
		spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
		spdlog::set_level(logLevel);
		spdlog::flush_on(spdlog::level::trace);
		s_Logger()->set_level(logLevel);
		s_Logger()->flush_on(spdlog::level::trace);

		if (!fileLoggingError.empty())
			s_Logger->error("File logging disabled [{}]: {}", resolvedLogFilePath, fileLoggingError);
		else if (options.logToFile)
			s_Logger->info("File logging active: {}", resolvedLogFilePath.empty() ? "logs/DefectStudio.log" : resolvedLogFilePath);
	}

	void Logger::Shutdown()
	{
		Flush();
		spdlog::shutdown();
		Logger::Get().reset();
	}

	void Logger::Flush()
	{
		if (Logger::Get() != nullptr)
			Logger::Get()->flush();
		spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) {
			if (logger != nullptr)
				logger->flush();
		});
	}

	Ref<spdlog::logger> &Logger::Get()
	{
		if (s_Logger == nullptr)
		{
			LoggerOptions defaultOptions;
			defaultOptions.logToFile = true;
			Initialize(defaultOptions);
		}

		return s_Logger;
	}
} // namespace DefectStudio
