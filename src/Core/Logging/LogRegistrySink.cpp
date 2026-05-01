#include "Core/dspch.hpp"

#include "Core/Logging/LogRegistrySink.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <thread>

#include "Core/Logging/LogRegistry.hpp"

namespace DefectStudio
{
	namespace
	{
		LogLevel toLogLevel(spdlog::level::level_enum level)
		{
			switch (level)
			{
				case spdlog::level::trace:
					return LogLevel::Trace;
				case spdlog::level::debug:
					return LogLevel::Debug;
				case spdlog::level::info:
					return LogLevel::Info;
				case spdlog::level::warn:
					return LogLevel::Warn;
				case spdlog::level::err:
					return LogLevel::Error;
				case spdlog::level::critical:
					return LogLevel::Critical;
				default:
					return LogLevel::Info;
			}
		}

		LogCategory categoryFromSource(std::string_view fileName)
		{
			if (fileName.find("EventQueue") != std::string_view::npos ||
			    fileName.find("EventBus") != std::string_view::npos ||
			    fileName.find("EventDispatcher") != std::string_view::npos ||
			    fileName.find("TaskMonitor") != std::string_view::npos ||
			    fileName.find("JobSystem") != std::string_view::npos ||
			    fileName.find("ProgressTracker") != std::string_view::npos)
				return LogCategory::JobSystem;

			if (fileName.find("Settings") != std::string_view::npos ||
			    fileName.find("ConfigManager") != std::string_view::npos ||
			    fileName.find("ConfigController") != std::string_view::npos)
				return LogCategory::Config;

			if (fileName.find("Application") != std::string_view::npos ||
			    fileName.find("Editor") != std::string_view::npos ||
			    fileName.find("Window") != std::string_view::npos ||
			    fileName.find("ImGui") != std::string_view::npos ||
			    fileName.find("Layer") != std::string_view::npos)
				return LogCategory::UI;

			if (fileName.find("Import") != std::string_view::npos)
				return LogCategory::Import;
			if (fileName.find("Export") != std::string_view::npos)
				return LogCategory::Export;
			if (fileName.find("IO") != std::string_view::npos ||
			    fileName.find("Path") != std::string_view::npos ||
			    fileName.find("Storage") != std::string_view::npos)
				return LogCategory::Project;

			if (fileName.find("Capability") != std::string_view::npos)
				return LogCategory::Capability;
			if (fileName.find("Notifier") != std::string_view::npos ||
			    fileName.find("Notification") != std::string_view::npos)
				return LogCategory::Notification;
			if (fileName.find("Parsing") != std::string_view::npos ||
			    fileName.find("Parser") != std::string_view::npos)
				return LogCategory::Parsing;
			if (fileName.find("Renderer") != std::string_view::npos)
				return LogCategory::Rendering;

			return LogCategory::General;
		}

		std::string sourceStem(const spdlog::source_loc &source)
		{
			if (source.filename == nullptr || *source.filename == '\0')
				return {};

			std::filesystem::path path(source.filename);
			std::string stem = path.stem().string();
			if (stem.empty())
				stem = path.filename().string();
			return stem;
		}
	}

	LogRegistrySink::LogRegistrySink(Ref<LogRegistry> registry)
		: m_Registry(CreateWeakRef(registry))
	{
	}

	void LogRegistrySink::sink_it_(const spdlog::details::log_msg &msg)
	{
		auto registry = m_Registry.lock();
		if (registry == nullptr)
			return;

		LogEntry entry;
		entry.level = toLogLevel(msg.level);
		entry.category = categoryFromSource(sourceStem(msg.source));
		entry.timestamp = Time::Now();
		entry.threadId = std::this_thread::get_id();
		entry.message.assign(msg.payload.begin(), msg.payload.end());
		entry.loggerName.assign(msg.logger_name.begin(), msg.logger_name.end());

		if (msg.source.filename != nullptr && *msg.source.filename != '\0')
			entry.sourceFile = msg.source.filename;
		if (msg.source.funcname != nullptr && *msg.source.funcname != '\0')
			entry.sourceFunction = msg.source.funcname;
		if (msg.source.line > 0)
			entry.sourceLine = static_cast<std::uint32_t>(msg.source.line);

		registry->Append(std::move(entry));
	}

	void LogRegistrySink::flush_()
	{
	}
} // namespace DefectStudio
