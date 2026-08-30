#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"

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
		[[nodiscard]] bool AnyContentHidden(const RendererWindowState &windowState)
		{
			for (const RendererAtomData &atom : windowState.structure.atoms)
				if (!atom.visible)
					return true;
			for (const RendererBondData &bond : windowState.structure.bonds)
				if (!bond.visible)
					return true;
			return false;
		}

		[[nodiscard]] bool AnyAtomHidden(const RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices)
		{
			for (const std::size_t index : atomIndices)
				if (index < windowState.structure.atoms.size() && !windowState.structure.atoms[index].visible)
					return true;
			return false;
		}

		// Same mechanism as H (hide selected)/Alt+H (show all) - see ViewModifier.cpp - just applied
		// to an explicit set of atom entities instead of only the current selection.
		void SetAtomsVisible(RendererWindowState &windowState, const std::vector<std::size_t> &atomIndices, bool visible)
		{
			SceneRegistry &scene = windowState.sceneRegistry;
			for (const std::size_t index : atomIndices)
			{
				Entity atomEntity = scene.AtomEntityAt(index);
				if (atomEntity)
					atomEntity.GetComponent<VisibilityComponent>().visible = visible;
			}
			SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);
		}

		void SetWindowContentVisible(RendererWindowState &windowState, bool visible)
		{
			entt::registry &registry = windowState.sceneRegistry.Registry();
			for (const entt::entity entity : registry.view<VisibilityComponent>())
				registry.get<VisibilityComponent>(entity).visible = visible;
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

		// Selecting any one of the three annotation kinds from the outliner clears the other two -
		// same three-way mutual exclusivity RendererPanel::handleFreeLabelInteraction/
		// handlePinnedMeasurementInteraction/handleSceneArrowInteraction already enforce for a
		// viewport click, so outliner-driven selection can't leave a stale cross-kind selection a
		// viewport click never would.
		void ClearOtherAnnotationSelections(
			RendererWindowState &windowState, std::vector<std::size_t> *keep)
		{
			if (&windowState.selectedFreeLabels != keep)
				windowState.selectedFreeLabels.clear();
			if (&windowState.selectedPinnedMeasurements != keep)
				windowState.selectedPinnedMeasurements.clear();
			if (&windowState.selectedSceneArrows != keep)
				windowState.selectedSceneArrows.clear();
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
		if (ImGui::Checkbox("##atomVisible", &visible))
			SetAtomsVisible(windowState, {atomIndex}, visible);
		ImGui::SameLine();

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

		bool visible = !AnyAtomHidden(windowState, atomIndices);
		if (ImGui::Checkbox("##speciesVisible", &visible))
			SetAtomsVisible(windowState, atomIndices, visible);
		ImGui::SameLine();

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
		std::vector<std::size_t> &selection = windowState.selectedFreeLabels;
		const bool isSelected = std::find(selection.begin(), selection.end(), labelIndex) != selection.end();
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
				const auto existing = std::find(selection.begin(), selection.end(), labelIndex);
				if (existing != selection.end())
					selection.erase(existing);
				else
					selection.push_back(labelIndex);
			}
			else
			{
				selection = {labelIndex};
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
		std::vector<std::size_t> &selection = windowState.selectedPinnedMeasurements;
		const bool isSelected = std::find(selection.begin(), selection.end(), pinIndex) != selection.end();
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
				const auto existing = std::find(selection.begin(), selection.end(), pinIndex);
				if (existing != selection.end())
					selection.erase(existing);
				else
					selection.push_back(pinIndex);
			}
			else
			{
				selection = {pinIndex};
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
		std::snprintf(
			groupLabel, sizeof(groupLabel), "Labels (%zu)",
			windowState.freeLabels.size() + windowState.pinnedMeasurements.size());
		const bool open = ImGui::TreeNodeEx(
			"##labels", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		if (open)
		{
			for (std::size_t i = 0; i < windowState.freeLabels.size(); ++i)
				drawFreeLabelRow(windowState, i);
			for (std::size_t i = 0; i < windowState.pinnedMeasurements.size(); ++i)
				drawPinnedMeasurementRow(windowState, i);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawSceneArrowRow(RendererWindowState &windowState, std::size_t arrowIndex)
	{
		ImGui::PushID(static_cast<int>(arrowIndex));
		std::vector<std::size_t> &selection = windowState.selectedSceneArrows;
		const bool isSelected = std::find(selection.begin(), selection.end(), arrowIndex) != selection.end();
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
				const auto existing = std::find(selection.begin(), selection.end(), arrowIndex);
				if (existing != selection.end())
					selection.erase(existing);
				else
					selection.push_back(arrowIndex);
			}
			else
			{
				selection = {arrowIndex};
			}
		}
		ImGui::PopID();
	}

	void SceneOutlinerPanel::drawArrowsGroup(RendererWindowState &windowState)
	{
		ImGui::PushID("##arrowsGroup");
		char groupLabel[32];
		std::snprintf(groupLabel, sizeof(groupLabel), "Arrows (%zu)", windowState.sceneArrows.size());
		const bool open = ImGui::TreeNodeEx(
			"##arrows", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		if (open)
		{
			for (std::size_t i = 0; i < windowState.sceneArrows.size(); ++i)
				drawSceneArrowRow(windowState, i);
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

			bool visible = !AnyContentHidden(windowState);
			if (ImGui::Checkbox("##visible", &visible))
				SetWindowContentVisible(windowState, visible);
			ImGui::SameLine();

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
