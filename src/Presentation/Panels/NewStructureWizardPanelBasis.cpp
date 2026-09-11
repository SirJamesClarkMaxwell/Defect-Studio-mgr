// The lattice-centering radio group, the motif table and the read-only list of what the two
// convolve into. Split out of NewStructureWizardPanel.cpp, which is over the file-size rule.
#include "Core/dspch.hpp"

#include "Presentation/Panels/NewStructureWizardPanel.hpp"

#include <algorithm>
#include <array>

#include <imgui.h>

#include "Domain/Crystal/LatticeBasisExpansion.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	namespace
	{
		struct PresetButtonSpec
		{
			BravaisCenteringPreset preset;
			const char *label;
		};
		constexpr std::array<PresetButtonSpec, 4> kPresetButtons = {
				PresetButtonSpec{BravaisCenteringPreset::Primitive, "Primitive (P)"},
				PresetButtonSpec{BravaisCenteringPreset::BodyCentered, "Body-centered (I)"},
				PresetButtonSpec{BravaisCenteringPreset::FaceCentered, "Face-centered (F)"},
				PresetButtonSpec{BravaisCenteringPreset::BaseCentered, "Base-centered (C)"}};
	} // namespace

	void NewStructureWizardPanel::drawCenteringPresetRow()
	{
		ImGui::TextUnformatted("Lattice centering:");
		ImGui::SameLine();
		ImGui::TextDisabled("(repeats the basis below; never edits it)");
		// Two per row: the spelled-out labels are far wider than the old bare symbols and would run
		// off the panel on a single line.
		for (std::size_t i = 0; i < kPresetButtons.size(); ++i)
		{
			if (i % 2 != 0)
				ImGui::SameLine();
			const PresetButtonSpec &spec = kPresetButtons[i];
			const bool supported = IsPresetSupportedFor(m_System, spec.preset);
			ImGui::BeginDisabled(!supported);
			if (ImGui::RadioButton(spec.label, m_Centering == spec.preset))
				m_Centering = spec.preset;
			ImGui::EndDisabled();
			if (!supported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Not offered for this crystal system");
		}
		// A system switch can invalidate the current choice (e.g. F under Hexagonal); fall back to
		// P rather than building a lattice the system does not admit.
		if (!IsPresetSupportedFor(m_System, m_Centering))
			m_Centering = BravaisCenteringPreset::Primitive;
	}

	void NewStructureWizardPanel::drawElementPickerPopup(const char *popupId, std::string &targetSpecies)
	{
		if (!ImGui::BeginPopup(popupId))
			return;
		const std::string clicked = DrawPeriodicTableGrid(
			m_RendererLayer,
			[&](const std::string &symbol) -> glm::vec3
			{ return CategoryColor(ClassifyElement(AtomicNumberForSymbol(m_RendererLayer, symbol))); },
			targetSpecies);
		if (!clicked.empty())
		{
			targetSpecies = clicked;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void NewStructureWizardPanel::drawBasisTable()
	{
		ImGui::TextUnformatted("Atomic basis (motif)");
		ImGui::SameLine();
		ImGui::TextDisabled("- the atoms on ONE lattice point, fractional 0 to 1");
		int rowToRemove = -1;
		if (ImGui::BeginTable(
				"##BasisRows", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			// One shared width for x/y/z: stretch columns give three different widths for three
			// fields that hold the same kind of number, which reads as if they were different fields.
			const float coordinateWidth = ImGui::CalcTextSize("-0.00000").x + ImGui::GetStyle().FramePadding.x * 4.0f;
			ImGui::TableSetupColumn("Element", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("(select)").x + 16.0f);
			ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, coordinateWidth);
			ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, coordinateWidth);
			ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_WidthFixed, coordinateWidth);
			ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Remove").x + 16.0f);
			ImGui::TableHeadersRow();

			for (std::size_t i = 0; i < m_BasisRows.size(); ++i)
			{
				BasisRow &row = m_BasisRows[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				if (ImGui::Button(row.species.empty() ? "(select)" : row.species.c_str(), ImVec2(-1.0f, 0.0f)))
					ImGui::OpenPopup("##ElementPicker");
				drawElementPickerPopup("##ElementPicker", row.species);

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##X", &row.fractional.x, 0.005f, -1.0f, 2.0f, "%.4f");

				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##Y", &row.fractional.y, 0.005f, -1.0f, 2.0f, "%.4f");

				ImGui::TableSetColumnIndex(3);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##Z", &row.fractional.z, 0.005f, -1.0f, 2.0f, "%.4f");

				ImGui::TableSetColumnIndex(4);
				if (ImGui::Button("Remove"))
					rowToRemove = static_cast<int>(i);

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (rowToRemove >= 0)
			m_BasisRows.erase(m_BasisRows.begin() + rowToRemove);

		if (ImGui::Button("+ Add row"))
			m_BasisRows.push_back(BasisRow{});

		const CrystalStructure preview = buildStructure();
		const std::vector<std::string> uniqueSpecies = preview.UniqueSpecies();
		std::string formula;
		for (const std::string &symbol : uniqueSpecies)
		{
			if (!formula.empty())
				formula += " ";
			formula += symbol;
		}
		ImGui::TextDisabled(
			"%zu basis row(s) -> %zu atom(s) - %s",
			m_BasisRows.size(),
			preview.atoms.size(),
			formula.empty() ? "(empty)" : formula.c_str());
	}

	void NewStructureWizardPanel::drawGeneratedAtomsSection()
	{
		const CrystalStructure expanded = buildStructure();
		// Overrides are indexed by generated-atom index, and both the basis rows and the centering
		// move those indices around. A size change drops them wholesale rather than re-attaching an
		// element to a site the user never picked it for.
		if (m_SpeciesOverrides.size() != expanded.atoms.size())
			m_SpeciesOverrides.assign(expanded.atoms.size(), std::string());
		const std::vector<std::size_t> coincident = FindCoincidentAtomIndices(expanded.atoms);
		if (!coincident.empty())
		{
			// Reported, never deduplicated: the user may be mid-edit, and deleting the atom they are
			// typing is worse than showing them the collision.
			ImGui::TextColored(
				ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%zu atom(s) share a site with another", coincident.size());
		}

		if (!ImGui::CollapsingHeader("Generated atoms"))
			return;

		ImGui::TextDisabled("%zu atom(s) - this is what reaches the renderer and POSCAR", expanded.atoms.size());
		ImGui::TextDisabled("Click an element to change that one site (diamond = F centering, every atom C).");

		const bool anyOverride = std::any_of(
			m_SpeciesOverrides.begin(), m_SpeciesOverrides.end(), [](const std::string &s) { return !s.empty(); });
		ImGui::BeginDisabled(!anyOverride);
		if (ImGui::Button("Reset elements to the basis"))
			m_SpeciesOverrides.assign(expanded.atoms.size(), std::string());
		ImGui::EndDisabled();
		if (anyOverride)
		{
			// Said out loud because the radio group upstairs still reads F/I/C: once the translated
			// copies carry different elements, the translation is no longer a lattice vector.
			ImGui::TextColored(
				ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
				"Per-site elements set - the lattice is primitive now, whatever the centering says.");
		}

		if (!ImGui::BeginTable(
				"##GeneratedAtoms", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			return;

		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000").x + 12.0f);
		ImGui::TableSetupColumn("Element");
		ImGui::TableSetupColumn("x");
		ImGui::TableSetupColumn("y");
		ImGui::TableSetupColumn("z");
		ImGui::TableHeadersRow();
		for (std::size_t i = 0; i < expanded.atoms.size(); ++i)
		{
			const AtomSite &atom = expanded.atoms[i];
			const bool collides = std::binary_search(coincident.begin(), coincident.end(), i);
			ImGui::PushID(static_cast<int>(i));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%zu", i);
			ImGui::TableSetColumnIndex(1);
			// The button carries the EFFECTIVE species (override or basis row), so a site reads the
			// same whether it was typed here or upstairs; the picker writes the override.
			const std::string label = collides ? atom.species + " (!)" : atom.species;
			if (collides)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
			if (ImGui::Button(label.empty() ? "(select)" : label.c_str(), ImVec2(-1.0f, 0.0f)))
				ImGui::OpenPopup("##ElementPicker");
			if (collides)
				ImGui::PopStyleColor();
			drawElementPickerPopup("##ElementPicker", m_SpeciesOverrides[i]);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.4f", atom.fractional.x);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.4f", atom.fractional.y);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%.4f", atom.fractional.z);
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	void NewStructureWizardPanel::drawBondSection()
	{
		if (!ImGui::CollapsingHeader("Bonds"))
			return;

		// Species of the DRAFT, not of a domain structure: the "Detected in this structure" table is
		// the useful half of the editor, and it needs the list the preview panes are about to bond.
		std::vector<std::string> species;
		const auto add = [&species](const std::string &symbol) {
			if (!symbol.empty() && std::find(species.begin(), species.end(), symbol) == species.end())
				species.push_back(symbol);
		};
		for (const BasisRow &row : m_BasisRows)
			add(row.species);
		for (const std::string &override_ : m_SpeciesOverrides)
			add(override_);
		std::sort(species.begin(), species.end());

		// The return value is ignored on purpose: there is nothing to rebuild on demand here. The
		// settings go into the draft on the next buildStructure(), the preview signature notices,
		// and the three panes re-bond themselves.
		DrawBondSettingsEditor(m_BondSettings, species, m_ElementPropertiesTable, m_RendererLayer, m_BondEditorState);
	}
} // namespace DefectStudio
