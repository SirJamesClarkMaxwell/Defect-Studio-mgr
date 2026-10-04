#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportDefectFrame.hpp"
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
			if (&windowState.selectedSceneOrbitals != keep)
				windowState.selectedSceneOrbitals.clear();
			windowState.selectedVacancies.clear();
			windowState.defectFrameSelected = false;
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
		WeakRef<CommandRegistry> commandRegistry,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_CommandRegistry(std::move(commandRegistry))
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
			if (windowState.windowId != m_DisplayedWindowId)
				continue;
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedSceneOrbitals.clear();
			windowState.selectedVacancies.clear();
			windowState.defectFrameSelected = false;
			windowState.selectedScenePlanes.clear();
			windowState.selectedScenePaths.clear();
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


} // namespace DefectStudio
