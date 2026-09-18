#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanelSections.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <imgui.h>

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Presentation/Panels/SceneObjectMultiSelection.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"

namespace DefectStudio
{
	// Shared by every label kind (free labels, pinned bond/angle labels) - one editor for
	// RendererWindowState::LabelStyle instead of a separate control block per label kind. Outline/
	// background rows read "0 = off" like the shader they feed (labels.frag/label_background.frag).
	static void drawLabelStyleEditor(RendererWindowState::LabelStyle &style)
	{
		ImGui::TextUnformatted("Text");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleTextColor", &style.textColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleTextAlpha", &style.textAlpha, 0.0f, 1.0f, "%.2f");

		// Checkbox is a thin view over backgroundAlpha/outlineWidth themselves (0 = off, same meaning
		// the shader already gives that value) rather than a separate enabled flag - one source of
		// truth. Toggling on restores a sensible default rather than 0, since the slider/drag below
		// would otherwise show "on" at a still-invisible value.
		bool backgroundEnabled = style.backgroundAlpha > 0.0f;
		if (ImGui::Checkbox("##StyleBackgroundEnabled", &backgroundEnabled))
			style.backgroundAlpha = backgroundEnabled ? 0.85f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Background");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::BeginDisabled(!backgroundEnabled);
		ImGui::ColorEdit3("##StyleBackgroundColor", &style.backgroundColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleBackgroundAlpha", &style.backgroundAlpha, 0.01f, 1.0f, "%.2f");
		ImGui::EndDisabled();

		bool borderEnabled = style.outlineWidth > 0.0f;
		if (ImGui::Checkbox("##StyleBorderEnabled", &borderEnabled))
			style.outlineWidth = borderEnabled ? 0.02f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Border");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleOutlineColor", &style.outlineColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!borderEnabled);
		ImGui::DragFloat("Width##StyleOutlineWidth", &style.outlineWidth, 0.002f, 0.001f, 0.2f, "%.3f");
		ImGui::EndDisabled();

		// Glyph stroke (labels.frag), independent of the Border row above which only frames the
		// background quad. Screen pixels, not world units and not normalized SDF units - stays a
		// constant on-screen thickness regardless of zoom, unlike Border's 0.001-0.2 world-unit range.
		bool strokeEnabled = style.strokeWidth > 0.0f;
		if (ImGui::Checkbox("##StyleStrokeEnabled", &strokeEnabled))
			style.strokeWidth = strokeEnabled ? 2.0f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Stroke");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleStrokeColor", &style.strokeColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!strokeEnabled);
		ImGui::DragFloat("Width (px)##StyleStrokeWidth", &style.strokeWidth, 0.05f, 0.1f, 8.0f, "%.2f");
		ImGui::EndDisabled();

		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Corner radius##StyleCornerRadius", &style.cornerRadius, 0.005f, 0.0f, 0.3f, "%.3f");

		ImGui::SetNextItemWidth(140.0f);
		ImGui::DragFloat2("Padding X/Y##StylePadding", &style.padding.x, 0.005f, 0.0f, 1.0f, "%.3f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Scale##StyleScale", &style.scale, 0.02f, 0.1f, 8.0f, "%.2f");
	}

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
				ImGui::Checkbox("Align to bond##PinAlign", &pin.alignToBondDirection);
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
		drawLabelStyleEditor(representative);
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

			char textBuffer[128];
			std::snprintf(textBuffer, sizeof(textBuffer), "%s", label.text.c_str());
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputText("##LabelText", textBuffer, sizeof(textBuffer)))
				label.text = textBuffer;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(200.0f);
			ImGui::InputFloat3("##LabelPos", &label.worldPosition.x, "%.3f");
			ImGui::SameLine();
			if (ImGui::Button("X##RemoveLabel"))
				labelToRemove = labelIndex;

			if (ImGui::TreeNode("Style##FreeLabelStyle"))
			{
				drawLabelStyleEditor(label.style);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		if (labelToRemove >= 0)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			windowState.freeLabels.erase(windowState.freeLabels.begin() + labelToRemove);
		}
	}

	void DrawAllArrowRows(RendererWindowState &windowState)
	{
		ImGui::Separator();
		ImGui::Text("Arrows");
		if (ImGui::Button("+ Add arrow"))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			const glm::vec3 seed = windowState.cursor3DPlaced ? windowState.cursor3DPosition : glm::vec3(0.0f);
			RendererWindowState::SceneArrow arrow = MakeDefaultSceneArrow(windowState, seed);
			arrow.id = windowState.sceneRegistry.AllocateObjectId();
			windowState.sceneArrows.push_back(std::move(arrow));
			const std::size_t newIndex = windowState.sceneArrows.size() - 1;
			windowState.selectedSceneArrows = {windowState.sceneArrows[newIndex].id};
			windowState.sceneArrowQuickEditActive = true;
			windowState.sceneArrowQuickEditIndex = newIndex;
		}

		int arrowToRemove = -1;
		ImGui::PushID("AllArrows");
		for (int arrowIndex = 0; arrowIndex < static_cast<int>(windowState.sceneArrows.size()); ++arrowIndex)
		{
			RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
			ImGui::PushID(arrowIndex);
			const SceneObjectId rowId = arrow.id;
			const bool isSelected = std::find(
				windowState.selectedSceneArrows.begin(), windowState.selectedSceneArrows.end(), rowId) !=
				windowState.selectedSceneArrows.end();
			const char *kindLabel = arrow.kind == RendererWindowState::ArrowKind::Line ? "Line"
				: arrow.kind == RendererWindowState::ArrowKind::Arrow2D ? "Arrow 2D" : "Arrow 3D";
			char rowLabel[32];
			std::snprintf(rowLabel, sizeof(rowLabel), "%s #%d", kindLabel, arrowIndex);
			if (ImGui::Selectable(rowLabel, isSelected, ImGuiSelectableFlags_AllowOverlap))
			{
				std::vector<SceneObjectId> &selection = windowState.selectedSceneArrows;
				if (ImGui::GetIO().KeyCtrl)
				{
					const auto existing = std::find(selection.begin(), selection.end(), rowId);
					if (existing != selection.end())
						selection.erase(existing);
					else
						selection.push_back(rowId);
				}
				else
				{
					selection = {rowId};
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("X##RemoveArrow"))
				arrowToRemove = arrowIndex;
			ImGui::PopID();
		}
		ImGui::PopID();
		if (arrowToRemove >= 0)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			EraseSceneArrows(windowState, {windowState.sceneArrows[static_cast<std::size_t>(arrowToRemove)].id});
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

		using Orbital = RendererWindowState::SceneOrbital;
		auto draw = [&](auto field, auto &&widget) {
			return DrawSelectedSharedValue(
				windowState, windowState.sceneOrbitals, windowState.selectedSceneOrbitals,
				representative, field, std::forward<decltype(widget)>(widget));
		};
		ImGui::TextDisabled("Wspolne pola ponizej sa stosowane do wszystkich zaznaczonych orbitali.");
		constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_DefaultOpen;
		if (ImGui::CollapsingHeader("Ksztalt##SelectedOrbitalShape", kOpen))
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
		if (ImGui::CollapsingHeader("Wyglad##SelectedOrbitalAppearance", kOpen))
		{
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
