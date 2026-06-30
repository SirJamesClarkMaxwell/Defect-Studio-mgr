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
} // namespace DefectStudio::Tests
