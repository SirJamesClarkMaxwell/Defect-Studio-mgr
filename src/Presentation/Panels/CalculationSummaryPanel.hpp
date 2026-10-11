#pragma once

#include <cstdint>

#include "Presentation/Panels/CalculationSummarySession.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class CalculationSummaryPanel final : public IPanel
	{
	public:
		CalculationSummaryPanel(Ref<CalculationSummarySession> session, std::string title, bool visibleByDefault = false);
		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] const char *GetSubcategory() const override { return "Obliczenia VASP"; }
		[[nodiscard]] Ref<IPanel> Clone() const override;
		[[nodiscard]] const Ref<CalculationSummarySession> &Session() const { return m_Session; }
		void RequestFocus();
		void DockAsTab(const CalculationSummaryPanel &target)
		{
			m_PendingDockId = target.DockId();
			m_TabTargetTitle = target.GetTitle();
		}
		void Close();
		[[nodiscard]] bool IsClosed() const { return m_Closed; }
		[[nodiscard]] unsigned int DockId() const { return m_DockId; }
		[[nodiscard]] std::uint64_t FocusOrder() const { return m_FocusOrder; }

	private:
		void renderChain();
		void renderPlots(bool convergence);
		void renderBoundaries();
		void renderSummary(const VaspOutputData &data);
		Ref<CalculationSummarySession> m_Session;
		float m_TopFraction = 0.45f;
		float m_LeftFraction = 0.5f;
		unsigned int m_DockId = 0;
		std::optional<unsigned int> m_PendingDockId;
		std::optional<std::string> m_TabTargetTitle;
		std::uint64_t m_FocusOrder = 0;
		bool m_FocusRequested = false;
		bool m_Closed = false;
	};
} // namespace DefectStudio
