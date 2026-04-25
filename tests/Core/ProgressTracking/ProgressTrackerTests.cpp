#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <thread>

#include "App/Events/JobEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Core/Utils/Time.hpp"

namespace
{
	bool WaitUntil(const std::function<bool()> &predicate, DefectStudio::Time::Milliseconds timeout)
	{
		const auto start = DefectStudio::Time::NowSteady();
		while (DefectStudio::Time::NowSteady() - start < timeout)
		{
			if (predicate())
				return true;
			std::this_thread::sleep_for(DefectStudio::Time::Milliseconds(2));
		}
		return predicate();
	}

	class ChildTrackerJob final : public DefectStudio::IJob
	{
	public:
		[[nodiscard]] std::string GetName() const override
		{
			return "ChildTrackerJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "ChildTrackerJob";
		}

		void Execute(DefectStudio::JobContext &context) override
		{
			context.SetProgress(1.0f, 1.0f);
			context.SetStage("child-done");
		}
	};

	class ParentTrackerJob final : public DefectStudio::IJob
	{
	public:
		[[nodiscard]] std::string GetName() const override
		{
			return "ParentTrackerJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "ParentTrackerJob";
		}

		void Execute(DefectStudio::JobContext &context) override
		{
			context.SetStage("submit-child");
			const auto childId = context.SubmitJob(DefectStudio::CreateRef<ChildTrackerJob>(), DefectStudio::JobPriority::Normal);
			if (childId == 0)
				context.LogError("Failed to submit child job");
			context.SetProgress(1.0f, 1.0f);
			context.SetStage("parent-done");
		}
	};
} // namespace

TEST(ProgressTrackerTests, BuildsSnapshotFromLifecycleEvents)
{
	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	DefectStudio::ProgressTracker tracker(eventBus);

	const DefectStudio::JobId id = 77;
	const auto createdAt = DefectStudio::Time::Now();
	const auto startedAt = DefectStudio::Time::Now();
	const auto finishedAt = DefectStudio::Time::Now();

	eventBus->Queue(DefectStudio::JobQueuedEvent{id, "ImportPipeline", "IO", createdAt});
	eventBus->Queue(DefectStudio::JobStartedEvent{id, "ImportPipeline", "IO", startedAt});
	eventBus->Queue(DefectStudio::JobProgressEvent{id, 3.0f, 4.0f, "Validate", "Parsing", DefectStudio::Time::Now()});
	eventBus->Queue(DefectStudio::JobCompletedEvent{id, "ImportPipeline", "IO", finishedAt});
	eventBus->ProcessQueue();

	auto snapshot = tracker.GetSnapshot(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Completed);
	EXPECT_TRUE(snapshot->finished);
	EXPECT_EQ(snapshot->label, "ImportPipeline");
	EXPECT_EQ(snapshot->source, "IO");
	EXPECT_EQ(snapshot->currentStage, "Validate");
	EXPECT_EQ(snapshot->currentMessage, "Parsing");
	EXPECT_FLOAT_EQ(snapshot->completedWork, 4.0f);
	EXPECT_FLOAT_EQ(snapshot->totalWork, 4.0f);
	EXPECT_EQ(snapshot->createdAt, createdAt);
	EXPECT_EQ(snapshot->startedAt, startedAt);
	EXPECT_EQ(snapshot->finishedAt, finishedAt);
}

TEST(ProgressTrackerTests, StoresErrorSummaryAndViews)
{
	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	DefectStudio::ProgressTracker tracker(eventBus);

	eventBus->Queue(DefectStudio::JobQueuedEvent{1, "A", "IO", DefectStudio::Time::Now()});
	eventBus->Queue(DefectStudio::JobStartedEvent{1, "A", "IO", DefectStudio::Time::Now()});
	eventBus->Queue(DefectStudio::JobQueuedEvent{2, "B", "Core", DefectStudio::Time::Now()});
	eventBus->Queue(DefectStudio::JobFailedEvent{2, "B", "Core", "failure", DefectStudio::Time::Now()});
	eventBus->ProcessQueue();

	auto failed = tracker.GetSnapshot(2);
	ASSERT_TRUE(failed.has_value());
	EXPECT_EQ(failed->status, DefectStudio::JobStatus::Failed);
	EXPECT_EQ(failed->errorSummary, "failure");

	const auto active = tracker.GetActiveSnapshots();
	const auto finished = tracker.GetFinishedSnapshots();
	ASSERT_EQ(active.size(), 1u);
	ASSERT_EQ(finished.size(), 1u);
	EXPECT_EQ(active.front().id, 1u);
	EXPECT_EQ(finished.front().id, 2u);
}

TEST(ProgressTrackerTests, ContainsSubtasksWithParentId)
{
	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	DefectStudio::JobSystem jobSystem(eventBus, 2);
	DefectStudio::ProgressTracker tracker(eventBus);

	const auto parentId = jobSystem.Submit(DefectStudio::CreateRef<ParentTrackerJob>());
	ASSERT_GT(parentId, 0u);

	ASSERT_TRUE(WaitUntil([&]() {
		eventBus->ProcessQueue();
		const auto entries = tracker.GetAllSnapshots();
		const auto completedCount = std::count_if(entries.begin(), entries.end(), [](const DefectStudio::ProgressEntrySnapshot &entry) {
			return entry.status == DefectStudio::JobStatus::Completed;
		});
		return completedCount >= 2;
	}, DefectStudio::Time::Milliseconds(2000)));

	const auto allEntries = tracker.GetAllSnapshots();
	auto parentIt = std::find_if(allEntries.begin(), allEntries.end(), [parentId](const DefectStudio::ProgressEntrySnapshot &entry) {
		return entry.id == parentId;
	});
	ASSERT_NE(parentIt, allEntries.end());

	auto childIt = std::find_if(allEntries.begin(), allEntries.end(), [parentId](const DefectStudio::ProgressEntrySnapshot &entry) {
		return entry.parentId == parentId;
	});
	ASSERT_NE(childIt, allEntries.end());
	EXPECT_GT(childIt->id, parentId);

	jobSystem.Shutdown();
}
