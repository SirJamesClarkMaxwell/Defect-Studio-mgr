#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

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
		std::filesystem::path logFilePath;
	};

	const char *ToString(LogLevel level);

	class Logger
	{
	public:
		static void Initialize(const LoggerOptions &options);
		static void Shutdown();
		static std::shared_ptr<spdlog::logger> &Get();

	private:
		static std::shared_ptr<spdlog::logger> &Access();
	};
} // namespace DefectStudio

#define DS_LOG_TRACE(...) ::DefectStudio::Logger::Get()->trace(__VA_ARGS__)
#define DS_LOG_DEBUG(...) ::DefectStudio::Logger::Get()->debug(__VA_ARGS__)
#define DS_LOG_INFO(...) ::DefectStudio::Logger::Get()->info(__VA_ARGS__)
#define DS_LOG_WARN(...) ::DefectStudio::Logger::Get()->warn(__VA_ARGS__)
#define DS_LOG_ERROR(...) ::DefectStudio::Logger::Get()->error(__VA_ARGS__)
#define DS_LOG_CRITICAL(...) ::DefectStudio::Logger::Get()->critical(__VA_ARGS__)