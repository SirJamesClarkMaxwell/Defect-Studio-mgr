#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanelSections.hpp"
#include "Presentation/Panels/ObjectPropertiesLabelStyle.hpp"
#include "Presentation/Panels/ViewportTextEditor.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <imgui.h>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/SceneObjectMultiSelection.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	template <typename T>
	[[nodiscard]] std::size_t FindObjectIndex(const std::vector<T> &objects, const SceneObjectId id)
	{
		const auto found = std::find_if(objects.begin(), objects.end(), [id](const T &object) { return object.id == id; });
		return found == objects.end() ? objects.size() : static_cast<std::size_t>(std::distance(objects.begin(), found));
	}

	template <typename Object, typename Value, typename WidgetFn>
	bool DrawSelectedSharedValue(
		RendererWindowState &windowState, std::vector<Object> &objects,
		const std::vector<SceneObjectId> &selection, const std::size_t representativeIndex,
		Value Object::*field, WidgetFn &&widget)
	{
		Value edited = objects[representativeIndex].*field;
		const bool changed = widget(edited);
		if (ImGui::IsItemActivated())
			PushPinnedMeasurementUndoSnapshot(windowState);
		if (!changed)
			return false;
		objects[representativeIndex].*field = edited;
		ApplySelectedSceneObjectField(objects, selection, representativeIndex, field);
		return true;
	}

	static void DrawPlaneAnchoring(
		RendererWindowState &windowState, RendererWindowState::ScenePlane &plane)
	{
		if (plane.anchorAtoms.empty())
		{
			ImGui::TextDisabled("Nie zakotwiczona - stoi tam, gdzie ja postawiono.");
			return;
		}

		std::string description = "Zakotwiczona na: ";
		bool first = true;
		for (const std::size_t atomIndex : plane.anchorAtoms)
		{
			if (atomIndex >= windowState.structure.atoms.size())
				continue;
			if (!first)
				description += ", ";
			description += windowState.structure.atoms[atomIndex].element + " #" + std::to_string(atomIndex);
			first = false;
		}
		ImGui::TextUnformatted(description.c_str());
		if (!ImGui::Button("Odczep"))
			return;

		// Resolve immediately so detaching between frames freezes the same quad the user sees.
		PushPinnedMeasurementUndoSnapshot(windowState);
		ResolveAnchoredScenePlanes(windowState);
		plane.anchorAtoms.clear();
	}


	namespace
	{
		// Window-wide bond label layout: auto-offset toward the structure centre and the angle past
		// which a bond-aligned label turns back to the camera.
		void DrawBondLabelLayoutRows(RendererWindowState &windowState)
		{
			ImGui::Checkbox("Auto-offset##BondLabelAutoOffset", &windowState.bondLabelAutoOffsetEnabled);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			ImGui::SliderFloat(
				"Magnitude##BondLabelAutoOffsetMagnitude", &windowState.bondLabelAutoOffsetMagnitude, 0.0f, 2.0f,
				"%.2f A");
			ImGui::SetNextItemWidth(120.0f);
			ImGui::SliderFloat(
				"Align threshold##BondLabelAlignThreshold", &windowState.bondLabelAlignThresholdDeg, 0.0f, 90.0f,
				"%.0f deg (90 = off)");
		}

		// Placement of the selected pins: live while dragging, one undo snapshot when an edit starts.
		void DrawPinPlacementRows(RendererWindowState &windowState)
		{
			std::vector<RendererWindowState::PinnedMeasurement *> pins;
			for (const SceneObjectId id : windowState.selectedPinnedMeasurements)
			{
				const std::size_t index = FindObjectIndex(windowState.pinnedMeasurements, id);
				if (index < windowState.pinnedMeasurements.size())
					pins.push_back(&windowState.pinnedMeasurements[index]);
			}
			if (pins.empty())
				return;
			RendererWindowState::PinnedMeasurement &first = *pins.front();
			ImGui::SeparatorText("Placement");
			const bool anyBond = std::any_of(pins.begin(), pins.end(), [](const auto *pin) { return pin->atomIndices.size() == 2; });
			if (anyBond)
			{
				bool align = first.alignToBondDirection;
				if (ImGui::Checkbox("Align to bond##PinAlignAll", &align))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					for (auto *pin : pins)
						pin->alignToBondDirection = align;
				}
				ImGui::SameLine();
				bool flipped = first.flipped;
				if (ImGui::Checkbox("Flip (F)##PinFlipAll", &flipped))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					for (auto *pin : pins)
						pin->flipped = flipped;
				}
			}
			glm::vec3 offset = first.worldOffset;
			if (ImGui::DragFloat3("Offset (A)##PinOffset", &offset.x, 0.01f, -20.0f, 20.0f, "%.2f"))
				for (auto *pin : pins)
					pin->worldOffset = offset;
			if (ImGui::IsItemActivated())
				PushPinnedMeasurementUndoSnapshot(windowState);
			ImGui::SameLine();
			if (ImGui::SmallButton("0##PinOffsetReset"))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				for (auto *pin : pins)
					pin->worldOffset = glm::vec3(0.0f);
			}
			float degrees = glm::degrees(first.rotationOffsetRadians);
			if (ImGui::SliderFloat("Rotation##PinRotation", &degrees, -180.0f, 180.0f, "%.0f deg"))
				for (auto *pin : pins)
					pin->rotationOffsetRadians = glm::radians(degrees);
			if (ImGui::IsItemActivated())
				PushPinnedMeasurementUndoSnapshot(windowState);
			if (anyBond)
			{
				ImGui::TextDisabled("All bond labels in this view:");
				DrawBondLabelLayoutRows(windowState);
			}
			ImGui::TextDisabled("G / R / S or the gizmo also move, turn and resize them.");
		}
	} // namespace

	void DrawSelectedLabelProperties(RendererWindowState &windowState)
	{
		ImGui::Separator();
		ImGui::Text("Selected labels");
		const std::size_t pinCount = windowState.selectedPinnedMeasurements.size();
		const std::size_t freeCount = windowState.selectedFreeLabels.size();

		if (pinCount == 1 && freeCount == 0)
		{
			RendererWindowState::PinnedMeasurement &pin =
				windowState.pinnedMeasurements[FindObjectIndex(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements[0])];
			ImGui::TextUnformatted(pin.atomIndices.size() == 2 ? "Bond length" : "Angle");
			if (pin.linkBroken)
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f), "link broken");
			if (pin.atomIndices.size() == 2)
			{
				ImGui::SameLine();
				if (ImGui::Button("Align to camera##PinAlignToCamera"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					AlignBondLabelToCamera(
						windowState,
						FindObjectIndex(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements[0]));
				}
			}
		}
		else
		{
			ImGui::Text("%zu label(s) selected - style below applies to all of them", pinCount + freeCount);
		}
		DrawSelectedFreeLabelTextProperties(windowState);
		DrawPinPlacementRows(windowState);
		ImGui::SeparatorText("Style");

		if (ImGui::Button("Copy Style##LabelStyleCopy"))
		{
			CopyLabelStyle(pinCount > 0
				? windowState.pinnedMeasurements[FindObjectIndex(
					windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements[0])].style
				: windowState.freeLabels[FindObjectIndex(
					windowState.freeLabels, windowState.selectedFreeLabels[0])].style);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!GetLabelStyleClipboard().has_value());
		if (ImGui::Button("Paste Style##LabelStylePaste"))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			PasteLabelStyle(windowState, windowState.selectedPinnedMeasurements, windowState.selectedFreeLabels);
		}
		ImGui::EndDisabled();

		const bool usedPinAsRepresentative = pinCount > 0;
		RendererWindowState::LabelStyle &representative = usedPinAsRepresentative
			? windowState.pinnedMeasurements[FindObjectIndex(
				windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements[0])].style
			: windowState.freeLabels[FindObjectIndex(
				windowState.freeLabels, windowState.selectedFreeLabels[0])].style;
		DrawLabelStyleEditor(representative);
		for (std::size_t i = usedPinAsRepresentative ? 1 : 0; i < pinCount; ++i)
			windowState.pinnedMeasurements[FindObjectIndex(
				windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements[i])].style = representative;
		for (std::size_t i = usedPinAsRepresentative ? 0 : 1; i < freeCount; ++i)
			windowState.freeLabels[FindObjectIndex(windowState.freeLabels, windowState.selectedFreeLabels[i])].style = representative;
	}

	void DrawAllLabelRows(RendererWindowState &windowState)
	{
		ImGui::Separator();
		ImGui::Text("Bond labels");
		DrawBondLabelLayoutRows(windowState);

		ImGui::Separator();
		ImGui::Text("Free labels");
		if (ImGui::Button("+ Add label"))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			RendererWindowState::FreeLabel label;
			label.id = windowState.sceneRegistry.AllocateObjectId();
			label.worldPosition = windowState.cursor3DPlaced ? windowState.cursor3DPosition : glm::vec3(0.0f);
			windowState.freeLabels.push_back(std::move(label));
		}
		int labelToRemove = -1;
		ImGui::PushID("AllFreeLabels");
		for (int labelIndex = 0; labelIndex < static_cast<int>(windowState.freeLabels.size()); ++labelIndex)
		{
			RendererWindowState::FreeLabel &label = windowState.freeLabels[labelIndex];
			ImGui::PushID(labelIndex);

			DrawFreeLabelTextInput(windowState, static_cast<std::size_t>(labelIndex), "##LabelText", 120.0f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(200.0f);
			glm::vec3 position = label.worldPosition;
			const bool moved = ImGui::InputFloat3("##LabelPos", &position.x, "%.3f");
			if (ImGui::IsItemActivated())
				PushPinnedMeasurementUndoSnapshot(windowState);
			if (moved)
			{
				label.worldPosition = position;
				if (const auto anchor = ResolveFreeLabelAnchor(windowState, label))
					label.anchorOffset = position - *anchor;
			}
			ImGui::SameLine();
			if (ImGui::Button("X##RemoveLabel"))
				labelToRemove = labelIndex;

			if (ImGui::TreeNode("Style##FreeLabelStyle"))
			{
				DrawLabelStyleEditor(label.style);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		if (labelToRemove >= 0)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			windowState.freeLabels.erase(windowState.freeLabels.begin() + labelToRemove);
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}
	}

	void AlignSelectedSceneOrbitalOrientations(
		std::vector<RendererWindowState::SceneOrbital> &orbitals,
		const std::vector<SceneObjectId> &selection)
	{
		std::size_t representative = orbitals.size();
		for (const SceneObjectId id : selection)
		{
			const std::size_t index = AnnotationIndex(orbitals, id);
			if (index < orbitals.size() && orbitals[index].lcaoComponents.empty() && !IsTwoCenterPreset(orbitals[index].preset))
			{
				representative = index;
				break;
			}
		}
		if (representative >= orbitals.size())
			return;

		const glm::vec3 orientation = orbitals[representative].rotationEuler;
		for (const SceneObjectId id : selection)
		{
			const std::size_t index = AnnotationIndex(orbitals, id);
			if (index < orbitals.size() && orbitals[index].lcaoComponents.empty() && !IsTwoCenterPreset(orbitals[index].preset))
				orbitals[index].rotationEuler = orientation;
		}
	}

	void DrawSelectedSceneOrbitalSection(RendererWindowState &windowState)
	{
		ImGui::Separator();
		const std::size_t selectedCount = windowState.selectedSceneOrbitals.size();
		ImGui::Text("Orbitale (%zu zaznaczonych)", selectedCount);
		const std::size_t representative = FirstSelectedSceneObjectIndex(
			windowState.sceneOrbitals, windowState.selectedSceneOrbitals);
		if (representative >= windowState.sceneOrbitals.size())
			return;

		ImGui::PushID("SelectedOrbitals");
		if (selectedCount == 1)
		{
			DrawSceneOrbitalEditor(windowState, representative);
			ImGui::PopID();
			return;
		}

		const bool hasLcao = std::any_of(windowState.selectedSceneOrbitals.begin(), windowState.selectedSceneOrbitals.end(), [&](SceneObjectId id) {
			const std::size_t index = AnnotationIndex(windowState.sceneOrbitals, id);
			return index < windowState.sceneOrbitals.size() && !windowState.sceneOrbitals[index].lcaoComponents.empty();
		});
		using Orbital = RendererWindowState::SceneOrbital;
		auto draw = [&](auto field, auto &&widget) {
			return DrawSelectedSharedValue(
				windowState, windowState.sceneOrbitals, windowState.selectedSceneOrbitals,
				representative, field, std::forward<decltype(widget)>(widget));
		};
		ImGui::TextDisabled("Wspolne pola ponizej sa stosowane do wszystkich zaznaczonych orbitali.");
		constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_DefaultOpen;
		if (!hasLcao && ImGui::CollapsingHeader("Ksztalt##SelectedOrbitalShape", kOpen))
		{
			draw(&Orbital::shell, [](int &value) {
				const bool changed = ImGui::SliderInt("Powloka (n)", &value, 1, 5);
				value = std::clamp(value, 1, 5);
				return changed;
			});
			draw(&Orbital::effectiveCharge, [](float &value) {
				return ImGui::DragFloat("Z_eff", &value, 0.05f, 0.1f, 30.0f, "%.2f");
			});
		}
		if (!hasLcao && ImGui::CollapsingHeader("Polozenie##SelectedOrbitalPlacement", kOpen))
		{
			if (ImGui::Button("Wyrownaj orientacje"))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				AlignSelectedSceneOrbitalOrientations(
					windowState.sceneOrbitals, windowState.selectedSceneOrbitals);
			}
			ImGui::SetItemTooltip(
				"Kopiuje obrot pierwszego zaznaczonego orbitalu jednoosrodkowego. "
				"Orbitale dwuosrodkowe zachowuja kierunek wyznaczony przez srodki.");
		}
		if (ImGui::CollapsingHeader("Wyglad##SelectedOrbitalAppearance", kOpen))
		{
			draw(&Orbital::phaseFlipped, [](bool &value) {
				return ImGui::Checkbox("Odwroc faze", &value);
			});
			draw(&Orbital::positiveLobeColor, [](glm::vec3 &value) {
				return ImGui::ColorEdit3("Faza +", &value.x);
			});
			draw(&Orbital::negativeLobeColor, [](glm::vec3 &value) {
				return ImGui::ColorEdit3("Faza -", &value.x);
			});
			draw(&Orbital::alpha, [](float &value) {
				return ImGui::SliderFloat("Przezroczystosc", &value, 0.05f, 1.0f, "%.2f");
			});
			draw(&Orbital::scale, [](float &value) {
				return ImGui::DragFloat("Skala rysunku", &value, 0.02f, 0.05f, 20.0f, "%.2f");
			});
			draw(&Orbital::isoFraction, [](float &value) {
				const bool changed = ImGui::SliderFloat("Izopowierzchnia", &value, 0.02f, 0.9f, "%.2f");
				value = std::clamp(value, 0.01f, 0.95f);
				return changed;
			});
			draw(&Orbital::resolution, [](int &value) {
				const bool changed = ImGui::SliderInt("Rozdzielczosc", &value, 16, 96);
				value = std::clamp(value, 8, 128);
				return changed;
			});
		}
		ImGui::PopID();
	}

	void DrawSelectedScenePlaneSection(RendererWindowState &windowState)
	{
		ImGui::Separator();
		const std::size_t selectedCount = windowState.selectedScenePlanes.size();
		ImGui::Text("Plaszczyzny (%zu zaznaczonych)", selectedCount);
		const std::size_t representative = FirstSelectedSceneObjectIndex(
			windowState.scenePlanes, windowState.selectedScenePlanes);
		if (representative >= windowState.scenePlanes.size())
			return;

		ImGui::PushID("SelectedPlanes");
		if (selectedCount == 1)
		{
			DrawPlaneAnchoring(windowState, windowState.scenePlanes[representative]);
			DrawScenePlaneEditor(windowState, representative);
			ImGui::PopID();
			return;
		}

		using Plane = RendererWindowState::ScenePlane;
		auto draw = [&](auto field, auto &&widget) {
			return DrawSelectedSharedValue(
				windowState, windowState.scenePlanes, windowState.selectedScenePlanes,
				representative, field, std::forward<decltype(widget)>(widget));
		};
		ImGui::TextDisabled("Wspolne pola ponizej sa stosowane do wszystkich zaznaczonych plaszczyzn.");
		draw(&Plane::halfExtents, [](glm::vec2 &value) {
			return ImGui::DragFloat2("Polowa rozmiaru", &value.x, 0.05f, 0.01f, 1000.0f, "%.2f");
		});
		draw(&Plane::color, [](glm::vec3 &value) {
			return ImGui::ColorEdit3("Kolor", &value.x);
		});
		draw(&Plane::alpha, [](float &value) {
			return ImGui::SliderFloat("Przezroczystosc", &value, 0.02f, 1.0f, "%.2f");
		});
		draw(&Plane::showBorder, [](bool &value) {
			return ImGui::Checkbox("Ramka", &value);
		});
		ImGui::SameLine();
		draw(&Plane::visible, [](bool &value) {
			return ImGui::Checkbox("Widoczna", &value);
		});
		ImGui::PopID();
	}

} // namespace DefectStudio
