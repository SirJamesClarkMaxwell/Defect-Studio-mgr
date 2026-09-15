#include <gtest/gtest.h>

#include <utility>

#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}
	} // namespace

	class RendererViewHistoryTests : public testing::Test
	{
	protected:
		void TearDown() override
		{
			renderer.OnDetach();
		}

		RendererLayer renderer{EmptyRendererConfig()};
	};

	TEST_F(RendererViewHistoryTests, RollInteractionCreatesOneUndoEntryAndUndoRestoresStart)
	{
		RendererWindowState window;
		window.windowId = "w1";
		window.camera = CreateUnique<RendererViewCamera>();
		renderer.AddWindow(std::move(window));
		RendererWindowState &storedWindow = renderer.GetWindows().front();
		const float startRoll = storedWindow.camera->Roll();

		renderer.BeginViewInteraction(storedWindow.windowId, "keyboard.roll");
		storedWindow.camera->Roll(0.1f);
		storedWindow.camera->Roll(0.1f);
		storedWindow.camera->Roll(0.1f);
		renderer.CommitViewInteraction(storedWindow.windowId);

		ASSERT_EQ(storedWindow.viewUndoHistory.size(), 1u);
		renderer.UndoViewChange(storedWindow.windowId);
		renderer.UpdateCameraTransitions(1.0f);
		EXPECT_NEAR(storedWindow.camera->Roll(), startRoll, 0.0001f);
	}
} // namespace DefectStudio::Tests
