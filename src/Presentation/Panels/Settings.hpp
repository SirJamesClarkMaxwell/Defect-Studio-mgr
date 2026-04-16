#pragma once

#include <string>

#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class Settings final : public IPanel
	{
	public:
		explicit Settings(std::string title = "Settings", bool visibleByDefault = true);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		[[nodiscard]] bool IsUrgentWorkerReserved() const
		{
			return m_ReserveUrgentWorker;
		}

		[[nodiscard]] float GetFontScaleStep() const
		{
			return m_FontScaleStep;
		}

		[[nodiscard]] float GetFontScale() const
		{
			return m_FontScale;
		}

		void SetFontScale(float fontScale);
		void SetFontScaleStep(float step);

	private:
		void renderSidebar();
		void renderSystemTab();
		void renderDisplayTab();

	private:
		bool m_ReserveUrgentWorker = true;
		bool m_SettingsInitialized = false;
		int m_WorkerThreadCount = 5;
		float m_FontScaleStep = 0.10f;
		float m_FontScale = 1.0f;
		int m_SelectedTabIndex = 0;
	};
} // namespace DefectStudio
