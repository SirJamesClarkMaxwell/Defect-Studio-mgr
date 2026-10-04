#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Domain/Electronic/HydrogenicOrbital.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalAim.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::vector<std::size_t> ValidSelectedAtoms(const RendererWindowState &windowState)
		{
			std::vector<std::size_t> indices;
			indices.reserve(windowState.selectedAtomIndices.size());
			for (const std::size_t atomIndex : windowState.selectedAtomIndices)
				if (atomIndex < windowState.structure.atoms.size())
					indices.push_back(atomIndex);
			return indices;
		}

		[[nodiscard]] std::vector<glm::vec3> SelectedAtomPositions(const RendererWindowState &windowState)
		{
			std::vector<glm::vec3> positions;
			for (const std::size_t atomIndex : ValidSelectedAtoms(windowState))
				positions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
			return positions;
		}

		[[nodiscard]] const char *AtomCountSuffix(const std::size_t count)
		{
			if (count == 1)
				return "atom";
			if (count >= 2 && count <= 4)
				return "atomy";
			return "atomow";
		}

		// A menu row whose label carries a real subscript: '_' opens the subscript run and it ends at
		// the next space (the convention OrbitalPresetMemberDisplayName documents). Unicode has no
		// subscript y and no subscript z, so "pᵧ" would have to be a Greek gamma wearing a wig - the
		// run is drawn at 72% size on a lowered baseline instead.
		//
		// The row is still an ordinary MenuItem, given the full label, so width, hover, keyboard nav
		// and the auto-close behaviour are ImGui's; only its text is drawn transparent and replaced.
		bool SubscriptMenuItem(const char *label, const bool enabled = true)
		{
			const std::string text = label;
			const std::size_t mark = text.find('_');
			if (mark == std::string::npos)
				return ImGui::MenuItem(label, nullptr, false, enabled);

			const std::size_t subEnd = text.find(' ', mark);
			const std::string base = text.substr(0, mark);
			const std::string sub =
				text.substr(mark + 1, subEnd == std::string::npos ? std::string::npos : subEnd - mark - 1);
			const std::string tail = subEnd == std::string::npos ? std::string() : text.substr(subEnd);

			const ImVec2 pen = ImGui::GetCursorScreenPos();
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
			const bool pressed = ImGui::MenuItem(label, nullptr, false, enabled);
			ImGui::PopStyleColor();

			ImFont *font = ImGui::GetFont();
			const float size = ImGui::GetFontSize();
			const float subSize = size * 0.72f;
			// GetColorU32 folds in style.Alpha, so a row inside an enclosing BeginDisabled dims with
			// the rest of the group instead of staying bright.
			const ImU32 color = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
			ImDrawList *draw = ImGui::GetWindowDrawList();
			float x = pen.x;
			draw->AddText(font, size, ImVec2(x, pen.y), color, base.c_str());
			x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, base.c_str()).x;
			draw->AddText(font, subSize, ImVec2(x, pen.y + size - subSize), color, sub.c_str());
			if (!tail.empty())
			{
				x += font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, sub.c_str()).x;
				draw->AddText(font, size, ImVec2(x, pen.y), color, tail.c_str());
			}
			return pressed;
		}

	} // namespace

	DrawSelectionDescription DescribeDrawSelection(const std::size_t validSelectedAtomCount)
	{
		DrawSelectionDescription description;
		description.canDrawSegment = validSelectedAtomCount == 2;
		description.canDrawPlane = validSelectedAtomCount >= 2;
		description.menuLabel = "Rysuj (" + std::to_string(validSelectedAtomCount) + " " +
			AtomCountSuffix(validSelectedAtomCount) + ")";
		description.lineLabel = description.canDrawSegment ? "Linia" : "Linia (wymaga dokladnie 2 atomow)";
		description.arrowLabel =
			description.canDrawSegment ? "Strzalka" : "Strzalka (wymaga dokladnie 2 atomow)";
		description.planeLabel =
			description.canDrawPlane ? "Plaszczyzna" : "Plaszczyzna (wymaga co najmniej 2 atomow)";
		if (!description.canDrawSegment)
			description.segmentTooltip = "Zaznacz dokladnie dwa atomy, aby dodac linie lub strzalke.";
		if (!description.canDrawPlane)
			description.planeTooltip = "Zaznacz co najmniej dwa atomy, aby dodac plaszczyzne.";
		return description;
	}

	DrawSelectionDescription DescribeDrawSelection(const RendererWindowState &windowState)
	{
		return DescribeDrawSelection(SelectedAtomPositions(windowState).size());
	}

	std::vector<std::vector<std::size_t>> ResolveOrbitalAddAnchorGroups(
		const OrbitalPreset preset, const std::vector<std::size_t> &selectedAtoms,
		const bool anchorToSelection)
	{
		if (!anchorToSelection)
			return {{}};
		if (IsTwoCenterPreset(preset))
			return selectedAtoms.size() == 2
				? std::vector<std::vector<std::size_t>>{selectedAtoms}
				: std::vector<std::vector<std::size_t>>{};

		std::vector<std::vector<std::size_t>> groups;
		groups.reserve(selectedAtoms.size());
		for (const std::size_t atomIndex : selectedAtoms)
			groups.push_back({atomIndex});
		return groups;
	}

	void DrawSegmentAddItems(RendererWindowState &windowState)
	{
		// Segment ends may be atoms or vacancy markers (AddScenePathThroughSelectedAtoms).
		std::size_t vacancies = 0;
		for (const std::size_t index : windowState.selectedVacancies)
			vacancies += index < windowState.structure.vacancies.size() ? 1u : 0u;
		const DrawSelectionDescription description =
			DescribeDrawSelection(ValidSelectedAtoms(windowState).size() + vacancies);
		const auto addSegment = [&](const bool arrow) {
			const auto result = AddScenePathThroughSelectedAtoms(windowState, arrow);
			if (!result)
				DS_LOG_WARN("Add segment failed: {}", result.Error().technicalDetails);
		};

		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Bufor##ScenePathAtomBuffer", &GetScenePathAtomBuffer(), 0.02f, 0.0f, 3.0f, "%.2f r");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip(
				"Odleglosc konca od srodka atomu, w promieniach kuli. "
				"0 = srodek, 1.0 = powierzchnia, 1.15 = odstep 15%% promienia.");
		if (ImGui::MenuItem(description.lineLabel.c_str(), nullptr, false, description.canDrawSegment))
			addSegment(false);
		if (ImGui::MenuItem(description.arrowLabel.c_str(), nullptr, false, description.canDrawSegment))
			addSegment(true);
	}

	void DrawPlaneAddItem(RendererWindowState &windowState)
	{
		const std::vector<glm::vec3> positions = SelectedAtomPositions(windowState);
		const DrawSelectionDescription description = DescribeDrawSelection(positions.size());
		if (!ImGui::MenuItem(description.planeLabel.c_str(), nullptr, false, description.canDrawPlane))
			return;

		const glm::vec3 viewDirection = windowState.camera != nullptr
			? glm::normalize(windowState.camera->Target() - windowState.camera->Position())
			: glm::vec3(0.0f, 0.0f, -1.0f);
		if (const std::optional<ScenePlaneFit> fit = FitScenePlane(positions, viewDirection))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			RendererWindowState::ScenePlane plane = MakeScenePlane(*fit);
			plane.anchorAtoms = ValidSelectedAtoms(windowState);
			plane.id = windowState.sceneRegistry.AllocateObjectId();
			windowState.scenePlanes.push_back(std::move(plane));
			windowState.selectedScenePlanes = {windowState.scenePlanes.back().id};
		}
	}

	void DrawFreeSegmentAddItems(RendererWindowState &windowState, const glm::vec3 &worldPosition)
	{
		const auto add = [&](const bool arrow) {
			const auto result = AddFreeScenePathSegment(windowState, worldPosition, arrow);
			if (!result)
				DS_LOG_WARN("Add free segment failed: {}", result.Error().technicalDetails);
		};
		if (ImGui::MenuItem("Linia swobodna"))
			add(false);
		if (ImGui::MenuItem("Strzalka swobodna"))
			add(true);
	}

	void DrawFreePlaneAddItem(RendererWindowState &windowState, const glm::vec3 &worldPosition)
	{
		if (!ImGui::MenuItem("Plaszczyzna swobodna"))
			return;

		PushPinnedMeasurementUndoSnapshot(windowState);
		RendererWindowState::ScenePlane plane = MakeDefaultScenePlane(windowState, worldPosition);
		plane.id = windowState.sceneRegistry.AllocateObjectId();
		windowState.scenePlanes.push_back(std::move(plane));
		windowState.selectedScenePlanes = {windowState.scenePlanes.back().id};
	}

	void DrawOrbitalAddMenu(
		RendererWindowState &windowState,
		const glm::vec3 &worldPosition,
		const bool drawSubmenu)
	{
		if (drawSubmenu && !ImGui::BeginMenu("Orbital"))
			return;
		{
			const std::vector<std::size_t> selected = ValidSelectedAtoms(windowState);
			const std::size_t selectedAtoms = selected.size();
			// One place that builds and files a new orbital, so the entry points below cannot
			// drift apart on anchoring, undo or selection.
			auto registerOrbital = [&](RendererWindowState::SceneOrbital orbital) {
				return AppendSceneOrbital(windowState, std::move(orbital));
			};
			auto addOrbital = [&](OrbitalPreset preset, int lobeIndex, const std::vector<std::size_t> &anchors) {
				auto orbital = MakeDefaultSceneOrbital(windowState, preset, worldPosition, anchors);
				orbital.lobeIndex = lobeIndex;
				return registerOrbital(std::move(orbital));
			};

			// "Put this preset where the user said" - and for a one-centre preset with several
			// atoms selected, that is one orbital per atom, not one orbital that silently declines
			// to anchor because the count was not exactly 1. That refusal was the whole of #4.
			auto addPreset = [&](OrbitalPreset preset, int lobeIndex, bool anchor) {
				std::vector<SceneObjectId> added;
				for (const std::vector<std::size_t> &anchors :
					ResolveOrbitalAddAnchorGroups(preset, selected, anchor))
					added.push_back(addOrbital(preset, lobeIndex, anchors));
				return added;
			};

			auto selectAdded = [&](std::vector<SceneObjectId> added) {
				windowState.selectedSceneOrbitals = std::move(added);
				// The atoms the orbitals were built on stay selected otherwise, and the next Delete
				// would remove them together with the orbitals.
				SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
				SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
			};

			auto addAllLobes = [&](OrbitalPreset preset, int members, bool anchor) {
				std::vector<SceneObjectId> added;
				for (int lobe = 0; lobe < members; ++lobe)
				{
					const std::vector<SceneObjectId> lobeIds = addPreset(preset, lobe, anchor);
					added.insert(added.end(), lobeIds.begin(), lobeIds.end());
				}
				return added;
			};

			// Placement used to be a whole level of flyout ("Na N zaznaczonych atomach" vs "W kursorze
			// 3D"), duplicating the catalogue under each. It is a mode, so it is a checkbox: a
			// Checkbox does not auto-close the popup, so mode and pick still happen in one trip.
			ImGui::BeginDisabled(selectedAtoms == 0);
			const std::string anchorLabel = selectedAtoms == 0
				? std::string("Zakotwicz na zaznaczonych atomach (brak zaznaczenia)")
				: "Zakotwicz na " + std::to_string(selectedAtoms) +
					(selectedAtoms == 1 ? " zaznaczonym atomie" : " zaznaczonych atomach");
			ImGui::Checkbox(anchorLabel.c_str(), &windowState.orbitalAddAnchorToSelection);
			ImGui::EndDisabled();

			// Anchoring with nothing selected resolves to zero anchor groups, i.e. an Add that adds
			// nothing, so it is off for this Add - but the remembered mode is kept, because adding an
			// orbital clears the atom selection and the next selection should anchor again.
			const bool anchor = windowState.orbitalAddAnchorToSelection && selectedAtoms > 0;
			if (!anchor)
				ImGui::TextDisabled("Orbital stanie w kursorze 3D.");

			const auto target = ResolveDanglingBondTarget(windowState, selected);
			if (ImGui::MenuItem("Wiązania zwisające → wakans (sp³)", nullptr, false, target.has_value()))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				std::vector<SceneObjectId> added;
				for (auto &orbital : MakeDanglingBondOrbitals(windowState, selected, *target))
					added.push_back(registerOrbital(std::move(orbital)));
				selectAdded(std::move(added));
			}
			if (target && ImGui::IsItemHovered())
			{
				std::string label = "na centroid zaznaczenia";
				for (std::size_t i = 0; i < windowState.structure.vacancies.size(); ++i)
				{
					const auto &vacancy = windowState.structure.vacancies[i];
					if (vacancy.cartesianPosition != *target)
						continue;
					label = "na " + vacancy.label + " #" + std::to_string(i + 1);
					break;
				}
				ImGui::SetTooltip("%s", label.c_str());
			}

			for (const OrbitalPresetGroup group : AllOrbitalPresetGroups())
			{
				ImGui::SeparatorText(OrbitalPresetGroupName(group));
				for (const OrbitalPreset preset : OrbitalPresetsInGroup(group))
				{
					const bool compatible = !anchor || !IsTwoCenterPreset(preset) || selectedAtoms == 2;
					const int members = OrbitalPresetMemberCount(preset);
					// The requirement rides in the label rather than a tooltip, because ImGui does not
					// hover a disabled item and a greyed row that will not say why is the complaint
					// this menu already collected once.
					const std::string presetLabel = compatible
						? std::string(OrbitalPresetDisplayName(preset))
						: std::string(OrbitalPresetDisplayName(preset)) + " (wymaga 2 atomow)";
					ImGui::BeginDisabled(!compatible);
					if (members <= 1)
					{
						if (ImGui::MenuItem(presetLabel.c_str()))
						{
							PushPinnedMeasurementUndoSnapshot(windowState);
							selectAdded(addPreset(preset, 0, anchor));
						}
						ImGui::EndDisabled();
						continue;
					}
					if (ImGui::BeginMenu(presetLabel.c_str()))
					{
						for (int lobe = 0; lobe < members; ++lobe)
						{
							if (!SubscriptMenuItem(OrbitalPresetMemberDisplayName(preset, lobe)))
								continue;
							PushPinnedMeasurementUndoSnapshot(windowState);
							selectAdded(addPreset(preset, lobe, anchor));
						}
						ImGui::Separator();
						const std::string allLabel = "Wszystkie platki (" +
							std::to_string(members * static_cast<int>(anchor ? std::max<std::size_t>(selectedAtoms, 1) : 1)) +
							")";
						if (ImGui::MenuItem(allLabel.c_str()))
						{
							PushPinnedMeasurementUndoSnapshot(windowState);
							selectAdded(addAllLobes(preset, members, anchor));
						}
						ImGui::EndMenu();
					}
					ImGui::EndDisabled();
				}
			}
			if (drawSubmenu)
				ImGui::EndMenu();
		}
	}
} // namespace DefectStudio
