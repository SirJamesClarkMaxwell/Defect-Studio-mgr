#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportModalCommit.hpp"

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool CommitAtomPreview(
			RendererWindowState &windowState, const SceneTransformSelectionSnapshot &snapshot,
			const WeakRef<CommandRegistry> &commandRegistryRef, std::string_view description)
		{
			if (windowState.structure.domainStructureId.empty())
				return false;
			Ref<CommandRegistry> commandRegistry = commandRegistryRef.lock();
			if (commandRegistry == nullptr)
				return false;

			GizmoTransformPayload payload;
			payload.windowId = windowState.windowId;
			payload.description = description;
			for (const AtomTransformStart &start : snapshot.atoms)
			{
				if (start.index >= windowState.structure.atoms.size())
					continue;
				payload.atomIndices.push_back(start.index);
				payload.afterPositions.push_back(windowState.structure.atoms[start.index].cartesianPosition);
			}
			if (payload.atomIndices.empty())
				return false;

			CommandContext context;
			context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
			Result<CommandOutcome> result =
				commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
			if (!result)
				DS_LOG_WARN("Gizmo transform commit failed: {}", result.Error().technicalDetails);
			return result.HasValue();
		}

		// Vacancies and the defect axes are domain data: the preview moved the renderer copies, the
		// commit edits the domain through the undoable set commands (one each), which rebuild the copies.
		void CommitDefectMarkerPreview(RendererWindowState &windowState, const SceneTransformSelectionSnapshot &snapshot,
			const WeakRef<CommandRegistry> &commandRegistryRef, std::string_view description)
		{
			Ref<CommandRegistry> commandRegistry = commandRegistryRef.lock();
			if (commandRegistry == nullptr)
				return;
			auto run = [&](const char *commandId, const char *payloadKey, auto payload) {
				CommandContext context;
				context.Set<decltype(payload)>(payloadKey, std::move(payload));
				const auto result = commandRegistry->Execute(CommandID{commandId}, std::move(context));
				if (!result)
					DS_LOG_WARN("Defect marker transform commit failed: {}", result.Error().technicalDetails);
			};
			if (!snapshot.vacancies.empty())
			{
				std::vector<std::pair<std::size_t, glm::vec3>> moved;
				for (const VacancyTransformStart &start : snapshot.vacancies)
					if (start.index < windowState.structure.vacancies.size())
						moved.emplace_back(start.index, windowState.structure.vacancies[start.index].cartesianPosition);
				SetVacanciesPayload payload{windowState.windowId, {}, std::string(description)};
				payload.edit = [moved](std::vector<VacancySite> &list, const CrystalStructure &structure) {
					for (const auto &[index, position] : moved)
						if (index < list.size())
						{
							list[index].position = position;
							list[index].fractional = structure.CartesianToFractional(position);
						}
				};
				run(kSetVacanciesCommandId, kSetVacanciesPayloadKey, std::move(payload));
			}
			if (snapshot.defectFrame && windowState.structure.defectFrame)
				run(kSetDefectFrameCommandId, kSetDefectFramePayloadKey,
					SetDefectFramePayload{windowState.windowId, windowState.structure.defectFrame, std::string(description)});
		}

	} // namespace

	[[nodiscard]] bool CommitViewportModalTransform(
		RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commandRegistryRef, std::string_view description)
	{
		const bool hasAtoms = HasAtomTransformTargets(windowState.modalTransformSelection);
		const bool hasSceneObjects = HasSceneObjectTransformTargets(windowState.modalTransformSelection) &&
			windowState.modalTransformSceneObjectsBefore.has_value();
		const SceneTransformSelectionSnapshot &selection = windowState.modalTransformSelection;
		const int markerCommands = static_cast<int>(!selection.vacancies.empty()) +
			static_cast<int>(selection.defectFrame.has_value());
		Ref<UndoStack> undoStack = layer.GetUndoStackHandle().lock();
		std::optional<UndoScope> group;
		if (static_cast<int>(hasAtoms) + static_cast<int>(hasSceneObjects) + markerCommands > 1 && undoStack != nullptr)
			group.emplace(*undoStack, std::string(description));

		if (hasSceneObjects)
			PushSceneObjectsUndoSnapshot(windowState, std::move(*windowState.modalTransformSceneObjectsBefore));
		const bool atomsCommitted = !hasAtoms ||
			CommitAtomPreview(windowState, windowState.modalTransformSelection, commandRegistryRef, description);
		if (markerCommands > 0)
			CommitDefectMarkerPreview(windowState, selection, commandRegistryRef, description);
		if (group.has_value())
		{
			const Result<void> committed = group->Commit();
			if (!committed)
				DS_LOG_WARN("Transform undo group commit failed: {}", committed.Error().technicalDetails);
		}
		return atomsCommitted;
	}

	bool ConfirmViewportModalTransform(RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commands, const SceneTransformDelta &delta, std::string_view description)
	{
		if (!windowState.modalTransform || !CanConfirmModalTransform(*windowState.modalTransform))
			return false;
		const auto session = *windowState.modalTransform;
		const bool duplicated = windowState.duplicateInPlacePending;
		bool committed = CommitViewportModalTransform(windowState, layer, commands, description);
		if (std::exchange(windowState.duplicateInPlacePending, false))
		{
			if (!committed)
				RestoreSceneTransformSelection(windowState, windowState.modalTransformSelection);
			if (Ref<UndoStack> undoStack = layer.GetUndoStackHandle().lock())
			{
				const Result<void> closed = committed ? undoStack->EndGroup() : undoStack->CancelGroup();
				if (!closed)
				{
					DS_LOG_WARN("Duplicate undo group close failed: {}", closed.Error().technicalDetails);
					committed = false;
				}
			}
		}
		if (committed)
			windowState.lastConfirmedTransform = RendererWindowState::ConfirmedTransform{
				session.op, session.constraint, session.orientation, delta,
				session.pickedPivotLabel.empty() ? windowState.transformPivotMode : TransformPivotMode::Median, duplicated};
		return committed;
	}

	void ResetViewportModalTransform(RendererWindowState &windowState)
	{
		windowState.modalTransform.reset();
		windowState.modalTransformSelection = {};
		windowState.modalTransformSceneObjectsBefore.reset();
		windowState.modalTransformStartedFromHandle = false;
		windowState.gizmoDragActive = false;
		windowState.modalSnapTargets.clear();
		windowState.modalSnapTargetHit.reset();
		windowState.modalPickedAxis = {};
		windowState.modalTransformPreviewSelection.reset();
	}

} // namespace DefectStudio
