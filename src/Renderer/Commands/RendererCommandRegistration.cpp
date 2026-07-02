#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererCommandRegistration.hpp"

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererViewportCommands.hpp"

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
	}

	void RegisterRendererCommands(CommandRegistry &registry, Ref<EventBus> eventBus)
	{
		using namespace RendererEvents::Viewport;

		RegisterRendererCommand(
			registry,
			"renderer.align_axis_a",
			"Renderer: Align to a axis",
			"Align active renderer viewport to lattice axis a.",
			[eventBus](CommandContext &) { return CreateRendererAlignAxisCommand(eventBus, 0); });
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_b",
			"Renderer: Align to b axis",
			"Align active renderer viewport to lattice axis b.",
			[eventBus](CommandContext &) { return CreateRendererAlignAxisCommand(eventBus, 1); });
		RegisterRendererCommand(
			registry,
			"renderer.align_axis_c",
			"Renderer: Align to c axis",
			"Align active renderer viewport to lattice axis c.",
			[eventBus](CommandContext &) { return CreateRendererAlignAxisCommand(eventBus, 2); });
		RegisterRendererCommand(
			registry,
			"renderer.orbit_left",
			"Renderer: Orbit left",
			"Orbit active renderer viewport left.",
			[eventBus](CommandContext &) { return CreateRendererOrbitDirectionCommand(eventBus, OrbitDirection::Left); });
		RegisterRendererCommand(
			registry,
			"renderer.orbit_right",
			"Renderer: Orbit right",
			"Orbit active renderer viewport right.",
			[eventBus](CommandContext &) { return CreateRendererOrbitDirectionCommand(eventBus, OrbitDirection::Right); });
		RegisterRendererCommand(
			registry,
			"renderer.orbit_up",
			"Renderer: Orbit up",
			"Orbit active renderer viewport up.",
			[eventBus](CommandContext &) { return CreateRendererOrbitDirectionCommand(eventBus, OrbitDirection::Up); });
		RegisterRendererCommand(
			registry,
			"renderer.orbit_down",
			"Renderer: Orbit down",
			"Orbit active renderer viewport down.",
			[eventBus](CommandContext &) { return CreateRendererOrbitDirectionCommand(eventBus, OrbitDirection::Down); });
		RegisterRendererCommand(
			registry,
			"renderer.roll_left",
			"Renderer: Roll left",
			"Roll active renderer viewport left.",
			[eventBus](CommandContext &) { return CreateRendererRollDirectionCommand(eventBus, RollDirection::Left); });
		RegisterRendererCommand(
			registry,
			"renderer.roll_right",
			"Renderer: Roll right",
			"Roll active renderer viewport right.",
			[eventBus](CommandContext &) { return CreateRendererRollDirectionCommand(eventBus, RollDirection::Right); });
		RegisterRendererCommand(
			registry,
			"renderer.zoom_in",
			"Renderer: Zoom in",
			"Zoom active renderer viewport in.",
			[eventBus](CommandContext &) { return CreateRendererZoomDirectionCommand(eventBus, ZoomDirection::In); });
		RegisterRendererCommand(
			registry,
			"renderer.zoom_out",
			"Renderer: Zoom out",
			"Zoom active renderer viewport out.",
			[eventBus](CommandContext &) { return CreateRendererZoomDirectionCommand(eventBus, ZoomDirection::Out); });
		RegisterRendererCommand(
			registry,
			"renderer.focus_selected_atom",
			"Renderer: Focus selected atom",
			"Focus active renderer viewport on the selected atom.",
			[eventBus](CommandContext &) { return CreateRendererFocusSelectedAtomCommand(eventBus); });
		RegisterRendererCommand(
			registry,
			"renderer.undo_view",
			"Renderer: Undo view",
			"Undo the last view change in the active renderer viewport.",
			[eventBus](CommandContext &) { return CreateRendererUndoViewCommand(eventBus); });
		RegisterRendererCommand(
			registry,
			"renderer.redo_view",
			"Renderer: Redo view",
			"Redo the last view change in the active renderer viewport.",
			[eventBus](CommandContext &) { return CreateRendererRedoViewCommand(eventBus); });
	}
} // namespace DefectStudio
