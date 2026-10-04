#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"

#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

#include <algorithm>
#include <cstdio>
#include <string_view>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] SceneVisibilityColumnState AtomGroupColumnState(
			const RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices)
		{
			SceneVisibilityColumnAccumulator flags;
			for (const std::size_t index : atomIndices)
			{
				if (index >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &atom = windowState.structure.atoms[index];
				flags.Add(atom.visible, atom.renderable);
			}
			return SceneVisibilityStateFrom(flags);
		}

		void SetAtomVisibilityColumns(
			RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices, bool visible,
			bool renderable)
		{
			SceneRegistry &scene = windowState.sceneRegistry;
			for (const std::size_t index : atomIndices)
			{
				Entity atomEntity = scene.AtomEntityAt(index);
				if (!atomEntity)
					continue;
				VisibilityComponent &visibility = atomEntity.GetComponent<VisibilityComponent>();
				visibility.visible = visible;
				visibility.renderable = renderable;
			}
			SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);
		}

		void PushSelectedRowColors()
		{
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.85f, 0.42f, 0.05f, 0.85f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.95f, 0.50f, 0.10f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.95f, 0.50f, 0.10f, 0.9f));
		}

		template <typename Object>
		[[nodiscard]] std::vector<SceneObjectId> CollectIds(
			const std::vector<Object> &objects, const std::vector<std::size_t> &indices)
		{
			std::vector<SceneObjectId> ids;
			ids.reserve(indices.size());
			for (const std::size_t index : indices)
				if (index < objects.size())
					ids.push_back(objects[index].id);
			return ids;
		}

	} // namespace

	std::vector<std::size_t> CollectSceneOutlinerSourceIndices(
		const SceneRegistry &scene, const SceneObjectKind kind)
	{
		std::vector<std::size_t> indices;
		const auto view = scene.Registry().view<const SceneObjectComponent>();
		for (const entt::entity entity : view)
		{
			const SceneObjectComponent &object = view.get<const SceneObjectComponent>(entity);
			if (object.kind == kind)
				indices.push_back(object.sourceIndex);
		}
		std::sort(indices.begin(), indices.end());
		return indices;
	}

	bool SceneOutlinerPanel::drawSceneObjectContextMenu(
		RendererWindowState &windowState,
		const SelectionRowKind rowKind,
		const SceneObjectId clickedId,
		std::vector<SceneObjectId> &selection,
		const bool openRequested)
	{
		if (openRequested)
		{
			selectAnnotationRowForContextMenu(windowState, rowKind, clickedId, selection);
			ImGui::OpenPopup("##SceneObjectRowContextMenu");
		}
		if (!ImGui::BeginPopup("##SceneObjectRowContextMenu"))
			return false;

		const SceneObjectEditKind editKind = rowKind == SelectionRowKind::FreeLabel
			? SceneObjectEditKind::FreeLabel
			: rowKind == SelectionRowKind::Arrow ? SceneObjectEditKind::Arrow
			: rowKind == SelectionRowKind::Orbital ? SceneObjectEditKind::Orbital
			: rowKind == SelectionRowKind::Plane ? SceneObjectEditKind::Plane
			: SceneObjectEditKind::Path;
		bool sceneMutated = false;
		const auto drawAction = [&](const std::string_view label, const std::string_view shortcut,
			const SceneObjectEditAction action)
		{
			const bool enabled = CanExecuteSceneObjectEditAction(windowState, editKind, action);
			if (ImGui::MenuItem(label.data(), shortcut.data(), false, enabled))
			{
				ExecuteSceneObjectEditAction(windowState, editKind, action);
				sceneMutated = action != SceneObjectEditAction::Copy;
				ImGui::CloseCurrentPopup();
			}
		};
		drawAction("Usun", "Delete", SceneObjectEditAction::Delete);
		drawAction("Duplikuj", "Ctrl+D", SceneObjectEditAction::Duplicate);
		drawAction("Kopiuj", "Ctrl+C", SceneObjectEditAction::Copy);
		drawAction("Wklej", "Ctrl+V", SceneObjectEditAction::Paste);
		ImGui::EndPopup();
		return sceneMutated;
	}

	void SceneOutlinerPanel::drawAtomRow(
		RendererWindowState &windowState, const std::size_t atomIndex,
		const std::vector<std::size_t> &orderedAtomIndices)
	{

		ImGui::PushID(static_cast<int>(atomIndex));
		bool visible = windowState.structure.atoms[atomIndex].visible;
		bool renderable = windowState.structure.atoms[atomIndex].renderable;

		const bool isSelected = std::find(
									 windowState.selectedAtomIndices.begin(), windowState.selectedAtomIndices.end(), atomIndex) !=
			windowState.selectedAtomIndices.end();
		char label[64];
		std::snprintf(
			label, sizeof(label), "#%zu  (%.2f, %.2f, %.2f)", atomIndex,
			windowState.structure.atoms[atomIndex].cartesianPosition.x,
			windowState.structure.atoms[atomIndex].cartesianPosition.y,
			windowState.structure.atoms[atomIndex].cartesianPosition.z);
		if (isSelected)
			PushSelectedRowColors();
		ImGui::SetNextItemAllowOverlap();
		const bool rowActivated = ImGui::Selectable(label, isSelected);
		if (isSelected)
			ImGui::PopStyleColor(3);
		if (DrawSceneVisibilityColumns(visible, renderable))
			SetAtomVisibilityColumns(windowState, {atomIndex}, visible, renderable);
		if (rowActivated)
			applyAtomRowSelection(windowState, orderedAtomIndices, atomIndex);
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawSpeciesGroup(
		RendererWindowState &windowState, const std::string &species, const std::vector<std::size_t> &atomIndices)
	{
		ImGui::PushID(species.c_str());

		char groupLabel[48];
		std::snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)", species.c_str(), atomIndices.size());
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##species", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		const SceneVisibilityColumnEdit speciesEdit =
			DrawSceneVisibilityColumns(AtomGroupColumnState(windowState, atomIndices));
		if (speciesEdit.visibleChanged || speciesEdit.renderableChanged)
		{
			// A parent row writes one value onto every atom under it; the column that was not
			// clicked keeps each atom's own value, which is what the per-atom read below preserves.
			for (const std::size_t index : atomIndices)
			{
				if (index >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &atom = windowState.structure.atoms[index];
				SetAtomVisibilityColumns(
					windowState, {index}, speciesEdit.visibleChanged ? speciesEdit.visible : atom.visible,
					speciesEdit.renderableChanged ? speciesEdit.renderable : atom.renderable);
			}
		}

		if (open)
		{
			// Only the expanded group's own atoms are ever iterated per frame - a species with
			// thousands of atoms (a large defect supercell's majority element) still costs one clipped
			// pass, not thousands of widgets, unless it's the group actually open on screen.
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(atomIndices.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					drawAtomRow(windowState, atomIndices[static_cast<std::size_t>(row)], atomIndices);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	bool SceneOutlinerPanel::drawFreeLabelRow(
		RendererWindowState &windowState, const std::size_t labelIndex,
		const std::vector<SceneObjectId> &orderedIds)
	{
		// "Free"/"Pin" discriminator prefix, not just the numeric index - drawLabelsGroup draws both
		// kinds' rows as siblings under the same tree node, and each restarts its own index from 0.
		ImGui::PushID("Free");
		ImGui::PushID(static_cast<int>(labelIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedFreeLabels;
		const SceneObjectId id = windowState.freeLabels[labelIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		const std::string &text = windowState.freeLabels[labelIndex].text;
		const std::string rowLabel = text.empty() ? "(no text)" : text;

		if (isSelected)
			PushSelectedRowColors();
		ImGui::SetNextItemAllowOverlap();
		const bool rowActivated = ImGui::Selectable(rowLabel.c_str(), isSelected);
		const bool contextRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		if (isSelected)
			ImGui::PopStyleColor(3);
		DrawSceneVisibilityColumns(
			windowState.freeLabels[labelIndex].visible, windowState.freeLabels[labelIndex].renderable);
		if (rowActivated)
			applyAnnotationRowSelection(
				windowState, SelectionRowKind::FreeLabel, orderedIds, id, selection);
		const bool sceneMutated = drawSceneObjectContextMenu(
			windowState, SelectionRowKind::FreeLabel, id, selection, contextRequested);
		ImGui::PopID();
		ImGui::PopID();
		return sceneMutated;
	}

	void SceneOutlinerPanel::drawPinnedMeasurementRow(
		RendererWindowState &windowState, const std::size_t pinIndex,
		const std::vector<SceneObjectId> &orderedIds)
	{
		ImGui::PushID("Pin");
		ImGui::PushID(static_cast<int>(pinIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedPinnedMeasurements;
		const SceneObjectId id = windowState.pinnedMeasurements[pinIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		const RendererWindowState::PinnedMeasurement &pin = windowState.pinnedMeasurements[pinIndex];
		char rowLabel[32];
		std::snprintf(rowLabel, sizeof(rowLabel), "%s #%zu", pin.atomIndices.size() == 2 ? "Bond length" : "Angle", pinIndex);

		if (isSelected)
			PushSelectedRowColors();
		ImGui::SetNextItemAllowOverlap();
		const bool rowActivated = ImGui::Selectable(rowLabel, isSelected);
		if (isSelected)
			ImGui::PopStyleColor(3);
		DrawSceneVisibilityColumns(
			windowState.pinnedMeasurements[pinIndex].visible, windowState.pinnedMeasurements[pinIndex].renderable);
		if (rowActivated)
			applyAnnotationRowSelection(
				windowState, SelectionRowKind::PinnedMeasurement, orderedIds, id, selection);
		ImGui::PopID();
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawLabelsGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##labelsGroup");
		char groupLabel[32];
		// One header over two collections - free labels and pinned measurements both live under it.
		std::snprintf(
			groupLabel, sizeof(groupLabel), "Labels (%zu)",
			windowState.freeLabels.size() + windowState.pinnedMeasurements.size());
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##labels", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		SceneVisibilityColumnAccumulator flags;
		AddSceneVisibilityColumnFlags(windowState.freeLabels, flags);
		AddSceneVisibilityColumnFlags(windowState.pinnedMeasurements, flags);
		const SceneVisibilityColumnEdit edit = DrawSceneVisibilityColumns(SceneVisibilityStateFrom(flags));
		ApplySceneVisibilityColumnEdit(windowState.freeLabels, edit);
		ApplySceneVisibilityColumnEdit(windowState.pinnedMeasurements, edit);
		if (open)
		{
			const std::vector<std::size_t> freeLabelIndices =
				CollectSceneOutlinerSourceIndices(windowState.sceneRegistry, SceneObjectKind::FreeLabel);
			const std::vector<SceneObjectId> freeLabelIds =
				CollectIds(windowState.freeLabels, freeLabelIndices);
			for (const std::size_t index : freeLabelIndices)
				if (drawFreeLabelRow(windowState, index, freeLabelIds))
					break;
			const std::vector<std::size_t> pinIndices =
				CollectSceneOutlinerSourceIndices(windowState.sceneRegistry, SceneObjectKind::PinnedMeasurement);
			const std::vector<SceneObjectId> pinIds =
				CollectIds(windowState.pinnedMeasurements, pinIndices);
			for (const std::size_t index : pinIndices)
				drawPinnedMeasurementRow(windowState, index, pinIds);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	bool SceneOutlinerPanel::drawSceneArrowRow(
		RendererWindowState &windowState, const std::size_t arrowIndex,
		const std::vector<SceneObjectId> &orderedIds)
	{
		ImGui::PushID(static_cast<int>(arrowIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedSceneArrows;
		const SceneObjectId id = windowState.sceneArrows[arrowIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
		const char *kindLabel = arrow.kind == RendererWindowState::ArrowKind::Line ? "Line"
			: arrow.kind == RendererWindowState::ArrowKind::Arrow2D ? "Arrow 2D" : "Arrow 3D";
		char rowLabel[32];
		std::snprintf(rowLabel, sizeof(rowLabel), "%s #%zu", kindLabel, arrowIndex);

		if (isSelected)
			PushSelectedRowColors();
		ImGui::SetNextItemAllowOverlap();
		const bool rowActivated = ImGui::Selectable(rowLabel, isSelected);
		const bool contextRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		if (isSelected)
			ImGui::PopStyleColor(3);
		DrawSceneVisibilityColumns(
			windowState.sceneArrows[arrowIndex].visible, windowState.sceneArrows[arrowIndex].renderable);
		if (rowActivated)
			applyAnnotationRowSelection(
				windowState, SelectionRowKind::Arrow, orderedIds, id, selection);
		const bool sceneMutated = drawSceneObjectContextMenu(
			windowState, SelectionRowKind::Arrow, id, selection, contextRequested);
		ImGui::PopID();
		return sceneMutated;
	}

	void SceneOutlinerPanel::drawArrowsGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##arrowsGroup");
		char groupLabel[32];
		std::snprintf(groupLabel, sizeof(groupLabel), "Arrows (%zu)", windowState.sceneArrows.size());
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##arrows", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		ApplySceneVisibilityColumnEdit(
			windowState.sceneArrows, DrawSceneVisibilityColumns(SceneVisibilityStateFor(windowState.sceneArrows)));
		if (open)
		{
			const std::vector<std::size_t> indices =
				CollectSceneOutlinerSourceIndices(windowState.sceneRegistry, SceneObjectKind::SceneArrow);
			const std::vector<SceneObjectId> ids = CollectIds(windowState.sceneArrows, indices);
			for (const std::size_t index : indices)
				if (drawSceneArrowRow(windowState, index, ids))
					break;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	bool SceneOutlinerPanel::drawSceneOrbitalRow(
		RendererWindowState &windowState, const std::size_t orbitalIndex,
		const std::vector<SceneObjectId> &orderedIds)
	{
		ImGui::PushID(static_cast<int>(orbitalIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedSceneOrbitals;
		const SceneObjectId id = windowState.sceneOrbitals[orbitalIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		char rowLabel[48];
		std::snprintf(
			rowLabel, sizeof(rowLabel), "%s #%zu",
			OrbitalPresetName(windowState.sceneOrbitals[orbitalIndex].preset), orbitalIndex);

		if (isSelected)
			PushSelectedRowColors();
		ImGui::SetNextItemAllowOverlap();
		const auto &orbital = windowState.sceneOrbitals[orbitalIndex];
		const bool rowActivated = ImGui::Selectable(orbital.displayName.empty() ? rowLabel : orbital.displayName.c_str(), isSelected);
		const bool contextRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		if (isSelected)
			ImGui::PopStyleColor(3);
		DrawSceneVisibilityColumns(
			windowState.sceneOrbitals[orbitalIndex].visible, windowState.sceneOrbitals[orbitalIndex].renderable);
		if (rowActivated)
			applyAnnotationRowSelection(
				windowState, SelectionRowKind::Orbital, orderedIds, id, selection);
		const bool sceneMutated = drawSceneObjectContextMenu(
			windowState, SelectionRowKind::Orbital, id, selection, contextRequested);
		ImGui::PopID();
		return sceneMutated;
	}

	void SceneOutlinerPanel::drawOrbitalsGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##orbitalsGroup");
		char groupLabel[32];
		std::snprintf(groupLabel, sizeof(groupLabel), "Orbitals (%zu)", windowState.sceneOrbitals.size());
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##orbitals", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		ApplySceneVisibilityColumnEdit(
			windowState.sceneOrbitals, DrawSceneVisibilityColumns(SceneVisibilityStateFor(windowState.sceneOrbitals)));
		if (open)
		{
			const std::vector<std::size_t> indices =
				CollectSceneOutlinerSourceIndices(windowState.sceneRegistry, SceneObjectKind::SceneOrbital);
			const std::vector<SceneObjectId> ids = CollectIds(windowState.sceneOrbitals, indices);
			for (const std::size_t index : indices)
				if (drawSceneOrbitalRow(windowState, index, ids))
					break;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawPlanesGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##planesGroup");
		char groupLabel[32];
		std::snprintf(groupLabel, sizeof(groupLabel), "Planes (%zu)", windowState.scenePlanes.size());
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##planes", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		ApplySceneVisibilityColumnEdit(
			windowState.scenePlanes, DrawSceneVisibilityColumns(SceneVisibilityStateFor(windowState.scenePlanes)));
		if (open)
		{
			std::vector<SceneObjectId> ids;
			ids.reserve(windowState.scenePlanes.size());
			for (const RendererWindowState::ScenePlane &plane : windowState.scenePlanes)
				ids.push_back(plane.id);
			for (std::size_t index = 0; index < windowState.scenePlanes.size(); ++index)
			{
				RendererWindowState::ScenePlane &plane = windowState.scenePlanes[index];
				ImGui::PushID(static_cast<int>(index));
				char rowLabel[32];
				std::snprintf(rowLabel, sizeof(rowLabel), "Plane #%zu", index);
				auto &selection = windowState.selectedScenePlanes;
				const bool selected =
					std::find(selection.begin(), selection.end(), plane.id) != selection.end();
				if (selected)
					PushSelectedRowColors();
				ImGui::SetNextItemAllowOverlap();
				const bool rowActivated = ImGui::Selectable(rowLabel, selected);
				const bool contextRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);
				if (selected)
					ImGui::PopStyleColor(3);
				DrawSceneVisibilityColumns(plane.visible, plane.renderable);
				if (rowActivated)
					applyAnnotationRowSelection(
						windowState, SelectionRowKind::Plane, ids, plane.id, selection);
				const bool sceneMutated = drawSceneObjectContextMenu(
					windowState, SelectionRowKind::Plane, plane.id, selection, contextRequested);
				ImGui::PopID();
				if (sceneMutated)
					break;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawVacanciesGroup(RendererWindowState &windowState)
	{
		if (windowState.structure.vacancies.empty())
			return;
		ImGui::PushID("##vacanciesGroup");
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##vacancies", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "Wakanse (%zu)",
			windowState.structure.vacancies.size());
		// One flag behind both columns: a vacancy marker has no separate "in the export" switch yet.
		const SceneVisibilityColumnEdit edit =
			DrawSceneVisibilityColumns({windowState.showVacancies, windowState.showVacancies});
		if (edit.visibleChanged)
			windowState.showVacancies = edit.visible;
		else if (edit.renderableChanged)
			windowState.showVacancies = edit.renderable;
		if (open)
		{
			std::optional<std::size_t> removeIndex;
			for (std::size_t index = 0; index < windowState.structure.vacancies.size(); ++index)
			{
				const RendererVacancyData &vacancy = windowState.structure.vacancies[index];
				ImGui::PushID(static_cast<int>(index));
				if (ImGui::SmallButton("X"))
					removeIndex = index;
				ImGui::SameLine();
				char row[96];
				std::snprintf(row, sizeof(row), "%s (%.2f, %.2f, %.2f)", vacancy.label.c_str(),
					vacancy.cartesianPosition.x, vacancy.cartesianPosition.y, vacancy.cartesianPosition.z);
				auto &selected = windowState.selectedVacancies;
				const bool isSelected = std::find(selected.begin(), selected.end(), index) != selected.end();
				if (ImGui::Selectable(row, isSelected))
				{
					if (ImGui::GetIO().KeyCtrl)
					{
						if (isSelected)
							std::erase(selected, index);
						else
							selected.push_back(index);
					}
					else
					{
						selected = {index};
						windowState.defectFrameSelected = false;
						windowState.selectedFreeLabels.clear();
						windowState.selectedSceneArrows.clear();
						windowState.selectedPinnedMeasurements.clear();
						windowState.selectedSceneOrbitals.clear();
						windowState.selectedScenePlanes.clear();
						windowState.selectedScenePaths.clear();
						SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
					}
				}
				ImGui::PopID();
			}
			ImGui::TreePop();

			const Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			const Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
			if (removeIndex.has_value() && commandRegistry != nullptr && domainLayer != nullptr)
			{
				const auto record = domainLayer->Workspace().Structures().Find(windowState.structureId).lock();
				// renderer.vacancy.set replaces the whole list, so the edit starts from the domain's.
				if (record != nullptr && *removeIndex < record->structure.vacancies.size())
				{
					SetVacanciesPayload payload{windowState.windowId, record->structure.vacancies, "Remove vacancy"};
					payload.vacancies.erase(payload.vacancies.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
					CommandContext context;
					context.Set<SetVacanciesPayload>(kSetVacanciesPayloadKey, std::move(payload));
					const Result<CommandOutcome> result =
						commandRegistry->Execute(CommandID{kSetVacanciesCommandId}, std::move(context));
					if (!result)
						DS_LOG_WARN("Remove vacancy failed: {}", result.Error().technicalDetails);
				}
			}
		}
		ImGui::PopID();
	}

} // namespace DefectStudio
