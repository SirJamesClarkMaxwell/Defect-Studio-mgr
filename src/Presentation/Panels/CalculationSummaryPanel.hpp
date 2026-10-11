#pragma once

#include "Presentation/Panels/CalculationSummarySession.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class CalculationSummaryPanel final : public IPanel
	{
	public:
		enum class View { Summary, Convergence, KsLevels };
		CalculationSummaryPanel(Ref<CalculationSummarySession> session, View view, std::string title, bool visibleByDefault = false);
		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] const char *GetSubcategory() const override { return "Obliczenia VASP"; }
		[[nodiscard]] Ref<IPanel> Clone() const override;
		[[nodiscard]] const Ref<CalculationSummarySession> &Session() const { return m_Session; }

	private:
		void renderChain();
		void renderPlots();
		void renderBoundaries();
		void renderSummary(const VaspOutputData &data);
		Ref<CalculationSummarySession> m_Session;
		View m_View;
	};
} // namespace DefectStudio
