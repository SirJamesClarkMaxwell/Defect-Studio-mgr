#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"

#include <algorithm>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "Domain/Electronic/HydrogenicOrbital.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Presentation/Panels/SceneOrientationControls.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		using SceneOrbital = RendererWindowState::SceneOrbital;

		// Work on a local copy so activation snapshots the value from before ImGui applies the first
		// click/drag delta. One activation is one global Undo entry, matching the arrow editor.
		template <typename T, typename WidgetFn>
		bool DrawUndoableValue(RendererWindowState &windowState, T &modelValue, WidgetFn &&widget)
		{
			T edited = modelValue;
			const bool changed = widget(edited);
			if (ImGui::IsItemActivated())
				PushPinnedMeasurementUndoSnapshot(windowState);
			if (changed)
				modelValue = edited;
			return changed;
		}

		void DrawPresetCombo(RendererWindowState &windowState, SceneOrbital &orbital)
		{
			if (!ImGui::BeginCombo("Preset", OrbitalPresetDisplayName(orbital.preset)))
				return;
			// Same five drawers as the Add menu, from the same source - see HydrogenicOrbital.hpp.
			for (const OrbitalPresetGroup group : AllOrbitalPresetGroups())
			{
				ImGui::SeparatorText(OrbitalPresetGroupName(group));
				for (const OrbitalPreset preset : OrbitalPresetsInGroup(group))
				{
					const bool selected = preset == orbital.preset;
					if (ImGui::Selectable(OrbitalPresetDisplayName(preset), selected) && !selected)
					{
						PushPinnedMeasurementUndoSnapshot(windowState);
						orbital.preset = preset;
						orbital.lobeIndex = std::clamp(orbital.lobeIndex, 0, OrbitalPresetMemberCount(preset) - 1);
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
				const std::size_t required = IsTwoCenterPreset(orbital.preset) ? 2u : 1u;
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
		const bool lcao = !orbital.lcaoComponents.empty();
		if (!orbital.displayName.empty())
			ImGui::TextUnformatted(orbital.displayName.c_str());
		if (lcao && ImGui::CollapsingHeader("Składowe", kOpen) &&
			ImGui::BeginTable("##LcaoComponents", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Atom");
			ImGui::TableSetupColumn("Funkcja");
			ImGui::TableSetupColumn("Współczynnik");
			ImGui::TableHeadersRow();
			for (const auto &component : orbital.lcaoComponents)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (component.anchorAtom < windowState.structure.atoms.size())
					ImGui::Text("%s #%zu", windowState.structure.atoms[component.anchorAtom].element.c_str(), component.anchorAtom + 1);
				else
					ImGui::TextDisabled("Atom (missing)");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(OrbitalPresetMemberDisplayName(component.preset, component.lobeIndex));
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.4f", component.coefficient);
			}
			ImGui::EndTable();
		}

		if (!lcao && ImGui::CollapsingHeader("Ksztalt##OrbitalShape", kOpen))
		{
			DrawPresetCombo(windowState, orbital);

			if (DrawUndoableValue(windowState, orbital.shell, [](int &value) {
					return ImGui::SliderInt("Powloka (n)", &value, 1, 5);
				}))
				orbital.shell = std::clamp(orbital.shell, 1, 5);

			// A number told you nothing: for a hybrid it picked one of 2/3/4 lobes, for a pi or a
			// delta one of two degenerate perpendiculars, and for an s or a sigma nothing at all.
			// The names come from the physics (HydrogenicOrbital.hpp), so they cannot drift from
			// what the preset actually builds - a test pins that down.
			const int memberCount = OrbitalPresetMemberCount(orbital.preset);
			orbital.lobeIndex = std::clamp(orbital.lobeIndex, 0, std::max(0, memberCount - 1));
			ImGui::BeginDisabled(memberCount <= 1);
			if (ImGui::BeginCombo("Czlon", OrbitalPresetMemberDisplayName(orbital.preset, orbital.lobeIndex)))
			{
				for (int lobe = 0; lobe < memberCount; ++lobe)
				{
					const bool selected = lobe == orbital.lobeIndex;
					if (ImGui::Selectable(OrbitalPresetMemberDisplayName(orbital.preset, lobe), selected) && !selected)
					{
						PushPinnedMeasurementUndoSnapshot(windowState);
						orbital.lobeIndex = lobe;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			if (memberCount <= 1 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Ten preset ma tylko jeden czlon.");

			DrawUndoableValue(windowState, orbital.effectiveCharge, [](float &value) {
				return ImGui::DragFloat("Z_eff", &value, 0.05f, 0.1f, 30.0f, "%.2f");
			});
			ImGui::SetItemTooltip(
				"Ladunek efektywny. To jest pokretlo, ktore naprawde sciaga lub rozdyma funkcje falowa "
				"- w odroznieniu od skali nizej, ktora tylko powieksza rysunek.");
		}

		if (!lcao && ImGui::CollapsingHeader("Polozenie##OrbitalPlacement", kOpen))
		{
			DrawAnchoring(windowState, orbital);
			const bool anchored = !orbital.anchorAtoms.empty();
			ImGui::BeginDisabled(anchored);
			DrawUndoableValue(windowState, orbital.centerA, [](glm::vec3 &value) {
				return ImGui::DragFloat3("Środek A", &value.x, 0.05f);
			});
			if (IsTwoCenterPreset(orbital.preset))
			{
				DrawUndoableValue(windowState, orbital.centerB, [](glm::vec3 &value) {
					return ImGui::DragFloat3("Środek B", &value.x, 0.05f);
				});
			}
			ImGui::EndDisabled();

			// Two-centre presets take their orientation from centerB - centerA, so a rotation here
			// would be silently ignored rather than doing nothing visible for a reason.
			ImGui::BeginDisabled(IsTwoCenterPreset(orbital.preset));
			DrawUndoableValue(windowState, orbital.rotationEuler, [](glm::vec3 &value) {
				return ImGui::DragFloat3("Obrot (stopnie)", &value.x, 1.0f);
			});
			ImGui::EndDisabled();
			DrawSceneOrbitalAimControls(windowState, {orbital.id});
			DrawSceneAxisAlignmentControls(windowState);
		}

		if (ImGui::CollapsingHeader("Wyglad##OrbitalAppearance", kOpen))
		{
			DrawUndoableValue(windowState, orbital.phaseFlipped, [](bool &value) {
				return ImGui::Checkbox("Odwroc faze", &value);
			});
			DrawUndoableValue(windowState, orbital.positiveLobeColor, [](glm::vec3 &value) {
				return ImGui::ColorEdit3("Faza +", &value.x);
			});
			DrawUndoableValue(windowState, orbital.negativeLobeColor, [](glm::vec3 &value) {
				return ImGui::ColorEdit3("Faza -", &value.x);
			});
			DrawUndoableValue(windowState, orbital.alpha, [](float &value) {
				return ImGui::SliderFloat("Przezroczystość", &value, 0.05f, 1.0f, "%.2f");
			});

			if (DrawUndoableValue(windowState, orbital.isoFraction, [](float &value) {
					return ImGui::SliderFloat("Izopowierzchnia", &value, 0.02f, 0.9f, "%.2f");
				}))
				orbital.isoFraction = std::clamp(orbital.isoFraction, 0.01f, 0.95f);
			ImGui::SetItemTooltip("Ulamek szczytowej amplitudy, nie wartosc bezwzgledna - dzieki temu "
								  "rozmyte 3d i ciasne 1s wygladaja jak w podreczniku przy tym samym ustawieniu.");

			if (DrawUndoableValue(windowState, orbital.resolution, [](int &value) {
					return ImGui::SliderInt("Rozdzielczosc", &value, 16, 96);
				}))
				orbital.resolution = std::clamp(orbital.resolution, 8, 128);
			ImGui::SetItemTooltip("Probki na os. Siatka liczy sie na glownym watku, wiec to jest pokretlo "
								  "od przyciec przy przeciaganiu.");

			ImGui::SeparatorText("Korekta rysunku");
			ImGui::TextDisabled("Te ustawienia zmieniaja rysunek, nie fizyke orbitalu.");
			DrawUndoableValue(windowState, orbital.scale, [](float &value) {
				return ImGui::DragFloat("Skala rysunku", &value, 0.02f, 0.05f, 20.0f, "%.2f");
			});

			float width = (orbital.stretch.x + orbital.stretch.y) * 0.5f;
			if (DrawUndoableValue(windowState, width, [](float &value) {
					return ImGui::DragFloat(
						"Szerokosc", &value, 0.01f, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				}))
				orbital.stretch.x = orbital.stretch.y = width;
			DrawUndoableValue(windowState, orbital.stretch.z, [](float &value) {
				return ImGui::DragFloat(
					"Dlugosc", &value, 0.01f, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			});

			if (ImGui::CollapsingHeader("Zaawansowane##OrbitalStretch"))
			{
				DrawUndoableValue(windowState, orbital.stretch.x, [](float &value) {
					return ImGui::DragFloat("X##OrbitalStretch", &value, 0.01f, 0.1f, 5.0f, "%.2f",
						ImGuiSliderFlags_AlwaysClamp);
				});
				DrawUndoableValue(windowState, orbital.stretch.y, [](float &value) {
					return ImGui::DragFloat("Y##OrbitalStretch", &value, 0.01f, 0.1f, 5.0f, "%.2f",
						ImGuiSliderFlags_AlwaysClamp);
				});
				DrawUndoableValue(windowState, orbital.stretch.z, [](float &value) {
					return ImGui::DragFloat("Z##OrbitalStretch", &value, 0.01f, 0.1f, 5.0f, "%.2f",
						ImGuiSliderFlags_AlwaysClamp);
				});
			}

			DrawUndoableValue(windowState, orbital.visible, [](bool &value) {
				return ImGui::Checkbox("Widoczny", &value);
			});
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
		const bool erasedAny = removed != windowState.sceneOrbitals.end();
		if (erasedAny)
			windowState.sceneOrbitals.erase(removed, windowState.sceneOrbitals.end());
		windowState.selectedSceneOrbitals.clear();
		if (erasedAny)
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
	}

	void EraseScenePlanes(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids)
	{
		if (ids.empty())
			return;
		const auto removed = std::remove_if(
			windowState.scenePlanes.begin(), windowState.scenePlanes.end(),
			[&ids](const RendererWindowState::ScenePlane &plane)
			{ return std::find(ids.begin(), ids.end(), plane.id) != ids.end(); });
		if (removed != windowState.scenePlanes.end())
			windowState.scenePlanes.erase(removed, windowState.scenePlanes.end());
		windowState.selectedScenePlanes.clear();
		windowState.selectedScenePaths.clear();
	}

	void DrawAllSceneOrbitalRows(RendererWindowState &windowState)
	{
		ImGui::Text("Orbitale (%zu)", windowState.sceneOrbitals.size());
		if (windowState.sceneOrbitals.empty())
		{
			ImGui::TextDisabled("Prawy przycisk w widoku > Add > Orbital.");
			return;
		}

		ImGui::PushID("AllOrbitals");
		std::vector<SceneObjectId> toRemove;
		for (std::size_t index = 0; index < windowState.sceneOrbitals.size(); ++index)
		{
			SceneOrbital &orbital = windowState.sceneOrbitals[index];
			ImGui::PushID(static_cast<int>(index));

			auto &selection = windowState.selectedSceneOrbitals;
			const bool selected = std::find(selection.begin(), selection.end(), orbital.id) != selection.end();
			const std::string label = orbital.displayName.empty()
				? std::string(OrbitalPresetName(orbital.preset)) + " #" + std::to_string(index) : orbital.displayName;
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

			ImGui::PopID();
		}
		ImGui::PopID();

		if (toRemove.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		EraseSceneOrbitals(windowState, toRemove);
	}

	void DrawScenePlaneEditor(RendererWindowState &windowState, const std::size_t index)
	{
		if (index >= windowState.scenePlanes.size())
			return;
		RendererWindowState::ScenePlane &plane = windowState.scenePlanes[index];
		// The resolver owns an anchored plane's frame; accepting edits here would only record an
		// undo snapshot and then snap the controls back on the next frame.
		ImGui::BeginDisabled(!plane.anchorAtoms.empty());
		DrawUndoableValue(windowState, plane.center, [](glm::vec3 &value) {
			return ImGui::DragFloat3("Środek", &value.x, 0.05f);
		});

		ImGui::EndDisabled();
		DrawScenePlaneRotationControls(windowState, {plane.id});
		DrawSceneDefectPlacementControls(windowState, true);
		DrawSceneAxisAlignmentControls(windowState);
		DrawUndoableValue(windowState, plane.halfExtents, [](glm::vec2 &value) {
			return ImGui::DragFloat2("Połowa rozmiaru", &value.x, 0.05f, 0.01f, 1000.0f, "%.2f");
		});
		DrawUndoableValue(windowState, plane.color, [](glm::vec3 &value) {
			return ImGui::ColorEdit3("Kolor", &value.x);
		});
		DrawUndoableValue(windowState, plane.alpha, [](float &value) {
			return ImGui::SliderFloat("Przezroczystość", &value, 0.02f, 1.0f, "%.2f");
		});
		DrawUndoableValue(windowState, plane.showBorder, [](bool &value) {
			return ImGui::Checkbox("Ramka", &value);
		});
		ImGui::SameLine();
		DrawUndoableValue(windowState, plane.visible, [](bool &value) {
			return ImGui::Checkbox("Widoczna", &value);
		});
	}

	void DrawAllScenePlaneRows(RendererWindowState &windowState)
	{
		ImGui::Text("Płaszczyzny (%zu)", windowState.scenePlanes.size());
		if (windowState.scenePlanes.empty())
		{
			ImGui::TextDisabled("Zaznacz dwa atomy lub więcej, potem prawy przycisk > Dodaj > Płaszczyzna.");
			return;
		}

		ImGui::PushID("AllPlanes");
		std::vector<SceneObjectId> toRemove;
		for (std::size_t index = 0; index < windowState.scenePlanes.size(); ++index)
		{
			RendererWindowState::ScenePlane &plane = windowState.scenePlanes[index];
			ImGui::PushID(static_cast<int>(1000 + index));

			auto &selection = windowState.selectedScenePlanes;
			const bool selected = std::find(selection.begin(), selection.end(), plane.id) != selection.end();
			const std::string label = "Płaszczyzna #" + std::to_string(index);
			if (ImGui::Selectable(label.c_str(), selected))
			{
				if (ImGui::GetIO().KeyCtrl)
				{
					const auto found = std::find(selection.begin(), selection.end(), plane.id);
					if (found == selection.end())
						selection.push_back(plane.id);
					else
						selection.erase(found);
				}
				else
				{
					selection = {plane.id};
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X##RemovePlane"))
				toRemove.push_back(plane.id);

			ImGui::PopID();
		}
		ImGui::PopID();

		if (toRemove.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		EraseScenePlanes(windowState, toRemove);
	}
} // namespace DefectStudio
