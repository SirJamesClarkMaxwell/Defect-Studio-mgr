#pragma once

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/RendererEvents.hpp"

namespace DefectStudio
{
	class EventBus;

	[[nodiscard]] Unique<ICommand> CreateRendererAlignAxisCommand(Ref<EventBus> eventBus, int axis);
	[[nodiscard]] Unique<ICommand> CreateRendererOrbitDirectionCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::OrbitDirection direction);
	[[nodiscard]] Unique<ICommand> CreateRendererPanDirectionCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::OrbitDirection direction);
	[[nodiscard]] Unique<ICommand> CreateRendererOrbitQuarterTurnCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::OrbitDirection direction);
	[[nodiscard]] Unique<ICommand> CreateRendererRollDirectionCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::RollDirection direction);
	[[nodiscard]] Unique<ICommand> CreateRendererZoomDirectionCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::ZoomDirection direction);
	[[nodiscard]] Unique<ICommand> CreateRendererFocusSelectedAtomCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererUndoViewCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererRedoViewCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererSaveCurrentViewCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererCycleSavedViewCommand(Ref<EventBus> eventBus, int direction);
	[[nodiscard]] Unique<ICommand> CreateRendererExportImageCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererSelectionToolToggleCommand(
		Ref<EventBus> eventBus,
		SelectionToolMode tool);
	[[nodiscard]] Unique<ICommand> CreateRendererGizmoOperationCommand(Ref<EventBus> eventBus, GizmoOperation operation);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsToggleCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsToggleSelectedBondCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsToggleSelectedAngleCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsRemoveSelectedBondCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsShowAllBondCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsRemoveAllBondCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsRemoveSelectedAngleCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsShowAllAngleCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsRemoveAllAngleCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLabelsToggleBondAlignmentCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererUndoLabelsCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererRedoLabelsCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererHideSelectionCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererShowAllCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererSelectionInvertCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererSelectAllCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererSetAsDefaultViewCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererApplyDefaultViewCommand(Ref<EventBus> eventBus);
} // namespace DefectStudio
