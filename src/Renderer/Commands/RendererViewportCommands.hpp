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
	[[nodiscard]] Unique<ICommand> CreateRendererHideSelectionCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererShowAllCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererSelectionInvertCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererSetAsDefaultViewCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererApplyDefaultViewCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLoadTestOrbitalCommand(Ref<EventBus> eventBus);
	[[nodiscard]] Unique<ICommand> CreateRendererLoadTestOrbitalGpuCommand(Ref<EventBus> eventBus);
} // namespace DefectStudio
