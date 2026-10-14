#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportModalCommit.hpp"

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ViewportModalTransform.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	void BeginInPlaceDuplicateMove(
		RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commandRegistryRef, const glm::vec2 &mouse)
	{
		Ref<CommandRegistry> registry = commandRegistryRef.lock();
		Ref<UndoStack> undoStack = layer.GetUndoStackHandle().lock();
		if (registry == nullptr || undoStack == nullptr || undoStack->HasActiveGroup() ||
			windowState.camera == nullptr || windowState.modalTransform.has_value())
			return;
		if (windowState.selectedAtomIndices.empty() && !HasSceneObjectSelection(windowState))
			return;

		const Result<void> begun = undoStack->BeginGroup("Duplicate and transform selection");
		if (!begun)
		{
			DS_LOG_WARN("Duplicate undo group failed: {}", begun.Error().technicalDetails);
			return;
		}
		CommandContext context;
		context.Set("renderer.window_id", windowState.windowId);
		context.Set("renderer.duplicate_offset", glm::vec3(0.0f));
		const Ref<bool> handled = CreateRef<bool>(false);
		context.Set("scene_objects.shortcut_action", std::string("renderer.selection.duplicate"));
		context.Set("scene_objects.shortcut_handled", handled);
		Result<CommandOutcome> result =
			registry->Execute(CommandID{"renderer.scene_objects.shortcut"}, context);
		if (result && !*handled)
			result = registry->Execute(CommandID{"renderer.selection.duplicate"}, std::move(context));
		if (result)
		{
			if (!*handled)
			{
				windowState.selectedPinnedMeasurements.clear();
				windowState.selectedFreeLabels.clear();
				windowState.selectedSceneOrbitals.clear();
				windowState.selectedScenePlanes.clear();
				windowState.selectedScenePaths.clear();
				windowState.selectedSceneDensities.clear();
				windowState.selectedSceneHideVolumes.clear();
				windowState.selectedVacancies.clear();
				windowState.defectFrameSelected = false;
				SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			}
			windowState.duplicateInPlacePending = true;
			BeginViewportModalTransform(windowState, ModalTransformOp::Translate, mouse);
			if (windowState.modalTransform.has_value())
				return;
			DS_LOG_WARN("In-place duplicate cancelled: the copied selection has no modal transform targets.");
		}
		else
			DS_LOG_WARN("In-place duplicate failed: {}", result.Error().technicalDetails);
		windowState.duplicateInPlacePending = false;
		const Result<void> cancelled = undoStack->CancelGroup();
		if (!cancelled)
			DS_LOG_WARN("Duplicate rollback failed: {}", cancelled.Error().technicalDetails);
	}
	bool RepeatViewportModalTransform(RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commands)
	{
		if (!windowState.lastConfirmedTransform || windowState.modalTransform ||
			SceneTransformPivotPositions(windowState, CaptureSceneTransformSelection(windowState)).empty())
		{
			DS_LOG_INFO("Repeat last transform: no confirmed operation or no selected transform targets.");
			return false;
		}
		const auto last = *windowState.lastConfirmedTransform;
		const auto undoStack = layer.GetUndoStackHandle().lock();
		if (!undoStack || undoStack->HasActiveGroup() || !commands.lock())
			return false;
		if (last.duplicated)
			BeginInPlaceDuplicateMove(windowState, layer, commands, glm::vec2(0.0f));
		else
			BeginViewportModalTransform(windowState, last.op, glm::vec2(0.0f));
		if (!windowState.modalTransform)
			return false;
		auto &session = *windowState.modalTransform;
		session.op = last.op;
		session.constraint = last.constraint;
		session.orientation = last.orientation;
		const auto positions = SceneTransformPivotPositions(windowState, windowState.modalTransformSelection);
		const auto cursor = windowState.cursor3DPlaced ? std::optional<glm::vec3>(windowState.cursor3DPosition) : std::nullopt;
		session.pivot = ComputeTransformPivot(last.pivotMode, positions, cursor);
		// Use the saved pivot policy for both application and confirmation, without changing the toolbar preference.
		const auto preference = windowState.transformPivotMode;
		windowState.transformPivotMode = last.pivotMode;
		ApplySceneTransformSelection(windowState, windowState.modalTransformSelection, last.delta,
			last.op, last.pivotMode, session.pivot);
		const bool committed = ConfirmViewportModalTransform(windowState, layer, commands, last.delta, "Repeat last transform");
		if (!committed && !last.duplicated)
			RestoreSceneTransformSelection(windowState, windowState.modalTransformSelection);
		windowState.transformPivotMode = preference;
		ResetViewportModalTransform(windowState);
		return committed;
	}
} // namespace DefectStudio
