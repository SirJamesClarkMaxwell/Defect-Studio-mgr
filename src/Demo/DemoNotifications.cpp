#include "Core/dspch.hpp"

#include <exception>
#include <imgui.h>
#include <string>
#include <utility>

#include "App/Application.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Notifications/Notifier.hpp"
#include "Demo/DemoNotifications.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127 4267 4458)
#endif
#include "ImGuiNotify.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace DefectStudio::Demo
{
	static ImGuiToastType toToastType(NotificationSeverity severity)
	{
		switch (severity)
		{
		case NotificationSeverity::Info:
			return ImGuiToastType::Info;
		case NotificationSeverity::Warn:
			return ImGuiToastType::Warning;
		case NotificationSeverity::Error:
		case NotificationSeverity::Critical:
			return ImGuiToastType::Error;
		}

		return ImGuiToastType::Info;
	}

	static ImVec4 severityColor(NotificationSeverity severity)
	{
		switch (severity)
		{
		case NotificationSeverity::Info:
			return ImVec4(0.35f, 0.72f, 1.00f, 1.00f);
		case NotificationSeverity::Warn:
			return ImVec4(1.00f, 0.76f, 0.28f, 1.00f);
		case NotificationSeverity::Error:
			return ImVec4(1.00f, 0.38f, 0.38f, 1.00f);
		case NotificationSeverity::Critical:
			return ImVec4(1.00f, 0.18f, 0.18f, 1.00f);
		}

		return ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	}

	DemoNotificationsPanel::DemoNotificationsPanel(Ref<Notifier> notifier)
		: m_Notifier(std::move(notifier))
	{
	}

	void DemoNotificationsPanel::Render()
	{
		if (!m_Notifier)
		{
			ImGui::TextUnformatted("Notification demo is not initialized.");
			return;
		}

		auto &application = Application::Get();
		auto &notifier = *m_Notifier;
		auto &capabilityRegistry = application.GetCapabilityRegistry();
		auto &capabilityService = application.GetCapabilityService();

		ImGui::TextUnformatted("Toast notifications and runtime diagnostics");
		ImGui::Spacing();

		auto emitNotification = [&](NotificationSeverity severity,
		                            const char *title,
		                            const char *message,
		                            NotificationCategory category = NotificationCategory::General) {
			std::string suffix = " #" + std::to_string(++m_NotificationDemoCounter);
			std::string resolvedTitle = std::string(title) + suffix;
			std::string resolvedMessage = std::string(message) + suffix;

			switch (severity)
			{
			case NotificationSeverity::Info:
				notifier.Info(resolvedTitle, resolvedMessage, category);
				break;
			case NotificationSeverity::Warn:
				notifier.Warning(resolvedTitle, resolvedMessage, category);
				break;
			case NotificationSeverity::Error:
				notifier.Error(resolvedTitle, resolvedMessage, category);
				break;
			case NotificationSeverity::Critical:
				notifier.Critical(resolvedTitle, resolvedMessage, category);
				break;
			}

			ImGui::InsertNotification({toToastType(severity), 3000, "%s", resolvedMessage.c_str()});
		};

		if (ImGui::Button("Info toast"))
			emitNotification(NotificationSeverity::Info, "Info", "ImGuiNotify toast from DemoLayer");
		ImGui::SameLine();
		if (ImGui::Button("Warning toast"))
			emitNotification(NotificationSeverity::Warn, "Warning", "Diagnostic warning surfaced in UI");
		ImGui::SameLine();
		if (ImGui::Button("Error toast"))
			emitNotification(NotificationSeverity::Error, "Error", "Blocking error path demonstrated");
		ImGui::SameLine();
		if (ImGui::Button("Critical toast"))
			emitNotification(NotificationSeverity::Critical, "Critical", "Critical diagnostic surfaced in UI");

		ImGui::Spacing();
		ImGui::SeparatorText("Runtime snapshot");
		ImGui::Text("Capability registry locked: %s", capabilityRegistry.IsLocked() ? "yes" : "no");
		ImGui::Text("Registered capabilities: %zu", capabilityRegistry.GetAll().size());
		ImGui::Text("Capability service reports 'ui.notifications': %s", capabilityService.IsAvailable("ui.notifications") ? "yes" : "no");

		ImGui::Spacing();
		ImGui::SeparatorText("CapabilityService example");
		if (ImGui::Button("Require ui.notifications"))
		{
			try
			{
				capabilityService.Require("ui.notifications");
				emitNotification(
					NotificationSeverity::Info,
					"Capability available",
					"ui.notifications requirement satisfied",
					NotificationCategory::Capability);
			}
			catch (const std::exception &exception)
			{
				emitNotification(
					NotificationSeverity::Error,
					"Capability missing",
					exception.what(),
					NotificationCategory::Capability);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Require demo.missing"))
		{
			try
			{
				capabilityService.Require("demo.missing");
				emitNotification(
					NotificationSeverity::Info,
					"Capability available",
					"demo.missing requirement satisfied",
					NotificationCategory::Capability);
			}
			catch (const std::exception &exception)
			{
				emitNotification(
					NotificationSeverity::Error,
					"Capability missing",
					exception.what(),
					NotificationCategory::Capability);
			}
		}

		ImGui::Spacing();
		ImGui::SeparatorText("StructuredError example");
		if (ImGui::Button("Show blocking StructuredError"))
		{
			StructuredError error{
				ErrorCategory::Runtime,
				Severity::Error,
				"Unable to load required renderer resource.",
				"Resource key='demo.missing.texture' was not found in cache.",
				"Verify assets path and rebuild the cache.",
				"DemoLayer",
				"DEMO_RENDERER_ASSET_MISSING",
				DisplayPolicy::BlockingPopup};
			application.ShowBlockingError(error);
			notifier.Notify(ToNotification(error));
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Recent notifier history");
		const auto &history = notifier.GetHistory();
		ImGui::Text("History entries: %zu", history.size());
		if (ImGui::BeginChild("##notification_history", ImVec2(0.0f, 180.0f), true))
		{
			for (auto it = history.rbegin(); it != history.rend(); ++it)
			{
				const Notification &notification = *it;
				const ImVec4 color = severityColor(notification.severity);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::BulletText("[%s] %s", notification.title.c_str(), notification.message.c_str());
				ImGui::PopStyleColor();
				if (!notification.source.empty())
				{
					ImGui::Indent();
					ImGui::TextDisabled("Source: %s", notification.source.c_str());
					ImGui::Unindent();
				}
			}
		}
		ImGui::EndChild();
	}

	void DemoNotificationsPanel::RenderToasts()
	{
		ImGui::RenderNotifications();
	}
} // namespace DefectStudio::Demo
