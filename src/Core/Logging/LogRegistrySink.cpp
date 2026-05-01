#include "Core/dspch.hpp"

#include "Core/Logging/LogRegistrySink.hpp"

#include "Core/Logging/LogRegistry.hpp"

#include <filesystem>

namespace DefectStudio
{

	NotificationSeverity toSeverity(spdlog::level::level_enum level)
	{
		switch (level)
		{
		case spdlog::level::trace:
			return NotificationSeverity::Trace;
		case spdlog::level::debug:
			return NotificationSeverity::Debug;
		case spdlog::level::info:
			return NotificationSeverity::Info;
		case spdlog::level::warn:
			return NotificationSeverity::Warning;
		case spdlog::level::err:
			return NotificationSeverity::Error;
		case spdlog::level::critical:
			return NotificationSeverity::Fatal;
		default:
			return NotificationSeverity::Info;
		}
	}

	NotificationCategory categoryFromSource(std::string_view fileName)
	{
		// Core/Runtime systems
		if (fileName.find("EventQueue") != std::string_view::npos)
			return NotificationCategory::JobSystem; // Core event handling
		if (fileName.find("EventBus") != std::string_view::npos || fileName.find("EventDispatcher") != std::string_view::npos)
			return NotificationCategory::JobSystem;
		
		// Settings/Config
		if (fileName.find("Settings") != std::string_view::npos)
			return NotificationCategory::Config;
		if (fileName.find("ConfigManager") != std::string_view::npos || fileName.find("ConfigController") != std::string_view::npos)
			return NotificationCategory::Config;
		
		// Job/Worker systems
		if (fileName.find("TaskMonitor") != std::string_view::npos || fileName.find("JobSystem") != std::string_view::npos)
			return NotificationCategory::JobSystem;
		if (fileName.find("ProgressTracker") != std::string_view::npos)
			return NotificationCategory::JobSystem;
		
		// UI/Application
		if (fileName.find("Application") != std::string_view::npos || fileName.find("Editor") != std::string_view::npos || fileName.find("Window") != std::string_view::npos)
			return NotificationCategory::UI;
		if (fileName.find("ImGui") != std::string_view::npos || fileName.find("Layer") != std::string_view::npos)
			return NotificationCategory::UI;
		
		// Project/IO
		if (fileName.find("IO") != std::string_view::npos || fileName.find("Path") != std::string_view::npos)
			return NotificationCategory::Project;
		if (fileName.find("Storage") != std::string_view::npos)
			return NotificationCategory::Project;
		
		// Capabilities
		if (fileName.find("Capability") != std::string_view::npos)
			return NotificationCategory::Capability;
		
		// Parsing/Rendering
		if (fileName.find("Parsing") != std::string_view::npos || fileName.find("Parser") != std::string_view::npos)
			return NotificationCategory::Parsing;
		if (fileName.find("Renderer") != std::string_view::npos)
			return NotificationCategory::Rendering;
		
		return NotificationCategory::General;
	}

	std::string originFromSource(const spdlog::source_loc &source)
	{
		if (source.filename == nullptr || *source.filename == '\0')
			return {};

		std::filesystem::path path(source.filename);
		std::string stem = path.stem().string();
		if (stem.empty())
			stem = path.filename().string();

		if (stem == "Settings")
			stem = "SettingsPanel";
		else if (stem == "TaskMonitorWindow")
			stem = "TaskMonitor";
		else if (stem == "ApplicationConfigController")
			stem = "ConfigService";
		else if (stem == "Application")
			stem = "Application";
		else if (stem == "Notifier")
			stem = "Notifier";
		else if (stem == "JobSystem")
			stem = "JobSystem";

		if (source.line > 0)
			stem += ":" + std::to_string(source.line);
		return stem;
	}


	void LogRegistrySink::sink_it_(const spdlog::details::log_msg &msg)
	{
		Notification notification;
		notification.severity = toSeverity(msg.level);
		std::string sourceStem;
		if (msg.source.filename != nullptr && *msg.source.filename != '\0')
		{
			std::filesystem::path path(msg.source.filename);
			sourceStem = path.stem().string();
		}
		if (sourceStem == "Notifier")
			return;
		notification.category = categoryFromSource(sourceStem);
		notification.message.assign(msg.payload.begin(), msg.payload.end());
		notification.timestamp = Time::Now();
		notification.source = originFromSource(msg.source);
		if (msg.logger_name.size() > 0)
			notification.title.assign(msg.logger_name.data(), msg.logger_name.size());

		LogRegistry::Get().Append(LogRegistry::Entry{std::move(notification), std::this_thread::get_id(), {}});
	}

	void LogRegistrySink::flush_()
	{
	}
} // namespace DefectStudio