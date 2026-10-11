#pragma once

#include <array>
#include "Presentation/Panels/DefectEnergeticsSession.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class DefectEnergeticsPanel final : public IPanel
	{
	public:
		enum class View { Conditions, Calculations, Formation, Transitions, Concentrations };
		DefectEnergeticsPanel(Ref<DefectEnergeticsSession> session, View view, std::string title, bool visibleByDefault = false);
		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] Ref<IPanel> Clone() const override;
		[[nodiscard]] const Ref<DefectEnergeticsSession> &Session() const { return m_Session; }

	private:
		void drawSource();
		void pickReference(const std::string &species);
		void drawRows();
		void drawStateRow(std::size_t row, const SpeciesEnergies &mu);
		void drawChemicalPotentials();
		void drawMuReferences();
		void drawMuPhases();
		void drawMuSets();
		void drawMuCombo();
		void drawContinuations();
		void drawFormation();
		void drawExport();
		void drawConcentrations();
		void drawConcentrationInputs();
		void drawConcentrationResults(int mode);
		void renderWindow();

		Ref<DefectEnergeticsSession> m_Session;
		View m_View;
		int m_ConcentrationLambda = 0, m_ConcentrationTemperature = 0, m_ConcentrationMap = 0;
		float m_ConcentrationHeight = 300.0f;
		std::array<double, 2> m_FormationRange{};
		std::array<char, 64> m_NewPhaseFormula{}, m_NewSetName{};
	};
} // namespace DefectStudio
