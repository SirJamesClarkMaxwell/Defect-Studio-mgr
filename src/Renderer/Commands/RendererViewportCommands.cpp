#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererViewportCommands.hpp"

#include <functional>
#include <string>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Assert.hpp"

namespace DefectStudio
{
	namespace
	{
		class RendererViewportEventCommand final : public ICommand
		{
		public:
			RendererViewportEventCommand(
				Ref<EventBus> eventBus,
				std::string description,
				std::function<void(EventBus &)> publish)
				: m_EventBus(std::move(eventBus)),
				  m_Description(std::move(description)),
				  m_Publish(std::move(publish))
			{
				DS_ASSERT(m_EventBus != nullptr, "RendererViewportEventCommand requires EventBus");
			}

			Result<void> Execute(CommandContext &) override
			{
				if (m_EventBus == nullptr || !m_Publish)
				{
					return StructuredError{
						ErrorCategory::Runtime,
						Severity::Error,
						"Renderer command failed.",
						"RendererViewportEventCommand has no EventBus or publish callback.",
						"Bind renderer commands during application bootstrap.",
						"RendererViewportEventCommand",
						"renderer.command.event_bus_unavailable"};
				}

				m_Publish(*m_EventBus);
				return {};
			}

			std::string Description() const override
			{
				return m_Description;
			}

		private:
			Ref<EventBus> m_EventBus;
			std::string m_Description;
			std::function<void(EventBus &)> m_Publish;
		};
	} // namespace

	Unique<ICommand> CreateRendererAlignAxisCommand(Ref<EventBus> eventBus, int axis)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer align to axis",
			[axis](EventBus &bus) {
				RendererEvents::Viewport::AlignToAxisRequested event;
				event.axis = axis;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererOrbitDirectionCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::OrbitDirection direction)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer orbit",
			[direction](EventBus &bus) {
				RendererEvents::Viewport::OrbitDirectionRequested event;
				event.direction = direction;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererOrbitQuarterTurnCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::OrbitDirection direction)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer quarter turn",
			[direction](EventBus &bus) {
				RendererEvents::Viewport::OrbitQuarterTurnRequested event;
				event.direction = direction;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererRollDirectionCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::RollDirection direction)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer roll",
			[direction](EventBus &bus) {
				RendererEvents::Viewport::RollDirectionRequested event;
				event.direction = direction;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererZoomDirectionCommand(
		Ref<EventBus> eventBus,
		RendererEvents::Viewport::ZoomDirection direction)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer zoom",
			[direction](EventBus &bus) {
				RendererEvents::Viewport::ZoomDirectionRequested event;
				event.direction = direction;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererFocusSelectedAtomCommand(Ref<EventBus> eventBus)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer focus selected atom",
			[](EventBus &bus) {
				RendererEvents::Viewport::FocusSelectedAtomRequested event;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererUndoViewCommand(Ref<EventBus> eventBus)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer undo view",
			[](EventBus &bus) {
				RendererEvents::Viewport::UndoViewRequested event;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererRedoViewCommand(Ref<EventBus> eventBus)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer redo view",
			[](EventBus &bus) {
				RendererEvents::Viewport::RedoViewRequested event;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererSaveCurrentViewCommand(Ref<EventBus> eventBus)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer save current view",
			[](EventBus &bus) {
				RendererEvents::Viewport::SaveCurrentViewRequested event;
				bus.Publish(event);
			});
	}

	Unique<ICommand> CreateRendererCycleSavedViewCommand(Ref<EventBus> eventBus, int direction)
	{
		return CreateUnique<RendererViewportEventCommand>(
			std::move(eventBus),
			"Renderer cycle saved view",
			[direction](EventBus &bus) {
				RendererEvents::Viewport::CycleSavedViewRequested event;
				event.direction = direction;
				bus.Publish(event);
			});
	}
} // namespace DefectStudio
