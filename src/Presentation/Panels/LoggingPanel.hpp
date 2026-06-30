#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Logging/LogRegistry.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;

	namespace AppEvents::Logs
	{
		struct ExportCompleted;
		struct ExportFailed;
	}

	class LoggingPanel final : public IPanel, public EventReceiver
	{
	public:
		explicit LoggingPanel(Ref<LogRegistry> logRegistry = {}, std::string title = "Logging Panel", bool visibleByDefault = true);
		LoggingPanel(
			Ref<EventBus> eventBus,
			Ref<LogRegistry> logRegistry,
			Path exportPath,
			std::string title = "Logging Panel",
			bool visibleByDefault = true);
		LoggingPanel(const LoggingPanel &other);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void bindLogExportEvents();
		void rebuildFromRegistry();
		void renderControls();
		void renderCategoryFilters();
		void renderEntries();
		void requestExport();
		void onExportCompleted(const AppEvents::Logs::ExportCompleted &event);
		void onExportFailed(const AppEvents::Logs::ExportFailed &event);

		[[nodiscard]] bool isCategoryEnabled(LogCategory category) const;
		[[nodiscard]] bool isSeverityEnabled(LogLevel level) const;
		[[nodiscard]] static const char *severityIcon(LogLevel level);
		[[nodiscard]] static const char *categoryIcon(LogCategory category);

	private:
		Ref<EventBus> m_EventBus;
		Ref<LogRegistry> m_LogRegistry;
		Path m_ExportPath;
		std::vector<LogEntry> m_Entries;
		std::array<bool, static_cast<std::size_t>(LogLevel::Count)> m_ShowLevel{};
		std::unordered_map<LogCategory, bool> m_ShowCategory;
		bool m_Paused = false;
		bool m_AutoScroll = true;
		std::string m_LastExportStatus;
	};
} // namespace DefectStudio
