#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"

#include <spdlog/common.h>

namespace DefectStudio
{
	class LogRegistry;

	enum class LogLevel
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,
		Critical
	};

	enum class LogCategory
	{
		General,
		Project,
		Import,
		Export,
		JobSystem,
		Parsing,
		UI,
		Scripting,
		Rendering,
		Capability,
		Config,
		Notification,
		Count
	};

	struct LogEntry
	{
		LogLevel level = LogLevel::Info;
		LogCategory category = LogCategory::General;
		std::string loggerName;
		std::string message;
		std::string sourceFile;
		std::string sourceFunction;
		std::uint32_t sourceLine = 0;
		std::thread::id threadId{};
		std::string threadLabel;
		Time::TimePoint timestamp{};

		[[nodiscard]] std::string Origin() const;
		[[nodiscard]] std::string TimestampString() const;
		[[nodiscard]] std::string ToString() const;
	};

	struct LoggerOptions
	{
		LogLevel level = LogLevel::Info;
		bool logToFile = false;
		Path logFilePath;
		Ref<LogRegistry> logRegistry;
	};

	const char *ToString(LogLevel level);
	const char *ToString(LogCategory category);

	class Logger
	{
	public:
		static void Initialize(const LoggerOptions &options);
		static void Shutdown();
		static void Flush();
		static Ref<spdlog::logger> &Get();
	};
} // namespace DefectStudio

#define DS_LOG_TRACE(...) ::DefectStudio::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::trace, __VA_ARGS__)
#define DS_LOG_DEBUG(...) ::DefectStudio::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::debug, __VA_ARGS__)
#define DS_LOG_INFO(...) ::DefectStudio::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::info, __VA_ARGS__)
#define DS_LOG_WARN(...) ::DefectStudio::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::warn, __VA_ARGS__)
#define DS_LOG_ERROR(...) ::DefectStudio::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::err, __VA_ARGS__)
#define DS_LOG_CRITICAL(...) ::DefectStudio::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::critical, __VA_ARGS__)
