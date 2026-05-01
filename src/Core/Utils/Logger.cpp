#include "Core/dspch.hpp"

#include <ctime>
#include <filesystem>
#include <sstream>

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

	const char *ToString(LogCategory category)
	{
		switch (category)
		{
			case LogCategory::General:
				return "General";
			case LogCategory::Project:
				return "Project";
			case LogCategory::Import:
				return "Import";
			case LogCategory::Export:
				return "Export";
			case LogCategory::JobSystem:
				return "JobSystem";
			case LogCategory::Parsing:
				return "Parsing";
			case LogCategory::UI:
				return "UI";
			case LogCategory::Scripting:
				return "Scripting";
			case LogCategory::Rendering:
				return "Rendering";
			case LogCategory::Capability:
				return "Capability";
			case LogCategory::Config:
				return "Config";
			case LogCategory::Notification:
				return "Notification";
			case LogCategory::Count:
				break;
		}

		return "General";
	}

	std::string LogEntry::Origin() const
	{
		if (!sourceFile.empty())
		{
			std::filesystem::path path(sourceFile);
			std::string origin = path.stem().string();
			if (origin.empty())
				origin = path.filename().string();
			if (sourceLine > 0)
				origin += ":" + std::to_string(sourceLine);
			return origin;
		}

		if (!loggerName.empty())
			return loggerName;
		if (!threadLabel.empty())
			return threadLabel;
		return "-";
	}

	std::string LogEntry::TimestampString() const
	{
		if (timestamp == Time::TimePoint{})
			return "-";

		const std::time_t time = Time::Clock::to_time_t(timestamp);
		std::tm localTime{};
#if defined(_WIN32)
		localtime_s(&localTime, &time);
#else
		localtime_r(&time, &localTime);
#endif

		char buffer[32] = {};
		std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &localTime);
		return std::string(buffer);
	}

	std::string LogEntry::ToString() const
	{
		std::ostringstream stream;
		stream << '[' << TimestampString() << "] "
		       << '[' << DefectStudio::ToString(level) << "] "
		       << '[' << DefectStudio::ToString(category) << "] "
		       << Origin() << ": "
		       << message;
		return stream.str();
	}

	void ResetLoggerStorage()
	{
		if (s_Logger != nullptr)
		{
			s_Logger->flush();
			spdlog::drop(s_Logger->name());
			s_Logger.reset();
		}
	}


	void Logger::Initialize(const LoggerOptions &options)
	{
		const spdlog::level::level_enum logLevel = ToSpdlogLevel(options.level);
		ResetLoggerStorage();

		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(CreateRef<spdlog::sinks::stdout_color_sink_mt>());
		if (options.logRegistry != nullptr)
			sinks.push_back(CreateRef<LogRegistrySink>(options.logRegistry));

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
		s_Logger->set_level(logLevel);
		s_Logger->flush_on(spdlog::level::trace);

		if (!fileLoggingError.empty())
			s_Logger->error("File logging disabled [{}]: {}", resolvedLogFilePath, fileLoggingError);
		else if (options.logToFile)
			s_Logger->info("File logging active: {}", resolvedLogFilePath.empty() ? "logs/DefectStudio.log" : resolvedLogFilePath);
	}

	void Logger::Shutdown()
	{
		Flush();
		spdlog::shutdown();
		s_Logger.reset();
	}

	void Logger::Flush()
	{
		if (s_Logger != nullptr)
			s_Logger->flush();
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
