#include "Core/dspch.hpp"

#include <imgui.h>
#include <string>
#include <utility>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Demo/DemoNotifications.hpp"
#include "Events/NotificationEvents.hpp"

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

	DemoNotificationsPanel::DemoNotificationsPanel(Ref<EventBus> eventBus)
		: m_EventBus(std::move(eventBus))
	{
		if (m_EventBus)
		{
			m_NotificationCenter = CreateUnique<NotificationCenter>(m_EventBus);
			m_NotificationCenter->RegisterListener(
				[this](const Notification &notification) {
					onNotification(notification);
				});
		}
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
		ImGui::SeparatorText("Recent notification history");
		const auto &history = m_NotificationCenter->GetNotifications();
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
		while (!m_PendingToasts.empty())
		{
			const Notification &notification = m_PendingToasts.front();
			ImGui::InsertNotification({
				toToastType(notification.severity),
				static_cast<int>(notification.timeoutMs),
				"%s",
				notification.message.c_str()});
			m_PendingToasts.pop_front();
		}

		ImGui::RenderNotifications();
	}

	void DemoNotificationsPanel::onNotification(const Notification &notification)
	{
		m_PendingToasts.push_back(notification);
	}

	void DemoNotificationsPanel::requestNotification(Notification notification)
	{
		if (m_EventBus)
			m_EventBus->Queue(NotificationRequestedEvent{std::move(notification)});
	}
} // namespace DefectStudio::Demo
