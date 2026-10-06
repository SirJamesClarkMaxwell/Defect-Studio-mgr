#include "Core/dspch.hpp"
#include "Presentation/Panels/GroupTheoryPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include <imgui.h>

#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	void GroupTheoryPanel::Cell(const ExactCoefficient &value)
	{
		ImGui::TextUnformatted(FormatExactValue(value, LabelStyle::Unicode).c_str());
		if (ImGui::IsItemHovered())
		{
			char numeric[96];
			if (value.numericImaginary != 0.0)
				std::snprintf(numeric, sizeof(numeric), "%.8g %+.8gi", value.numeric, value.numericImaginary);
			else
				std::snprintf(numeric, sizeof(numeric), "%.8g", value.numeric);
			ImGui::SetTooltip("%s", numeric);
		}
	}

	std::optional<GroupTheoryPanel::BasisKey> GroupTheoryPanel::currentBasisKey() const
	{
		if (!m_BasisKey.has_value())
			return std::nullopt;

		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
			return std::nullopt;

		const Result<AtomEditTarget> target =
			ResolveAtomEditTarget(m_RendererLayer, *domainLayer, m_BasisKey->windowId);
		if (!target || target->record == nullptr)
			return std::nullopt;

		const StructureRecord &record = *target->record;
		if (ToString(record.id) != m_BasisKey->structureId)
			return std::nullopt;

		BasisKey current = *m_BasisKey;
		current.revision = record.revision;
		const auto basis = selectionBasis(*target->windowState, record);
		if (!basis)
			return std::nullopt;
		current.basisHash = basis->hash;
		return current;
	}

	Result<SelectionBasis> GroupTheoryPanel::selectionBasis(
		const RendererWindowState &windowState, const StructureRecord &record) const
	{
		const auto &selection = windowState.selectedAtomIndices;
		if (selection.empty() || selection.back() >= record.structure.atoms.size())
			return BuildSelectionBasis(record.structure, selection, glm::dvec3(0.0));
		glm::dvec3 centre(record.structure.atoms[selection.back()].position);
		if (m_CentreMode == CentreMode::Cursor)
			centre = glm::dvec3(windowState.cursor3DPosition);
		else if (m_CentreMode == CentreMode::Vacancy)
		{
			if (m_VacancyIndex >= record.structure.vacancies.size())
				return StructuredError{ErrorCategory::Validation, Severity::Error, "The analysis vacancy no longer exists.",
					"", "Choose another analysis centre.", "GroupTheoryPanel", "symmetry.basis.missing_vacancy"};
			centre = glm::dvec3(record.structure.vacancies[m_VacancyIndex].position);
		}
		auto basis = BuildSelectionBasis(record.structure, selection, centre);
		if (basis && m_CentreMode == CentreMode::SelectionCentroid)
		{
			glm::dvec3 mean(0.0);
			for (const BasisSite &site : basis->sites)
				mean += site.position;
			centre += mean / static_cast<double>(basis->sites.size());
			basis = BuildSelectionBasis(record.structure, selection, centre);
		}
		return basis;
	}

	void GroupTheoryPanel::drawSalcBasisCombo()
	{
		constexpr const char *names[] = {"sp³ (wiązanie zwisające)", "p → centrum", "s"};
		if (ImGui::BeginCombo("Funkcja bazowa", names[static_cast<int>(m_SalcFunction)]))
		{
			for (int i = 0; i < 3; ++i)
				if (ImGui::Selectable(names[i], i == static_cast<int>(m_SalcFunction)))
					m_SalcFunction = static_cast<SalcBasisFunction>(i);
			ImGui::EndCombo();
		}
	}

	std::string GroupTheoryPanel::projectedSalcName(const std::size_t vectorIndex) const
	{
		const auto &vector = m_Result->reduction.projectedVectors[vectorIndex];
		std::string name = m_PhysicalBuffers[vectorIndex].data();
		if (name.empty())
		{
			name = vector.irrepLabel + " #" + std::to_string(vector.occurrenceIndex + 1);
			const auto &irreps = m_Result->reduction.decomposition;
			const auto irrep = std::find_if(irreps.begin(), irreps.end(), [&](const auto &entry) {
				return entry.irrepLabel == vector.irrepLabel;
			});
			if (irrep != irreps.end() && irrep->dimension > 1)
				name += " (" + std::to_string(vector.irrepRow + 1) + ")";
		}
		return name;
	}

	void GroupTheoryPanel::drawSalcButton(const SymmetryAdaptedVector &vector, const std::string &name)
	{
		auto &windows = m_RendererLayer.GetWindows();
		const auto window = std::find_if(windows.begin(), windows.end(), [&](const auto &candidate) {
			return m_BasisKey && candidate.windowId == m_BasisKey->windowId;
		});
		const bool missingWindow = window == windows.end();
		const bool stale = !m_Basis || !m_BasisKey || currentBasisKey() != m_BasisKey;
		const bool complex = std::any_of(vector.coefficients.begin(), vector.coefficients.end(),
			[](const auto &coefficient) { return std::abs(coefficient.numericImaginary) >= 1e-9; });
		ImGui::BeginDisabled(missingWindow || stale || complex);
		const bool draw = ImGui::Button("Rysuj");
		ImGui::EndDisabled();
		if ((missingWindow || stale) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", missingWindow ? "Okno źródłowe zostało zamknięte." : "Zaznaczenie się zmieniło - przelicz");
		if (complex && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Wektor zespolony - narysuj Re/Im z tabeli „Baza rzeczywista” poniżej.");
		if (!draw || missingWindow || stale || complex)
			return;
		auto orbital = BuildSalcSceneOrbital(*window, vector, *m_Basis, m_SalcFunction, name);
		if (!orbital)
		{
			m_Error = orbital.Error();
			return;
		}
		PushPinnedMeasurementUndoSnapshot(*window);
		window->selectedSceneOrbitals = {AppendSceneOrbital(*window, std::move(orbital.Value()))};
		SceneSystem::SyncLabelEntities(window->sceneRegistry, *window);
		m_Error.reset();
	}

	void GroupTheoryPanel::drawRealPairVectors()
	{
		const auto &vectors = m_Result->reduction.realPairVectors;
		if (vectors.empty())
			return;
		ImGui::TextUnformatted("Baza rzeczywista par zespolonych");
		ImGui::TextDisabled("u = √2 Re z, v = √2 Im z - jeden z równoważnych wyborów bazy "
			"(obrót w płaszczyźnie u, v), nie wynik teorii grup.");
		const auto &sites = m_Result->reduction.siteLabels;
		const int columns = static_cast<int>(sites.size()) + 4;
		if (!ImGui::BeginTable("##group_real_pairs", columns, ImGuiTableFlags_Borders |
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame))
			return;
		ImGui::TableSetupColumn("Para");
		ImGui::TableSetupColumn("Kopia");
		ImGui::TableSetupColumn("Składowa");
		for (const auto &site : sites)
			ImGui::TableSetupColumn(site.c_str());
		ImGui::TableSetupColumn("Rysuj");
		ImGui::TableHeadersRow();
		for (std::size_t index = 0; index < vectors.size(); ++index)
		{
			const auto &vector = vectors[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			MathLabel(FormatIrrepLabel(vector.irrepLabel, LabelStyle::Latex) + " \\oplus " +
				FormatIrrepLabel(vector.conjugateIrrepLabel, LabelStyle::Latex));
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", vector.occurrenceIndex + 1);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(vector.irrepRow == 0 ? "u (Re)" : "v (Im)");
			for (std::size_t column = 0; column < vector.coefficients.size(); ++column)
			{
				ImGui::TableSetColumnIndex(static_cast<int>(column + 3));
				Cell(vector.coefficients[column]);
			}
			ImGui::TableSetColumnIndex(columns - 1);
			const std::string name = vector.irrepLabel + "⊕" + vector.conjugateIrrepLabel + " #" +
				std::to_string(vector.occurrenceIndex + 1) + (vector.irrepRow == 0 ? " Re" : " Im");
			drawSalcButton(vector, name);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
} // namespace DefectStudio
