#include <gtest/gtest.h>

#include <atomic>

#include "App/Events/JobEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;
}

TEST(JobSystemEventsTests, PublishesQueuedStartedProgressAndCompletedEvents)
{
	auto eventBus = CreateRef<EventBus>();
	JobSystem jobSystem(eventBus);

	std::atomic<int> queuedCount = 0;
	std::atomic<int> startedCount = 0;
	std::atomic<int> progressCount = 0;
	std::atomic<int> completedCount = 0;

	auto queuedSub = eventBus->Subscribe<JobQueuedEvent>([&](const JobQueuedEvent &) {
		++queuedCount;
	});
	auto startedSub = eventBus->Subscribe<JobStartedEvent>([&](const JobStartedEvent &) {
		++startedCount;
	});
	auto progressSub = eventBus->Subscribe<JobProgressEvent>([&](const JobProgressEvent &) {
		++progressCount;
	});
	auto completedSub = eventBus->Subscribe<JobCompletedEvent>([&](const JobCompletedEvent &) {
		++completedCount;
	});

	const auto id = jobSystem.Submit(CreateRef<SleepJob>("events", 5, Time::Milliseconds(2)));
	ASSERT_GT(id, 0u);

	ASSERT_TRUE(WaitUntil([&]() {
		eventBus->ProcessQueue();
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(1500)));

	eventBus->ProcessQueue();

	EXPECT_GE(queuedCount.load(), 1);
	EXPECT_GE(startedCount.load(), 1);
	EXPECT_GE(progressCount.load(), 1);
	EXPECT_GE(completedCount.load(), 1);

	jobSystem.Shutdown();
}
