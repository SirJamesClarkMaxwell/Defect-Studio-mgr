#pragma once

#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

#include <spdlog/common.h>

namespace DefectStudio
{
	enum class LogLevel
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,
		Critical
	};

	struct LoggerOptions
	{
		LogLevel level = LogLevel::Info;
		bool logToFile = false;
		Path logFilePath;
	};

	const char *ToString(LogLevel level);

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
