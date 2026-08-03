#include <gtest/gtest.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/Commands/RendererViewportCommands.hpp"

namespace DefectStudio::Tests
{
	TEST(RendererViewportCommandTests, CommandExecutionPublishesViewportEvent)
	{
		auto eventBus = CreateRef<EventBus>();
		CommandRegistry registry;
		bool observed = false;
		int observedAxis = -1;
		auto subscription = eventBus->Subscribe<RendererEvents::Viewport::AlignToAxisRequested>(
			[&observed, &observedAxis](const RendererEvents::Viewport::AlignToAxisRequested &event) {
				observed = true;
				observedAxis = event.axis;
			});

		ASSERT_TRUE(registry.Register(
			CommandMeta{
				CommandID("renderer.align_axis_b"),
				"Renderer: Align to b axis",
				"Renderer",
				"Align active renderer viewport to lattice axis b.",
				{},
				CommandFlags::None},
			[eventBus](CommandContext &) {
				return CreateRendererAlignAxisCommand(eventBus, 1);
			}));

		const auto result = registry.Execute(CommandID("renderer.align_axis_b"));

		ASSERT_TRUE(result);
		EXPECT_TRUE(observed);
		EXPECT_EQ(observedAxis, 1);
	}

	TEST(RendererViewportCommandTests, QuarterTurnCommandPublishesDirectionEvent)
	{
		auto eventBus = CreateRef<EventBus>();
		bool observed = false;
		RendererEvents::Viewport::OrbitDirection observedDirection = RendererEvents::Viewport::OrbitDirection::Left;
		auto subscription = eventBus->Subscribe<RendererEvents::Viewport::OrbitQuarterTurnRequested>(
			[&observed, &observedDirection](const RendererEvents::Viewport::OrbitQuarterTurnRequested &event) {
				observed = true;
				observedDirection = event.direction;
			});

		Unique<ICommand> command = CreateRendererOrbitQuarterTurnCommand(
			eventBus,
			RendererEvents::Viewport::OrbitDirection::Up);
		CommandContext context;

		ASSERT_TRUE(command->Execute(context));
		EXPECT_TRUE(observed);
		EXPECT_EQ(observedDirection, RendererEvents::Viewport::OrbitDirection::Up);
	}

	TEST(RendererViewportCommandTests, SavedViewCommandsPublishEvents)
	{
		auto eventBus = CreateRef<EventBus>();
		bool saveObserved = false;
		int cycleDirection = 0;
		auto saveSubscription = eventBus->Subscribe<RendererEvents::Viewport::SaveCurrentViewRequested>(
			[&saveObserved](const RendererEvents::Viewport::SaveCurrentViewRequested &) {
				saveObserved = true;
			});
		auto cycleSubscription = eventBus->Subscribe<RendererEvents::Viewport::CycleSavedViewRequested>(
			[&cycleDirection](const RendererEvents::Viewport::CycleSavedViewRequested &event) {
				cycleDirection = event.direction;
			});

		CommandContext context;
		ASSERT_TRUE(CreateRendererSaveCurrentViewCommand(eventBus)->Execute(context));
		ASSERT_TRUE(CreateRendererCycleSavedViewCommand(eventBus, -1)->Execute(context));

		EXPECT_TRUE(saveObserved);
		EXPECT_EQ(cycleDirection, -1);
	}
} // namespace DefectStudio::Tests
