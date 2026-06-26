#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Logging/LogRegistry.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class LoggingPanel final : public IPanel
	{
	public:
		explicit LoggingPanel(Ref<LogRegistry> logRegistry = {}, std::string title = "Logging Panel", bool visibleByDefault = true);
		LoggingPanel(const LoggingPanel &other);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void rebuildFromRegistry();
		void renderControls();
		void renderCategoryFilters();
		void renderEntries();
		[[nodiscard]] bool exportEntriesToCsv(const std::string &path) const;

		[[nodiscard]] bool isCategoryEnabled(LogCategory category) const;
		[[nodiscard]] bool isSeverityEnabled(LogLevel level) const;
		[[nodiscard]] static const char *severityIcon(LogLevel level);
		[[nodiscard]] static const char *categoryIcon(LogCategory category);

	private:
		Ref<LogRegistry> m_LogRegistry;
		std::vector<LogEntry> m_Entries;
		std::array<bool, static_cast<std::size_t>(LogLevel::Count)> m_ShowLevel{};
		std::unordered_map<LogCategory, bool> m_ShowCategory;
		bool m_Paused = false;
		bool m_AutoScroll = true;
		std::string m_LastExportStatus;
	};
} // namespace DefectStudio
