#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"

#include <algorithm>
#include <string>

#include <imgui.h>

#include "Domain/Electronic/HydrogenicOrbital.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio
{
	namespace
	{
		using SceneOrbital = RendererWindowState::SceneOrbital;

		// ImGui reports "this widget was just released" separately from "its value changed this
		// frame". Snapshotting on activation rather than on every changed frame is what makes one
		// drag of a slider one Undo instead of sixty - the same rule the arrow editor follows.
		void SnapshotOnActivation(RendererWindowState &windowState)
		{
			if (ImGui::IsItemActivated())
				PushPinnedMeasurementUndoSnapshot(windowState);
		}

		// How many members the preset's lobeIndex actually selects, so the control offers the real
		// range instead of letting someone dial a d orbital's lobe 4 on an s.
		[[nodiscard]] int LobeCount(OrbitalPreset preset)
		{
			switch (preset)
			{
				case OrbitalPreset::S: return 1;
				case OrbitalPreset::P: return 3;
				case OrbitalPreset::D: return 5;
				case OrbitalPreset::F: return 7;
				case OrbitalPreset::Sp: return 2;
				case OrbitalPreset::Sp2: return 3;
				case OrbitalPreset::Sp3: return 4;
				case OrbitalPreset::Pi:
				case OrbitalPreset::PiStar:
				case OrbitalPreset::Delta:
				case OrbitalPreset::DeltaStar: return 2;
				default: return 1;
			}
		}

		[[nodiscard]] bool IsTwoCenter(OrbitalPreset preset)
		{
			switch (preset)
			{
				case OrbitalPreset::S:
				case OrbitalPreset::P:
				case OrbitalPreset::D:
				case OrbitalPreset::F:
				case OrbitalPreset::Sp:
				case OrbitalPreset::Sp2:
				case OrbitalPreset::Sp3: return false;
				default: return true;
			}
		}

		void DrawPresetCombo(RendererWindowState &windowState, SceneOrbital &orbital)
		{
			if (!ImGui::BeginCombo("Preset", OrbitalPresetName(orbital.preset)))
				return;
			// Same five drawers as the Add menu, from the same source - see HydrogenicOrbital.hpp.
			for (const OrbitalPresetGroup group : AllOrbitalPresetGroups())
			{
				ImGui::SeparatorText(OrbitalPresetGroupName(group));
				for (const OrbitalPreset preset : OrbitalPresetsInGroup(group))
				{
					const bool selected = preset == orbital.preset;
					if (ImGui::Selectable(OrbitalPresetName(preset), selected) && !selected)
					{
						PushPinnedMeasurementUndoSnapshot(windowState);
						orbital.preset = preset;
						orbital.lobeIndex = std::clamp(orbital.lobeIndex, 0, LobeCount(preset) - 1);
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		void DrawAnchoring(RendererWindowState &windowState, SceneOrbital &orbital)
		{
			if (orbital.anchorAtoms.empty())
			{
				ImGui::TextDisabled("Nie zakotwiczony - stoi tam, gdzie go postawiono.");
				const std::size_t selectedAtoms = windowState.selectedAtomIndices.size();
				const std::size_t required = IsTwoCenter(orbital.preset) ? 2u : 1u;
				ImGui::BeginDisabled(selectedAtoms != required);
				if (ImGui::Button("Zakotwicz na zaznaczonych atomach"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					orbital.anchorAtoms = windowState.selectedAtomIndices;
				}
				ImGui::EndDisabled();
				return;
			}

			std::string description = "Zakotwiczony na: ";
			for (std::size_t i = 0; i < orbital.anchorAtoms.size(); ++i)
			{
				const std::size_t atomIndex = orbital.anchorAtoms[i];
				if (atomIndex >= windowState.structure.atoms.size())
					continue;
				if (i > 0)
					description += ", ";
				description += windowState.structure.atoms[atomIndex].element + " #" + std::to_string(atomIndex);
			}
			ImGui::TextUnformatted(description.c_str());
			if (!ImGui::Button("Odczep"))
				return;
			// Detaching freezes it where it currently sits rather than snapping it back to whatever
			// centerA held before it was anchored - the position on screen is the one the user is
			// looking at, and having it jump on detach would be the surprising half.
			PushPinnedMeasurementUndoSnapshot(windowState);
			const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, windowState.structure);
			orbital.centerA = centers.centerA;
			orbital.centerB = centers.centerB;
			orbital.anchorAtoms.clear();
		}
	} // namespace

	void DrawSceneOrbitalEditor(RendererWindowState &windowState, const std::size_t orbitalIndex)
	{
		if (orbitalIndex >= windowState.sceneOrbitals.size())
			return;
		SceneOrbital &orbital = windowState.sceneOrbitals[orbitalIndex];
		constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_DefaultOpen;

		if (ImGui::CollapsingHeader("Ksztalt##OrbitalShape", kOpen))
		{
			DrawPresetCombo(windowState, orbital);

			if (ImGui::SliderInt("Powloka (n)", &orbital.shell, 1, 5))
				orbital.shell = std::clamp(orbital.shell, 1, 5);
			SnapshotOnActivation(windowState);

			const int lobeCount = LobeCount(orbital.preset);
			ImGui::BeginDisabled(lobeCount <= 1);
			if (ImGui::SliderInt("Platek", &orbital.lobeIndex, 0, std::max(0, lobeCount - 1)))
				orbital.lobeIndex = std::clamp(orbital.lobeIndex, 0, lobeCount - 1);
			SnapshotOnActivation(windowState);
			ImGui::EndDisabled();

			ImGui::DragFloat("Z_eff", &orbital.effectiveCharge, 0.05f, 0.1f, 30.0f, "%.2f");
			SnapshotOnActivation(windowState);
			ImGui::SetItemTooltip(
				"Ladunek efektywny. To jest pokretlo, ktore naprawde sciaga lub rozdyma funkcje falowa "
				"- w odroznieniu od skali nizej, ktora tylko powieksza rysunek.");
		}

		if (ImGui::CollapsingHeader("Polozenie##OrbitalPlacement", kOpen))
		{
			DrawAnchoring(windowState, orbital);
			const bool anchored = !orbital.anchorAtoms.empty();
			ImGui::BeginDisabled(anchored);
			ImGui::DragFloat3("Srodek A", &orbital.centerA.x, 0.05f);
			SnapshotOnActivation(windowState);
			if (IsTwoCenter(orbital.preset))
			{
				ImGui::DragFloat3("Srodek B", &orbital.centerB.x, 0.05f);
				SnapshotOnActivation(windowState);
			}
			ImGui::EndDisabled();

			// Two-centre presets take their orientation from centerB - centerA, so a rotation here
			// would be silently ignored rather than doing nothing visible for a reason.
			ImGui::BeginDisabled(IsTwoCenter(orbital.preset));
			ImGui::DragFloat3("Obrot (stopnie)", &orbital.rotationEuler.x, 1.0f);
			SnapshotOnActivation(windowState);
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Wyglad##OrbitalAppearance", kOpen))
		{
			ImGui::ColorEdit3("Faza +", &orbital.positiveLobeColor.x);
			SnapshotOnActivation(windowState);
			ImGui::ColorEdit3("Faza -", &orbital.negativeLobeColor.x);
			SnapshotOnActivation(windowState);
			ImGui::SliderFloat("Przezroczystosc", &orbital.alpha, 0.05f, 1.0f, "%.2f");
			SnapshotOnActivation(windowState);
			ImGui::DragFloat("Skala rysunku", &orbital.scale, 0.02f, 0.05f, 20.0f, "%.2f");
			SnapshotOnActivation(windowState);
			ImGui::SetItemTooltip("Powieksza siatke, nie zmienia fizyki. Od tego jest Z_eff.");

			if (ImGui::SliderFloat("Izopowierzchnia", &orbital.isoFraction, 0.02f, 0.9f, "%.2f"))
				orbital.isoFraction = std::clamp(orbital.isoFraction, 0.01f, 0.95f);
			SnapshotOnActivation(windowState);
			ImGui::SetItemTooltip("Ulamek szczytowej amplitudy, nie wartosc bezwzgledna - dzieki temu "
								  "rozmyte 3d i ciasne 1s wygladaja jak w podreczniku przy tym samym ustawieniu.");

			if (ImGui::SliderInt("Rozdzielczosc", &orbital.resolution, 16, 96))
				orbital.resolution = std::clamp(orbital.resolution, 8, 128);
			SnapshotOnActivation(windowState);
			ImGui::SetItemTooltip("Probki na os. Siatka liczy sie na glownym watku, wiec to jest pokretlo "
								  "od przyciec przy przeciaganiu.");

			ImGui::Checkbox("Widoczny", &orbital.visible);
			SnapshotOnActivation(windowState);
		}
	}

	void EraseSceneOrbitals(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids)
	{
		if (ids.empty())
			return;
		const auto removed = std::remove_if(
			windowState.sceneOrbitals.begin(), windowState.sceneOrbitals.end(),
			[&ids](const SceneOrbital &orbital)
			{ return std::find(ids.begin(), ids.end(), orbital.id) != ids.end(); });
		if (removed == windowState.sceneOrbitals.end())
			return;
		windowState.sceneOrbitals.erase(removed, windowState.sceneOrbitals.end());
		windowState.selectedSceneOrbitals.clear();
	}

	void DrawSceneOrbitalSection(RendererWindowState &windowState)
	{
		ImGui::Separator();
		ImGui::Text("Orbitale (%zu)", windowState.sceneOrbitals.size());
		if (windowState.sceneOrbitals.empty())
		{
			ImGui::TextDisabled("Prawy przycisk w widoku > Add > Orbital.");
			return;
		}

		std::vector<SceneObjectId> toRemove;
		for (std::size_t index = 0; index < windowState.sceneOrbitals.size(); ++index)
		{
			SceneOrbital &orbital = windowState.sceneOrbitals[index];
			ImGui::PushID(static_cast<int>(index));

			auto &selection = windowState.selectedSceneOrbitals;
			const bool selected = std::find(selection.begin(), selection.end(), orbital.id) != selection.end();
			const std::string label = std::string(OrbitalPresetName(orbital.preset)) + " #" + std::to_string(index);
			if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
			{
				// Same clear+select / Ctrl-toggle rule as clicking one in the viewport.
				if (ImGui::GetIO().KeyCtrl)
				{
					const auto found = std::find(selection.begin(), selection.end(), orbital.id);
					if (found == selection.end())
						selection.push_back(orbital.id);
					else
						selection.erase(found);
				}
				else
				{
					selection = {orbital.id};
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X##RemoveOrbital"))
				toRemove.push_back(orbital.id);

			if (selected)
				DrawSceneOrbitalEditor(windowState, index);

			ImGui::PopID();
		}

		if (toRemove.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		EraseSceneOrbitals(windowState, toRemove);
	}
} // namespace DefectStudio
