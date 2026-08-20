#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererCommandRegistration.hpp"

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererViewportCommands.hpp"

#include <functional>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		void RegisterRendererCommand(
			CommandRegistry &registry,
			const char *id,
			const char *name,
			const char *description,
			CommandFactory factory)
		{
			auto result = registry.Register(
				CommandMeta{
					CommandID{id},
					name,
					"Renderer",
					description,
					{},
					CommandFlags::None},
				std::move(factory));
			if (!result)
				DS_LOG_WARN("Renderer command registration failed: {}", result.Error().technicalDetails);
		}

		Unique<ICommand> MakeAlignAxisCommand(Ref<EventBus> eventBus, int axis, CommandContext &)
		{
			return CreateRendererAlignAxisCommand(std::move(eventBus), axis);
		}

		Unique<ICommand> MakeOrbitDirectionCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::OrbitDirection direction,
			CommandContext &)
		{
			return CreateRendererOrbitDirectionCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeOrbitQuarterTurnCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::OrbitDirection direction,
			CommandContext &)
		{
			return CreateRendererOrbitQuarterTurnCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeRollDirectionCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::RollDirection direction,
			CommandContext &)
		{
			return CreateRendererRollDirectionCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeZoomDirectionCommand(
			Ref<EventBus> eventBus,
			RendererEvents::Viewport::ZoomDirection direction,
			CommandContext &)
		{
			return CreateRendererZoomDirectionCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeFocusSelectedAtomCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererFocusSelectedAtomCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeUndoViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererUndoViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeRedoViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererRedoViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSaveCurrentViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSaveCurrentViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeCycleSavedViewCommand(Ref<EventBus> eventBus, int direction, CommandContext &)
		{
			return CreateRendererCycleSavedViewCommand(std::move(eventBus), direction);
		}

		Unique<ICommand> MakeExportImageCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererExportImageCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSelectionToolToggleCommand(Ref<EventBus> eventBus, SelectionToolMode tool, CommandContext &)
		{
			return CreateRendererSelectionToolToggleCommand(std::move(eventBus), tool);
		}

		Unique<ICommand> MakeHideSelectionCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererHideSelectionCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeShowAllCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererShowAllCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSelectionInvertCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSelectionInvertCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeSetAsDefaultViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererSetAsDefaultViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeApplyDefaultViewCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererApplyDefaultViewCommand(std::move(eventBus));
		}

		Unique<ICommand> MakeLoadTestOrbitalCommand(Ref<EventBus> eventBus, CommandContext &)
		{
			return CreateRendererLoadTestOrbitalCommand(std::move(eventBus));
		}
	}

	void RegisterRendererCommands(CommandRegistry &registry, Ref<EventBus> eventBus)
	{
		using namespace RendererEvents::Viewport;

		RegisterRendererCommand(
			registry,
			"renderer.align_axis_a",
			"Renderer: Align to a axis",
			"Align active renderer viewport to lattice axis a.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 0));
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_b",
			"Renderer: Align to b axis",
			"Align active renderer viewport to lattice axis b.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 1));
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_c",
			"Renderer: Align to c axis",
			"Align active renderer viewport to lattice axis c.",
			std::bind_front(MakeAlignAxisCommand, eventBus, 2));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_left",
			"Renderer: Orbit left",
			"Orbit active renderer viewport left.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Left));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_right",
			"Renderer: Orbit right",
			"Orbit active renderer viewport right.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Right));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_up",
			"Renderer: Orbit up",
			"Orbit active renderer viewport up.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Up));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_down",
			"Renderer: Orbit down",
			"Orbit active renderer viewport down.",
			std::bind_front(MakeOrbitDirectionCommand, eventBus, OrbitDirection::Down));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_left_90",
			"Renderer: Orbit left 90",
			"Orbit active renderer viewport left by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Left));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_right_90",
			"Renderer: Orbit right 90",
			"Orbit active renderer viewport right by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Right));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_up_90",
			"Renderer: Orbit up 90",
			"Orbit active renderer viewport up by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Up));
		RegisterRendererCommand(
			registry,
			"renderer.orbit_down_90",
			"Renderer: Orbit down 90",
			"Orbit active renderer viewport down by 90 degrees.",
			std::bind_front(MakeOrbitQuarterTurnCommand, eventBus, OrbitDirection::Down));
		RegisterRendererCommand(
			registry,
			"renderer.roll_left",
			"Renderer: Roll left",
			"Roll active renderer viewport left.",
			std::bind_front(MakeRollDirectionCommand, eventBus, RollDirection::Left));
		RegisterRendererCommand(
			registry,
			"renderer.roll_right",
			"Renderer: Roll right",
			"Roll active renderer viewport right.",
			std::bind_front(MakeRollDirectionCommand, eventBus, RollDirection::Right));
		RegisterRendererCommand(
			registry,
			"renderer.zoom_in",
			"Renderer: Zoom in",
			"Zoom active renderer viewport in.",
			std::bind_front(MakeZoomDirectionCommand, eventBus, ZoomDirection::In));
		RegisterRendererCommand(
			registry,
			"renderer.zoom_out",
			"Renderer: Zoom out",
			"Zoom active renderer viewport out.",
			std::bind_front(MakeZoomDirectionCommand, eventBus, ZoomDirection::Out));
		RegisterRendererCommand(
			registry,
			"renderer.focus_selected_atom",
			"Renderer: Focus selected atom",
			"Focus active renderer viewport on the selected atom.",
			std::bind_front(MakeFocusSelectedAtomCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.undo_view",
			"Renderer: Undo view",
			"Undo the last view change in the active renderer viewport.",
			std::bind_front(MakeUndoViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.redo_view",
			"Renderer: Redo view",
			"Redo the last view change in the active renderer viewport.",
			std::bind_front(MakeRedoViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.view.save_current",
			"Renderer: Save current view",
			"Save the active renderer viewport camera view.",
			std::bind_front(MakeSaveCurrentViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.view.cycle_next",
			"Renderer: Next saved view",
			"Cycle to the next saved renderer viewport camera view.",
			std::bind_front(MakeCycleSavedViewCommand, eventBus, 1));
		RegisterRendererCommand(
			registry,
			"renderer.view.cycle_previous",
			"Renderer: Previous saved view",
			"Cycle to the previous saved renderer viewport camera view.",
			std::bind_front(MakeCycleSavedViewCommand, eventBus, -1));
		RegisterRendererCommand(
			registry,
			"renderer.export.image",
			"Renderer: Export image (PNG)",
			"Save the active renderer viewport's current frame to exports/ as a PNG.",
			std::bind_front(MakeExportImageCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.box_select_toggle",
			"Renderer: Toggle box-select",
			"Toggle box drag-select on the active renderer viewport.",
			std::bind_front(MakeSelectionToolToggleCommand, eventBus, SelectionToolMode::Box));
		RegisterRendererCommand(
			registry,
			"renderer.selection.circle_select_toggle",
			"Renderer: Toggle circle-select",
			"Toggle circle drag-select on the active renderer viewport.",
			std::bind_front(MakeSelectionToolToggleCommand, eventBus, SelectionToolMode::Circle));
		RegisterRendererCommand(
			registry,
			"renderer.selection.hide",
			"Renderer: Hide selection",
			"Hide the currently selected atoms in the active renderer viewport.",
			std::bind_front(MakeHideSelectionCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.show_all",
			"Renderer: Show all",
			"Make all atoms visible in the active renderer viewport.",
			std::bind_front(MakeShowAllCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.selection.invert",
			"Renderer: Invert selection",
			"Invert the atom selection in the active renderer viewport.",
			std::bind_front(MakeSelectionInvertCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.view.set_as_project_default",
			"Renderer: Set as default view",
			"Save the active renderer viewport's current view as the session default view.",
			std::bind_front(MakeSetAsDefaultViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.view.apply_project_default",
			"Renderer: Apply default view",
			"Apply the session default view to the active renderer viewport.",
			std::bind_front(MakeApplyDefaultViewCommand, eventBus));
		RegisterRendererCommand(
			registry,
			"renderer.debug.load_test_orbital",
			"Renderer: Load test orbital (debug)",
			"T08.6 debug: loads a hardcoded fixture orbital and overlays its isosurface on the active viewport.",
			std::bind_front(MakeLoadTestOrbitalCommand, eventBus));
	}
} // namespace DefectStudio
