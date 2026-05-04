#include "Core/dspch.hpp"

#include <imgui.h>
#include <string>
#include <utility>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Demo/DemoNotifications.hpp"
#include "Events/NotificationEvents.hpp"

namespace DefectStudio::Demo
{
	static const char *toString(NotificationSeverity severity)
	{
		switch (severity)
		{
		case NotificationSeverity::Info:
			return "Info";
		case NotificationSeverity::Warn:
			return "Warn";
		case NotificationSeverity::Error:
			return "Error";
		case NotificationSeverity::Critical:
			return "Critical";
		}

		return "Info";
	}

	DemoNotificationsPanel::DemoNotificationsPanel(Ref<EventBus> eventBus)
		: m_EventBus(std::move(eventBus))
	{
		if (m_EventBus)
			m_NotificationCenter = CreateUnique<NotificationCenter>(m_EventBus);
	}

	void DemoNotificationsPanel::Render()
	{
		if (!m_EventBus)
		{
			ImGui::TextUnformatted("Notification demo is not initialized.");
			return;
		}

		ImGui::TextUnformatted("Toast notifications and runtime diagnostics");
		ImGui::Spacing();

		auto emitNotification = [&](NotificationSeverity severity,
		                            const char *title,
		                            const char *message,
		                            NotificationCategory category = NotificationCategory::General) {
			std::string suffix = " #" + std::to_string(++m_NotificationDemoCounter);
			std::string resolvedTitle = std::string(title) + suffix;
			std::string resolvedMessage = std::string(message) + suffix;

			const std::uint32_t timeoutMs = severity == NotificationSeverity::Info ? 4000u : (severity == NotificationSeverity::Warn ? 8000u : 12000u);
			requestNotification(Notification{severity, category, std::move(resolvedTitle), std::move(resolvedMessage), "DemoNotificationsPanel", Time::Now(), timeoutMs, severity == NotificationSeverity::Critical});
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
		ImGui::SeparatorText("StructuredError example");
		if (ImGui::Button("Emit StructuredError notification"))
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
			requestNotification(ToNotification(error));
		}

		ImGui::Spacing();
		ImGui::SeparatorText("NotificationCenter history");
		const auto &entries = m_NotificationCenter->GetNotifications();
		ImGui::Text("History entries: %zu", entries.size());
		if (ImGui::BeginChild("##notification_history", ImVec2(0.0f, 180.0f), true))
		{
			for (auto it = entries.rbegin(); it != entries.rend(); ++it)
			{
				const Notification &notification = *it;
				ImGui::BulletText("[%s] %s: %s", toString(notification.severity), notification.title.c_str(), notification.message.c_str());
			}
		}
		ImGui::EndChild();
	}

	void DemoNotificationsPanel::requestNotification(Notification notification)
	{
		if (m_EventBus)
			m_EventBus->Queue(NotificationRequestedEvent{std::move(notification)});
	}
} // namespace DefectStudio::Demo
