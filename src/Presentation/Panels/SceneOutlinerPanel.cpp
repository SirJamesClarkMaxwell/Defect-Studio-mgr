#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"

#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

#include <algorithm>
#include <cstdio>
#include <map>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Domain/DomainLayer.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "ScientificRuntime/Python/CopyWindowStateJob.hpp"

namespace DefectStudio
{
	namespace
	{
		// What a parent row saw under it. Accumulated rather than returned per collection, so a header
		// spanning two vectors (Labels) reads exactly like one spanning a single vector.
		struct ColumnFlags
		{
			bool anyVisible = false;
			bool anyHidden = false;
			bool anyRenderable = false;
			bool anyNotRenderable = false;

			void Add(bool visible, bool renderable)
			{
				anyVisible = anyVisible || visible;
				anyHidden = anyHidden || !visible;
				anyRenderable = anyRenderable || renderable;
				anyNotRenderable = anyNotRenderable || !renderable;
			}
		};

		// On when everything under the row agrees, mixed when it does not. An empty row reads as on.
		[[nodiscard]] SceneVisibilityColumnState ColumnStateFrom(const ColumnFlags &flags)
		{
			SceneVisibilityColumnState state;
			state.visible = !flags.anyHidden;
			state.renderable = !flags.anyNotRenderable;
			state.visibleMixed = flags.anyVisible && flags.anyHidden;
			state.renderableMixed = flags.anyRenderable && flags.anyNotRenderable;
			return state;
		}

		template <typename Objects>
		void AddObjectColumnFlags(const Objects &objects, ColumnFlags &flags)
		{
			for (const auto &object : objects)
				flags.Add(object.visible, object.renderable);
		}

		// A species row's two columns.
		[[nodiscard]] SceneVisibilityColumnState AtomGroupColumnState(
			const RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices)
		{
			ColumnFlags flags;
			for (const std::size_t index : atomIndices)
			{
				if (index >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &atom = windowState.structure.atoms[index];
				flags.Add(atom.visible, atom.renderable);
			}
			return ColumnStateFrom(flags);
		}

		// The same two columns over any vector of scene objects that carries the pair of flags.
		template <typename Objects>
		[[nodiscard]] SceneVisibilityColumnState ObjectGroupColumnState(const Objects &objects)
		{
			ColumnFlags flags;
			AddObjectColumnFlags(objects, flags);
			return ColumnStateFrom(flags);
		}

		template <typename Objects>
		void ApplyGroupColumnEdit(Objects &objects, const SceneVisibilityColumnEdit &edit)
		{
			for (auto &object : objects)
			{
				if (edit.visibleChanged)
					object.visible = edit.visible;
				if (edit.renderableChanged)
					object.renderable = edit.renderable;
			}
		}

		// Same mechanism as H (hide selected)/Alt+H (show all) - see ViewModifier.cpp - just applied
		// to an explicit set of atom entities instead of only the current selection. Atoms are owned
		// by the ECS mirror, so both columns are written on the component and pushed back out; see
		// Renderer/Scene/SceneVisibility.hpp.
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

		// The window row stands for everything under it, in both columns.
		[[nodiscard]] SceneVisibilityColumnState WindowContentColumnState(const RendererWindowState &windowState)
		{
			ColumnFlags flags;
			AddObjectColumnFlags(windowState.structure.atoms, flags);
			AddObjectColumnFlags(windowState.structure.bonds, flags);
			AddObjectColumnFlags(windowState.pinnedMeasurements, flags);
			AddObjectColumnFlags(windowState.freeLabels, flags);
			AddObjectColumnFlags(windowState.sceneArrows, flags);
			AddObjectColumnFlags(windowState.sceneOrbitals, flags);
			AddObjectColumnFlags(windowState.scenePlanes, flags);
			return ColumnStateFrom(flags);
		}

		void ApplyWindowContentColumnEdit(RendererWindowState &windowState, const SceneVisibilityColumnEdit &edit)
		{
			if (!edit.visibleChanged && !edit.renderableChanged)
				return;
			entt::registry &registry = windowState.sceneRegistry.Registry();
			for (const entt::entity entity : registry.view<VisibilityComponent>())
			{
				VisibilityComponent &visibility = registry.get<VisibilityComponent>(entity);
				if (edit.visibleChanged)
					visibility.visible = edit.visible;
				if (edit.renderableChanged)
					visibility.renderable = edit.renderable;
			}
			ApplyGroupColumnEdit(windowState.pinnedMeasurements, edit);
			ApplyGroupColumnEdit(windowState.freeLabels, edit);
			ApplyGroupColumnEdit(windowState.sceneArrows, edit);
			ApplyGroupColumnEdit(windowState.sceneOrbitals, edit);
			ApplyGroupColumnEdit(windowState.scenePlanes, edit);
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		}

		// Selection highlight tint - ImGuiTreeNodeFlags_Selected's default background reads as
		// barely-there on this theme (see ProjectTreePanel.cpp's identical fix), so a selected
		// label/pin/arrow row gets a solid, more opaque tint pushed just for that one item.
		void PushSelectedRowColors()
		{
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.85f, 0.42f, 0.05f, 0.85f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.95f, 0.50f, 0.10f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.95f, 0.50f, 0.10f, 0.9f));
		}

		// The scene registry is the one enumeration point for annotations, but an entt view walks its
		// pool, and destroy() swap-and-pops - so rows would jump around every time an annotation is
		// added or deleted. Sorting by sourceIndex restores the vector order the user sees everywhere
		// else (properties panel, creation order in the viewport).
		[[nodiscard]] std::vector<std::size_t> CollectSourceIndices(const SceneRegistry &scene, SceneObjectKind kind)
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

		// Selecting any one annotation kind from the outliner clears the others -
		// the same mutual-exclusion rule RendererPanel::handleFreeLabelInteraction/
		// handlePinnedMeasurementInteraction/handleSceneArrowInteraction already enforce for a
		// viewport click, so outliner-driven selection can't leave a stale cross-kind selection a
		// viewport click never would.
		void ClearOtherAnnotationSelections(RendererWindowState &windowState, const std::vector<SceneObjectId> *keep)
		{
			if (&windowState.selectedFreeLabels != keep)
				windowState.selectedFreeLabels.clear();
			if (&windowState.selectedPinnedMeasurements != keep)
				windowState.selectedPinnedMeasurements.clear();
			if (&windowState.selectedSceneArrows != keep)
				windowState.selectedSceneArrows.clear();
			if (&windowState.selectedSceneOrbitals != keep)
				windowState.selectedSceneOrbitals.clear();
			if (&windowState.selectedScenePlanes != keep)
				windowState.selectedScenePlanes.clear();
		}
	} // namespace

	SceneOutlinerPanel::SceneOutlinerPanel(
		RendererLayer &layer,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<JobSystem> jobSystem,
		ElementPropertiesTable elementPropertiesTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable))
	{
	}

	Ref<IPanel> SceneOutlinerPanel::Clone() const
	{
		return CreateRef<SceneOutlinerPanel>(*this);
	}

	void SceneOutlinerPanel::drawAtomRow(RendererWindowState &windowState, std::size_t atomIndex)
	{
		Ref<EventBus> eventBus = m_Layer.GetEventBus();

		ImGui::PushID(static_cast<int>(atomIndex));
		bool visible = windowState.structure.atoms[atomIndex].visible;
		bool renderable = windowState.structure.atoms[atomIndex].renderable;
		if (DrawSceneVisibilityColumns(visible, renderable))
			SetAtomVisibilityColumns(windowState, {atomIndex}, visible, renderable);

		const bool isSelected = std::find(
									 windowState.selectedAtomIndices.begin(), windowState.selectedAtomIndices.end(), atomIndex) !=
			windowState.selectedAtomIndices.end();
		char label[64];
		std::snprintf(
			label, sizeof(label), "#%zu  (%.2f, %.2f, %.2f)", atomIndex,
			windowState.structure.atoms[atomIndex].cartesianPosition.x,
			windowState.structure.atoms[atomIndex].cartesianPosition.y,
			windowState.structure.atoms[atomIndex].cartesianPosition.z);
		ImGui::Selectable(label, isSelected);
		if (ImGui::IsItemClicked() && eventBus != nullptr)
		{
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			event.atomIndex = atomIndex;
			event.additive = ImGui::GetIO().KeyCtrl;
			eventBus->Publish(event);
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawSpeciesGroup(
		RendererWindowState &windowState, const std::string &species, const std::vector<std::size_t> &atomIndices)
	{
		ImGui::PushID(species.c_str());

		const SceneVisibilityColumnEdit speciesEdit = DrawSceneVisibilityColumns(
			AtomGroupColumnState(windowState, atomIndices));
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

		char groupLabel[48];
		std::snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)", species.c_str(), atomIndices.size());
		const bool open = ImGui::TreeNodeEx(
			"##species", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
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
					drawAtomRow(windowState, atomIndices[static_cast<std::size_t>(row)]);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawFreeLabelRow(RendererWindowState &windowState, std::size_t labelIndex)
	{
		// "Free"/"Pin" discriminator prefix, not just the numeric index - drawLabelsGroup draws both
		// kinds' rows as siblings under the same tree node, and each restarts its own index from 0.
		ImGui::PushID("Free");
		ImGui::PushID(static_cast<int>(labelIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedFreeLabels;
		const SceneObjectId id = windowState.freeLabels[labelIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		DrawSceneVisibilityColumns(
			windowState.freeLabels[labelIndex].visible, windowState.freeLabels[labelIndex].renderable);
		const std::string &text = windowState.freeLabels[labelIndex].text;
		char rowLabel[96];
		std::snprintf(rowLabel, sizeof(rowLabel), "%s", text.empty() ? "(no text)" : text.c_str());

		if (isSelected)
			PushSelectedRowColors();
		ImGui::Selectable(rowLabel, isSelected);
		if (isSelected)
			ImGui::PopStyleColor(3);
		if (ImGui::IsItemClicked())
		{
			ClearOtherAnnotationSelections(windowState, &selection);
			if (ImGui::GetIO().KeyCtrl)
			{
				const auto existing = std::find(selection.begin(), selection.end(), id);
				if (existing != selection.end())
					selection.erase(existing);
				else
					selection.push_back(id);
			}
			else
			{
				selection = {id};
			}
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
		}
		ImGui::PopID();
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawPinnedMeasurementRow(RendererWindowState &windowState, std::size_t pinIndex)
	{
		ImGui::PushID("Pin");
		ImGui::PushID(static_cast<int>(pinIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedPinnedMeasurements;
		const SceneObjectId id = windowState.pinnedMeasurements[pinIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		DrawSceneVisibilityColumns(
			windowState.pinnedMeasurements[pinIndex].visible, windowState.pinnedMeasurements[pinIndex].renderable);
		const RendererWindowState::PinnedMeasurement &pin = windowState.pinnedMeasurements[pinIndex];
		char rowLabel[32];
		std::snprintf(rowLabel, sizeof(rowLabel), "%s #%zu", pin.atomIndices.size() == 2 ? "Bond length" : "Angle", pinIndex);

		if (isSelected)
			PushSelectedRowColors();
		ImGui::Selectable(rowLabel, isSelected);
		if (isSelected)
			ImGui::PopStyleColor(3);
		if (ImGui::IsItemClicked())
		{
			ClearOtherAnnotationSelections(windowState, &selection);
			if (ImGui::GetIO().KeyCtrl)
			{
				const auto existing = std::find(selection.begin(), selection.end(), id);
				if (existing != selection.end())
					selection.erase(existing);
				else
					selection.push_back(id);
			}
			else
			{
				selection = {id};
			}
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
		}
		ImGui::PopID();
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawLabelsGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##labelsGroup");
		char groupLabel[32];
		// One header over two collections - free labels and pinned measurements both live under it.
		{
			ColumnFlags flags;
			AddObjectColumnFlags(windowState.freeLabels, flags);
			AddObjectColumnFlags(windowState.pinnedMeasurements, flags);
			const SceneVisibilityColumnEdit edit = DrawSceneVisibilityColumns(ColumnStateFrom(flags));
			ApplyGroupColumnEdit(windowState.freeLabels, edit);
			ApplyGroupColumnEdit(windowState.pinnedMeasurements, edit);
		}
		std::snprintf(
			groupLabel, sizeof(groupLabel), "Labels (%zu)",
			windowState.freeLabels.size() + windowState.pinnedMeasurements.size());
		const bool open = ImGui::TreeNodeEx(
			"##labels", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		if (open)
		{
			for (const std::size_t index : CollectSourceIndices(windowState.sceneRegistry, SceneObjectKind::FreeLabel))
				drawFreeLabelRow(windowState, index);
			for (const std::size_t index : CollectSourceIndices(windowState.sceneRegistry, SceneObjectKind::PinnedMeasurement))
				drawPinnedMeasurementRow(windowState, index);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawSceneArrowRow(RendererWindowState &windowState, std::size_t arrowIndex)
	{
		ImGui::PushID(static_cast<int>(arrowIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedSceneArrows;
		const SceneObjectId id = windowState.sceneArrows[arrowIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		DrawSceneVisibilityColumns(
			windowState.sceneArrows[arrowIndex].visible, windowState.sceneArrows[arrowIndex].renderable);
		const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
		const char *kindLabel = arrow.kind == RendererWindowState::ArrowKind::Line ? "Line"
			: arrow.kind == RendererWindowState::ArrowKind::Arrow2D ? "Arrow 2D" : "Arrow 3D";
		char rowLabel[32];
		std::snprintf(rowLabel, sizeof(rowLabel), "%s #%zu", kindLabel, arrowIndex);

		if (isSelected)
			PushSelectedRowColors();
		ImGui::Selectable(rowLabel, isSelected);
		if (isSelected)
			ImGui::PopStyleColor(3);
		if (ImGui::IsItemClicked())
		{
			ClearOtherAnnotationSelections(windowState, &selection);
			if (ImGui::GetIO().KeyCtrl)
			{
				const auto existing = std::find(selection.begin(), selection.end(), id);
				if (existing != selection.end())
					selection.erase(existing);
				else
					selection.push_back(id);
			}
			else
			{
				selection = {id};
			}
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawArrowsGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##arrowsGroup");
		char groupLabel[32];
		ApplyGroupColumnEdit(
			windowState.sceneArrows, DrawSceneVisibilityColumns(ObjectGroupColumnState(windowState.sceneArrows)));
		std::snprintf(groupLabel, sizeof(groupLabel), "Arrows (%zu)", windowState.sceneArrows.size());
		const bool open = ImGui::TreeNodeEx(
			"##arrows", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		if (open)
		{
			for (const std::size_t index : CollectSourceIndices(windowState.sceneRegistry, SceneObjectKind::SceneArrow))
				drawSceneArrowRow(windowState, index);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawSceneOrbitalRow(RendererWindowState &windowState, std::size_t orbitalIndex)
	{
		ImGui::PushID(static_cast<int>(orbitalIndex));
		std::vector<SceneObjectId> &selection = windowState.selectedSceneOrbitals;
		const SceneObjectId id = windowState.sceneOrbitals[orbitalIndex].id;
		const bool isSelected = std::find(selection.begin(), selection.end(), id) != selection.end();
		DrawSceneVisibilityColumns(
			windowState.sceneOrbitals[orbitalIndex].visible, windowState.sceneOrbitals[orbitalIndex].renderable);
		char rowLabel[48];
		std::snprintf(
			rowLabel, sizeof(rowLabel), "%s #%zu",
			OrbitalPresetName(windowState.sceneOrbitals[orbitalIndex].preset), orbitalIndex);

		if (isSelected)
			PushSelectedRowColors();
		ImGui::Selectable(rowLabel, isSelected);
		if (isSelected)
			ImGui::PopStyleColor(3);
		if (ImGui::IsItemClicked())
		{
			ClearOtherAnnotationSelections(windowState, &selection);
			if (ImGui::GetIO().KeyCtrl)
			{
				const auto existing = std::find(selection.begin(), selection.end(), id);
				if (existing != selection.end())
					selection.erase(existing);
				else
					selection.push_back(id);
			}
			else
			{
				selection = {id};
			}
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawOrbitalsGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##orbitalsGroup");
		char groupLabel[32];
		ApplyGroupColumnEdit(
			windowState.sceneOrbitals, DrawSceneVisibilityColumns(ObjectGroupColumnState(windowState.sceneOrbitals)));
		std::snprintf(groupLabel, sizeof(groupLabel), "Orbitals (%zu)", windowState.sceneOrbitals.size());
		const bool open = ImGui::TreeNodeEx(
			"##orbitals", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		if (open)
		{
			for (const std::size_t index : CollectSourceIndices(windowState.sceneRegistry, SceneObjectKind::SceneOrbital))
				drawSceneOrbitalRow(windowState, index);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawPlanesGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##planesGroup");
		char groupLabel[32];
		ApplyGroupColumnEdit(
			windowState.scenePlanes, DrawSceneVisibilityColumns(ObjectGroupColumnState(windowState.scenePlanes)));
		std::snprintf(groupLabel, sizeof(groupLabel), "Planes (%zu)", windowState.scenePlanes.size());
		const bool open = ImGui::TreeNodeEx(
			"##planes", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		if (open)
		{
			for (std::size_t index = 0; index < windowState.scenePlanes.size(); ++index)
			{
				RendererWindowState::ScenePlane &plane = windowState.scenePlanes[index];
				ImGui::PushID(static_cast<int>(index));
				DrawSceneVisibilityColumns(plane.visible, plane.renderable);
				char rowLabel[32];
				std::snprintf(rowLabel, sizeof(rowLabel), "Plane #%zu", index);
				auto &selection = windowState.selectedScenePlanes;
				const bool selected =
					std::find(selection.begin(), selection.end(), plane.id) != selection.end();
				if (selected)
					PushSelectedRowColors();
				ImGui::Selectable(rowLabel, selected);
				if (selected)
					ImGui::PopStyleColor(3);
				if (ImGui::IsItemClicked())
				{
					ClearOtherAnnotationSelections(windowState, &selection);
					// Ctrl-click adds, same as every other row in this panel and in the viewport.
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
					SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::dispatchCopyViewAndVisibility(const RendererWindowState &source, const std::string &targetWindowId)
	{
		if (source.structure.domainStructureId.empty())
		{
			m_CopyError = "Source window has no editable structure.";
			return;
		}
		RendererWindowState *target = nullptr;
		for (RendererWindowState &candidate : m_Layer.GetWindows())
		{
			if (candidate.windowId == targetWindowId)
			{
				target = &candidate;
				break;
			}
		}
		if (target == nullptr || target->structure.domainStructureId.empty())
		{
			m_CopyError = "Target window has no editable structure.";
			return;
		}

		const std::optional<Uuid> sourceStructureId = ParseUuid(source.structure.domainStructureId);
		const std::optional<Uuid> targetStructureId = ParseUuid(target->structure.domainStructureId);
		if (!sourceStructureId.has_value() || !targetStructureId.has_value())
		{
			m_CopyError = "Source or target window's structure id is invalid.";
			return;
		}
		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
		{
			m_CopyError = "DomainLayer unavailable";
			return;
		}
		Ref<const StructureRecord> sourceRecord = domainLayer->Workspace().Structures().Find(*sourceStructureId).lock();
		Ref<const StructureRecord> targetRecord = domainLayer->Workspace().Structures().Find(*targetStructureId).lock();
		if (sourceRecord == nullptr || targetRecord == nullptr)
		{
			m_CopyError = "Source or target window's structure is no longer registered.";
			return;
		}
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_CopyError = "JobSystem unavailable";
			return;
		}

		m_CopySourceWindowId = source.windowId;
		m_CopyTargetWindowId = targetWindowId;
		m_PendingCopyJob = CreateRef<CopyWindowStateJob>(
			sourceRecord->structure, targetRecord->structure, m_ElementPropertiesTable);
		m_PendingCopyJobId = jobSystem->Submit(m_PendingCopyJob, JobPriority::Normal);
		m_CopyError.clear();
	}

	void SceneOutlinerPanel::pollCopyJob()
	{
		if (m_PendingCopyJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingCopyJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			const std::optional<StructureComparisonResult> &result = m_PendingCopyJob->GetResult();
			RendererWindowState *source = nullptr;
			RendererWindowState *target = nullptr;
			for (RendererWindowState &candidate : m_Layer.GetWindows())
			{
				if (candidate.windowId == m_CopySourceWindowId)
					source = &candidate;
				if (candidate.windowId == m_CopyTargetWindowId)
					target = &candidate;
			}

			if (result.has_value() && source != nullptr && target != nullptr)
			{
				// Only matched atoms carry an opinion - an unmatched target atom (no source
				// counterpart) keeps whatever visibility it already had, nothing to copy from it.
				for (const AtomDisplacement &match : result->matches)
				{
					if (match.referenceAtomIndex >= source->structure.atoms.size())
						continue;
					const bool sourceVisible = source->structure.atoms[match.referenceAtomIndex].visible;
					Entity atomEntity = target->sceneRegistry.AtomEntityAt(match.comparisonAtomIndex);
					if (atomEntity)
						atomEntity.GetComponent<VisibilityComponent>().visible = sourceVisible;
				}
				SceneSystem::PushSelectionAndVisibilityToWindowState(target->sceneRegistry, *target);

				if (Ref<EventBus> eventBus = m_Layer.GetEventBus())
				{
					RendererEvents::Viewport::ViewTransitionRequested event;
					event.windowId = target->windowId;
					event.targetView.target = source->camera->Target();
					event.targetView.distance = source->camera->Distance();
					event.targetView.yaw = source->camera->Yaw();
					event.targetView.pitch = source->camera->Pitch();
					event.targetView.roll = source->camera->Roll();
					event.targetView.projection = source->camera->Projection();
					event.sourceAction = "outliner.copy_view_and_visibility";
					eventBus->Publish(event);
				}
				m_CopyError.clear();
			}
			else if (!result.has_value())
			{
				m_CopyError = "Copy completed with no result";
			}
			else
			{
				m_CopyError = "Source or target window closed before the copy finished";
			}
		}
		else
		{
			m_CopyError = snapshot->errorMessage.empty() ? "Copy failed" : snapshot->errorMessage;
		}
		m_PendingCopyJob.reset();
		m_PendingCopyJobId = 0;
	}

	void SceneOutlinerPanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		pollCopyJob();

		std::vector<RendererWindowState> &windows = m_Layer.GetWindows();
		if (m_EditingWindowIndex >= static_cast<int>(windows.size()))
			m_EditingWindowIndex = -1;
		if (m_ActiveWindowIndex >= static_cast<int>(windows.size()))
			m_ActiveWindowIndex = -1;

		if (windows.empty())
			ImGui::TextDisabled("No open structures.");

		for (int i = 0; i < static_cast<int>(windows.size()); ++i)
		{
			RendererWindowState &windowState = windows[static_cast<std::size_t>(i)];
			ImGui::PushID(i);

			ApplyWindowContentColumnEdit(
				windowState, DrawSceneVisibilityColumns(WindowContentColumnState(windowState)));

			if (m_EditingWindowIndex == i)
			{
				if (m_JustStartedEditing)
				{
					ImGui::SetKeyboardFocusHere();
					m_JustStartedEditing = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				const bool committed = ImGui::InputText(
					"##rename", m_EditingBuffer.data(), m_EditingBuffer.size(),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				if (committed || ImGui::IsItemDeactivated())
				{
					if (m_EditingBuffer[0] != '\0')
						windowState.title = m_EditingBuffer.data();
					m_EditingWindowIndex = -1;
				}
			}
			else
			{
				const bool open = ImGui::TreeNodeEx(
					"##win", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
					windowState.title.c_str());
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					m_ActiveWindowIndex = i;
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_ActiveWindowIndex = i;
					m_EditingWindowIndex = i;
					std::snprintf(m_EditingBuffer.data(), m_EditingBuffer.size(), "%s", windowState.title.c_str());
					m_JustStartedEditing = true;
				}
				if (m_ActiveWindowIndex == i && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
					ImGui::IsKeyPressed(ImGuiKey_F2, false))
				{
					m_EditingWindowIndex = i;
					std::snprintf(m_EditingBuffer.data(), m_EditingBuffer.size(), "%s", windowState.title.c_str());
					m_JustStartedEditing = true;
				}

				if (ImGui::BeginPopupContextItem())
				{
					const bool isCopying = m_PendingCopyJob != nullptr;
					if (ImGui::BeginMenu("Copy view + visibility to", !isCopying))
					{
						bool anyOther = false;
						for (int otherIndex = 0; otherIndex < static_cast<int>(windows.size()); ++otherIndex)
						{
							if (otherIndex == i)
								continue;
							anyOther = true;
							const RendererWindowState &otherWindow = windows[static_cast<std::size_t>(otherIndex)];
							if (ImGui::MenuItem(otherWindow.title.c_str()))
								dispatchCopyViewAndVisibility(windowState, otherWindow.windowId);
						}
						if (!anyOther)
							ImGui::TextDisabled("No other open windows");
						ImGui::EndMenu();
					}
					ImGui::EndPopup();
				}

				if (open)
				{
					// Grouped by species rather than one flat atom-per-row list - a defect supercell's
					// atom count is unwieldy to scroll flat, but its handful of distinct elements
					// aren't. Recomputed every frame (cheap relative to everything else Render() already
					// walks per window) rather than cached, since atoms can be added/removed/retyped by
					// any command between frames with no dedicated invalidation hook to hang a cache off.
					std::map<std::string, std::vector<std::size_t>> speciesGroups;
					for (std::size_t atomIndex = 0; atomIndex < windowState.structure.atoms.size(); ++atomIndex)
						speciesGroups[windowState.structure.atoms[atomIndex].element].push_back(atomIndex);

					for (const auto &[species, atomIndices] : speciesGroups)
						drawSpeciesGroup(windowState, species, atomIndices);

					drawLabelsGroup(windowState);
					drawArrowsGroup(windowState);
					drawOrbitalsGroup(windowState);
					drawPlanesGroup(windowState);

					ImGui::TreePop();
				}
			}

			ImGui::PopID();
		}

		if (m_PendingCopyJob != nullptr)
			ImGui::TextDisabled("Copying view + visibility...");
		if (!m_CopyError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_CopyError.c_str());

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
