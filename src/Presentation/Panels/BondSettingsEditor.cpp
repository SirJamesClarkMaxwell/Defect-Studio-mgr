// The bond cutoff editor, lifted out of BondSettingsPanel so the New Structure wizard can show the
// same block: a draft has no domain record, and the panel resolves its target through
// ResolveAtomEditTarget, which refuses a window without one.
#include "Core/dspch.hpp"

#include "Presentation/Panels/BondSettingsEditor.hpp"

#include <cstdio>

#include <imgui.h>

#include "Domain/Crystal/BondGenerator.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		// The stored setting is a SCALE (cutoff = scale x sum of covalent radii) because that is the
		// only thing BondGenerator reads. Nobody thinks in scales though - "C-C bonds up to 1.8 A" is
		// the actual question - so the editor converts both ways through this and leaves the stored
		// form alone. 0 for an unparsable pair key, which callers treat as "no angstrom input possible".
		[[nodiscard]] float RadiusSumForPair(const std::string &pairKey, const ElementPropertiesTable &table)
		{
			const std::size_t separator = pairKey.find('-');
			if (separator == std::string::npos)
				return 0.0f;
			return table.Get(pairKey.substr(0, separator)).covalentRadius +
				table.Get(pairKey.substr(separator + 1)).covalentRadius;
		}

		void DrawPairPickerPopup(
			const char *popupId, char *targetBuffer, std::size_t targetSize, RendererLayer &layer)
		{
			if (!ImGui::BeginPopup(popupId))
				return;
			const std::string clicked = DrawPeriodicTableGrid(
				layer,
				[&](const std::string &symbol) -> glm::vec3
				{ return CategoryColor(ClassifyElement(AtomicNumberForSymbol(layer, symbol))); },
				std::string(targetBuffer));
			if (!clicked.empty())
			{
				std::snprintf(targetBuffer, targetSize, "%s", clicked.c_str());
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	} // namespace

	bool DrawBondSettingsEditor(
		BondGenerationSettings &settings,
		const std::vector<std::string> &presentSpecies,
		const ElementPropertiesTable &elementPropertiesTable,
		RendererLayer &layer,
		BondSettingsEditorState &state)
	{
		bool edited = false;

		ImGui::TextWrapped(
			"A bond forms when two atoms are closer than their pair's max bond length. Set that length "
			"directly per pair below, or leave a pair on the global scale x (covalent radius A + "
			"covalent radius B).");

		ImGui::SetNextItemWidth(160.0f);
		ImGui::DragFloat("Global cutoff scale", &settings.globalCutoffScale, 0.01f, 0.5f, 3.0f, "%.2f");
		edited = ImGui::IsItemDeactivatedAfterEdit() || edited;
		ImGui::SameLine();
		ImGui::TextDisabled("(used for any pair without an override below)");

		ImGui::Separator();
		ImGui::TextUnformatted("Per-pair overrides");

		std::string pairToRemove;
		if (ImGui::BeginTable(
				"##BondPairOverrides", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Pair", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Max bond length");
			ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableHeadersRow();

			for (auto &[pairKey, scale] : settings.perPairCutoffOverride)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(pairKey.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::PushID(pairKey.c_str());
				const float radiusSum = RadiusSumForPair(pairKey, elementPropertiesTable);
				if (radiusSum > 0.0f)
				{
					float cutoff = scale * radiusSum;
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::DragFloat("##Cutoff", &cutoff, 0.005f, 0.1f * radiusSum, 5.0f * radiusSum, "%.3f A"))
						scale = cutoff / radiusSum;
					edited = ImGui::IsItemDeactivatedAfterEdit() || edited;
				}
				else
				{
					// An element this build has no covalent radius for - the scale is still editable,
					// there is just nothing to convert it against.
					ImGui::TextDisabled("(unknown radii)");
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##Scale", &scale, 0.01f, 0.1f, 5.0f, "%.2f");
				edited = ImGui::IsItemDeactivatedAfterEdit() || edited;
				ImGui::TableSetColumnIndex(3);
				if (ImGui::Button("Remove"))
					pairToRemove = pairKey;
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (!pairToRemove.empty())
		{
			settings.perPairCutoffOverride.erase(pairToRemove);
			edited = true;
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Add override");
		ImGui::SetNextItemWidth(60.0f);
		ImGui::InputText("##PairFirst", state.newPairFirst, sizeof(state.newPairFirst));
		ImGui::SameLine();
		if (ImGui::SmallButton("...##ChooseFirst"))
			ImGui::OpenPopup("##ChoosePairFirst");
		DrawPairPickerPopup("##ChoosePairFirst", state.newPairFirst, sizeof(state.newPairFirst), layer);
		ImGui::SameLine();
		ImGui::TextUnformatted("-");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(60.0f);
		ImGui::InputText("##PairSecond", state.newPairSecond, sizeof(state.newPairSecond));
		ImGui::SameLine();
		if (ImGui::SmallButton("...##ChooseSecond"))
			ImGui::OpenPopup("##ChoosePairSecond");
		DrawPairPickerPopup("##ChoosePairSecond", state.newPairSecond, sizeof(state.newPairSecond), layer);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("##NewPairScale", &state.newPairScale, 0.01f, 0.1f, 5.0f, "%.2f");
		ImGui::SameLine();
		const bool canAddPair = state.newPairFirst[0] != '\0' && state.newPairSecond[0] != '\0';
		ImGui::BeginDisabled(!canAddPair);
		if (ImGui::Button("Add"))
		{
			settings.perPairCutoffOverride[BondPairKey(state.newPairFirst, state.newPairSecond)] = state.newPairScale;
			state.newPairFirst[0] = '\0';
			state.newPairSecond[0] = '\0';
			edited = true;
		}
		ImGui::EndDisabled();

		// Auto-detected pairs actually present in this structure - lets the user see real cutoff
		// distances and edit one without having to guess/type species symbols by hand.
		if (presentSpecies.empty())
			return edited;

		ImGui::Separator();
		ImGui::TextUnformatted("Detected in this structure");
		if (!ImGui::BeginTable(
				"##DetectedPairs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			return edited;

		ImGui::TableSetupColumn("Pair", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Max bond length");
		ImGui::TableSetupColumn("##AddDetected", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableHeadersRow();

		for (std::size_t a = 0; a < presentSpecies.size(); ++a)
		{
			for (std::size_t b = a; b < presentSpecies.size(); ++b)
			{
				const std::string pairKey = BondPairKey(presentSpecies[a], presentSpecies[b]);
				const auto override_ = settings.perPairCutoffOverride.find(pairKey);
				const bool hasOverride = override_ != settings.perPairCutoffOverride.end();
				const float scale = hasOverride ? override_->second : settings.globalCutoffScale;
				const float radiusSum = elementPropertiesTable.Get(presentSpecies[a]).covalentRadius +
					elementPropertiesTable.Get(presentSpecies[b]).covalentRadius;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(pairKey.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::PushID(pairKey.c_str());
				// Editable in place: dragging the length of a pair that is actually in the structure
				// is the whole workflow ("these two should not be bonded, cut it to 1.5 A"), and
				// having to press Add override first was pure ceremony.
				float editedCutoff = scale * radiusSum;
				ImGui::SetNextItemWidth(120.0f);
				ImGui::BeginDisabled(radiusSum <= 0.0f);
				if (ImGui::DragFloat("##Cutoff", &editedCutoff, 0.005f, 0.1f * radiusSum, 5.0f * radiusSum, "%.3f A") &&
					radiusSum > 0.0f)
					settings.perPairCutoffOverride[pairKey] = editedCutoff / radiusSum;
				ImGui::EndDisabled();
				edited = ImGui::IsItemDeactivatedAfterEdit() || edited;
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)", hasOverride ? "override" : "global scale");
				ImGui::TableSetColumnIndex(2);
				ImGui::BeginDisabled(hasOverride);
				if (ImGui::SmallButton("Add override"))
				{
					settings.perPairCutoffOverride[pairKey] = settings.globalCutoffScale;
					edited = true;
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
		return edited;
	}
} // namespace DefectStudio
