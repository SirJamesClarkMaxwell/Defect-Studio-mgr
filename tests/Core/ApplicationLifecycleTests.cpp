#include <gtest/gtest.h>

#include "App/ApplicationLifecycle.hpp"
#include "Core/Event.hpp"

TEST(ApplicationLifecycleTests, CreateGuardPreventsDoubleCreate)
{
	DefectStudio::ApplicationLifecycleState state;

	EXPECT_TRUE(state.TryMarkCreated());
	EXPECT_TRUE(state.IsCreated());
	EXPECT_FALSE(state.TryMarkCreated());
}

TEST(ApplicationLifecycleTests, ShutdownIsIdempotent)
{
	DefectStudio::ApplicationLifecycleState state;
	EXPECT_TRUE(state.TryMarkCreated());
	state.SetRunning(true);

	EXPECT_TRUE(state.TryBeginShutdown());
	EXPECT_FALSE(state.IsCreated());
	EXPECT_FALSE(state.IsRunning());

	EXPECT_FALSE(state.TryBeginShutdown());
	EXPECT_FALSE(state.IsCreated());
	EXPECT_FALSE(state.IsRunning());
}

TEST(ApplicationLifecycleTests, WindowCloseEventStopsLoop)
{
	DefectStudio::ApplicationLifecycleState state;
	EXPECT_TRUE(state.TryMarkCreated());
	state.SetRunning(true);

	DefectStudio::WindowCloseEvent closeEvent;
	DefectStudio::HandleLifecycleEvent(closeEvent, state);

	EXPECT_TRUE(closeEvent.handled);
	EXPECT_FALSE(state.IsRunning());
}

TEST(ApplicationLifecycleTests, NonCloseEventDoesNotStopLoop)
{
	DefectStudio::ApplicationLifecycleState state;
	EXPECT_TRUE(state.TryMarkCreated());
	state.SetRunning(true);

	DefectStudio::WindowResizeEvent resizeEvent(1280, 720);
	DefectStudio::HandleLifecycleEvent(resizeEvent, state);

	EXPECT_FALSE(resizeEvent.handled);
	EXPECT_TRUE(state.IsRunning());
}
