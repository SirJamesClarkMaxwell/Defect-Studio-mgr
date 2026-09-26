#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"

#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/SceneObjectMultiSelection.hpp"
#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>

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
		// The window row stands for everything under it, in both columns.
		[[nodiscard]] SceneVisibilityColumnState WindowContentColumnState(const RendererWindowState &windowState)
		{
			SceneVisibilityColumnAccumulator flags;
			AddSceneVisibilityColumnFlags(windowState.structure.atoms, flags);
			AddSceneVisibilityColumnFlags(windowState.structure.bonds, flags);
			AddSceneVisibilityColumnFlags(windowState.pinnedMeasurements, flags);
			AddSceneVisibilityColumnFlags(windowState.freeLabels, flags);
			AddSceneVisibilityColumnFlags(windowState.sceneArrows, flags);
			AddSceneVisibilityColumnFlags(windowState.sceneOrbitals, flags);
			AddSceneVisibilityColumnFlags(windowState.scenePlanes, flags);
			if (windowState.paths != nullptr)
				windowState.paths->Store().Visit([&flags](const ScenePath &path) { flags.Add(path.visible, path.renderable); });
			return SceneVisibilityStateFrom(flags);
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
			ApplySceneVisibilityColumnEdit(windowState.pinnedMeasurements, edit);
			ApplySceneVisibilityColumnEdit(windowState.freeLabels, edit);
			ApplySceneVisibilityColumnEdit(windowState.sceneArrows, edit);
			ApplySceneVisibilityColumnEdit(windowState.sceneOrbitals, edit);
			ApplySceneVisibilityColumnEdit(windowState.scenePlanes, edit);
			if (windowState.paths != nullptr)
				ApplySceneVisibilityColumnEdit(windowState.paths->Store(), edit);
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		}

		[[nodiscard]] SceneOutlinerSelectionModifier CurrentSelectionModifier()
		{
			const ImGuiIO &io = ImGui::GetIO();
			if (io.KeyShift)
				return SceneOutlinerSelectionModifier::Range;
			if (io.KeyCtrl)
				return SceneOutlinerSelectionModifier::Toggle;
			return SceneOutlinerSelectionModifier::Replace;
		}

		void ClearOtherAnnotationSelections(
			RendererWindowState &windowState, const std::vector<SceneObjectId> *keep)
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
			if (&windowState.selectedScenePaths != keep)
				windowState.selectedScenePaths.clear();
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

	void SceneOutlinerPanel::applyAtomRowSelection(
		RendererWindowState &windowState, const std::vector<std::size_t> &orderedRows,
		const std::size_t clickedIndex)
	{
		const SceneOutlinerSelectionModifier modifier = CurrentSelectionModifier();
		const std::optional<std::size_t> anchor =
			m_SelectionAnchor.has_value() && m_SelectionAnchor->windowId == windowState.windowId &&
				m_SelectionAnchor->kind == SelectionRowKind::Atom
			? std::optional<std::size_t>{m_SelectionAnchor->atomIndex}
			: std::nullopt;
		std::vector<std::size_t> selection = ApplySceneOutlinerSelection(
			windowState.selectedAtomIndices, orderedRows, clickedIndex, anchor, modifier);

		if (modifier != SceneOutlinerSelectionModifier::Toggle)
		{
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
			ClearOtherAnnotationSelections(windowState, nullptr);
		}
		windowState.selectedAtomIndices = std::move(selection);
		for (std::size_t index = 0; index < windowState.structure.atoms.size(); ++index)
		{
			Entity atom = windowState.sceneRegistry.AtomEntityAt(index);
			if (atom)
				atom.GetComponent<SelectionComponent>().selected = std::find(
					windowState.selectedAtomIndices.begin(), windowState.selectedAtomIndices.end(), index) !=
					windowState.selectedAtomIndices.end();
		}
		SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
		m_SelectionAnchor = SelectionAnchor{windowState.windowId, SelectionRowKind::Atom, clickedIndex, {}};
	}

	void SceneOutlinerPanel::applyAnnotationRowSelection(
		RendererWindowState &windowState, const SelectionRowKind kind,
		const std::vector<SceneObjectId> &orderedRows, const SceneObjectId clickedId,
		std::vector<SceneObjectId> &selection)
	{
		const SceneOutlinerSelectionModifier modifier = CurrentSelectionModifier();
		const std::optional<SceneObjectId> anchor =
			m_SelectionAnchor.has_value() && m_SelectionAnchor->windowId == windowState.windowId &&
				m_SelectionAnchor->kind == kind
			? std::optional<SceneObjectId>{m_SelectionAnchor->objectId}
			: std::nullopt;
		std::vector<SceneObjectId> updated =
			ApplySceneOutlinerSelection(selection, orderedRows, clickedId, anchor, modifier);

		if (modifier != SceneOutlinerSelectionModifier::Toggle)
		{
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
			ClearOtherAnnotationSelections(windowState, &selection);
		}
		selection = std::move(updated);
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
		m_SelectionAnchor = SelectionAnchor{windowState.windowId, kind, 0, clickedId};
	}

	void SceneOutlinerPanel::selectAnnotationRowForContextMenu(
		RendererWindowState &windowState,
		const SelectionRowKind kind,
		const SceneObjectId clickedId,
		std::vector<SceneObjectId> &selection)
	{
		if (std::find(selection.begin(), selection.end(), clickedId) != selection.end())
			return;
		SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
		ClearOtherAnnotationSelections(windowState, &selection);
		selection = {clickedId};
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
		m_SelectionAnchor = SelectionAnchor{windowState.windowId, kind, 0, clickedId};
	}

	void SceneOutlinerPanel::clearSelection()
	{
		m_SelectionAnchor.reset();
		Ref<EventBus> eventBus = m_Layer.GetEventBus();
		for (RendererWindowState &windowState : m_Layer.GetWindows())
		{
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedSceneArrows.clear();
			windowState.selectedSceneOrbitals.clear();
			windowState.selectedScenePlanes.clear();
			windowState.selectedScenePaths.clear();
			windowState.sceneArrowQuickEditActive = false;
			if (eventBus == nullptr)
				continue;
			// An AtomSelectionRequested with no atomIndex is the renderer's own "select nothing" -
			// the same event an empty-space viewport click publishes, so the two paths clear the
			// selection through one piece of code rather than two.
			RendererEvents::Viewport::AtomSelectionRequested event;
			event.windowId = windowState.windowId;
			eventBus->Publish(event);
		}
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
		{
			// Hiding the panel while it held focus would otherwise leave nav switched on for good,
			// and the viewport's arrow-key camera shortcuts would stop working.
			if (m_HadKeyboardFocus)
				applyKeyboardNavFlag(false);
			return;
		}

		// Dear ImGui's own keyboard nav, switched on only while this panel holds focus: arrows walk
		// the rows and scroll them into view, Enter/Space activates the focused one. The app-wide flag
		// stays off on purpose (see ProjectTreePanel.hpp) because the viewport's camera shortcuts are
		// bare arrow keys - but the viewport is not focused while the outliner is, so the two never
		// contest a key.
		//
		// The flag is written at the END of the frame, never set-then-restored inside one: ImGui
		// latches ConfigFlags in NewFrame, so a value that does not outlive Render() is a value
		// NewFrame never sees. That was why the arrow keys did nothing at all.
		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			applyKeyboardNavFlag(false);
			return;
		}

		m_HadKeyboardFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
		// Esc drops the selection rather than only the nav highlight - the outliner's counterpart to
		// clicking empty space in the viewport.
		if (m_HadKeyboardFocus && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			clearSelection();

		pollCopyJob();

		std::vector<RendererWindowState> &windows = m_Layer.GetWindows();
		if (m_EditingWindowIndex >= static_cast<int>(windows.size()))
			m_EditingWindowIndex = -1;
		if (m_ActiveWindowIndex >= static_cast<int>(windows.size()))
			m_ActiveWindowIndex = -1;

		// Delete/Ctrl+C/Ctrl+D/Ctrl+V for whichever scene-object kind is selected, mirroring the
		// viewport's own shortcuts (ViewportLabelInteraction.cpp) exactly - same dispatcher, same
		// chords, just gated on this panel holding focus instead of the mouse hovering the 3D view.
		// Previously the row context menu was the only way to reach these from the outliner at all.
		if (m_HadKeyboardFocus && m_ActiveWindowIndex >= 0)
		{
			RendererWindowState &activeWindow = windows[static_cast<std::size_t>(m_ActiveWindowIndex)];
			const std::optional<SceneObjectEditKind> selectedKind = !activeWindow.selectedSceneArrows.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::Arrow}
				: !activeWindow.selectedSceneOrbitals.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::Orbital}
				: !activeWindow.selectedScenePlanes.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::Plane}
				: !activeWindow.selectedFreeLabels.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::FreeLabel}
				: std::nullopt;
			const ImGuiIO &io = ImGui::GetIO();
			if (selectedKind.has_value() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
				ExecuteSceneObjectEditAction(activeWindow, *selectedKind, SceneObjectEditAction::Delete);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && selectedKind.has_value())
				ExecuteSceneObjectEditAction(activeWindow, *selectedKind, SceneObjectEditAction::Copy);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && selectedKind.has_value())
				ExecuteSceneObjectEditAction(activeWindow, *selectedKind, SceneObjectEditAction::Duplicate);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
				ExecuteSceneObjectEditAction(
					activeWindow, selectedKind.value_or(SceneObjectEditKind::Arrow), SceneObjectEditAction::Paste);
		}

		if (windows.empty())
			ImGui::TextDisabled("No open structures.");

		for (int i = 0; i < static_cast<int>(windows.size()); ++i)
		{
			RendererWindowState &windowState = windows[static_cast<std::size_t>(i)];
			ImGui::PushID(i);

			if (m_EditingWindowIndex == i)
			{
				if (m_JustStartedEditing)
				{
					ImGui::SetKeyboardFocusHere();
					m_JustStartedEditing = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::SetNextItemAllowOverlap();
				const bool committed = ImGui::InputText(
					"##rename", m_EditingBuffer.data(), m_EditingBuffer.size(),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				const bool deactivated = ImGui::IsItemDeactivated();
				ApplyWindowContentColumnEdit(
					windowState, DrawSceneVisibilityColumns(WindowContentColumnState(windowState)));
				if (committed || deactivated)
				{
					if (m_EditingBuffer[0] != '\0')
						windowState.title = m_EditingBuffer.data();
					m_EditingWindowIndex = -1;
				}
			}
			else
			{
				ImGui::SetNextItemAllowOverlap();
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
				ApplyWindowContentColumnEdit(
					windowState, DrawSceneVisibilityColumns(WindowContentColumnState(windowState)));

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
					drawPathsGroup(windowState);

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
		applyKeyboardNavFlag(m_HadKeyboardFocus);
	}

	void SceneOutlinerPanel::applyKeyboardNavFlag(const bool enabled)
	{
		m_HadKeyboardFocus = enabled;
		ImGuiIO &io = ImGui::GetIO();
		if (enabled)
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		else
			io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	}
} // namespace DefectStudio
