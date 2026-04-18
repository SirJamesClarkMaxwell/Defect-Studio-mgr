#include <gtest/gtest.h>

#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;
}

TEST(JobSystemControlTests, CancelledJobIsMarkedCancelled)
{
	JobSystem jobSystem;
	auto gate = CreateRef<Gate>();

	const auto blockerId = jobSystem.Submit(CreateRef<BlockingJob>(gate), JobPriority::Highest);
	const auto cancellableId = jobSystem.Submit(CreateRef<SleepJob>("cancel-me", 20, Time::Milliseconds(5)));
	EXPECT_TRUE(jobSystem.RequestCancel(cancellableId));
	gate->Open();

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(cancellableId);
		return snapshot.has_value() && snapshot->status == JobStatus::Cancelled;
	}, Time::Milliseconds(1500)));

	auto blocker = jobSystem.GetJob(blockerId);
	ASSERT_TRUE(blocker.has_value());
	EXPECT_EQ(blocker->status, JobStatus::Completed);
	jobSystem.Shutdown();
}

TEST(JobSystemControlTests, ResetMovesFinishedJobToQueued)
{
	JobSystem jobSystem;
	const auto id = jobSystem.Submit(CreateRef<SleepJob>("reset-me", 1, Time::Milliseconds(5)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(500)));

	EXPECT_TRUE(jobSystem.Reset(id));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Queued);
	EXPECT_TRUE(snapshot->errorMessage.empty());
	EXPECT_EQ(snapshot->completedWork, 0.0f);
	jobSystem.Shutdown();
}

TEST(JobSystemControlTests, ResetFailsOnRunningJob)
{
	JobSystem jobSystem;
	auto gate = CreateRef<Gate>();
	const auto id = jobSystem.Submit(CreateRef<BlockingJob>(gate));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Running;
	}, Time::Milliseconds(500)));

	EXPECT_FALSE(jobSystem.Reset(id));

	gate->Open();
	jobSystem.Shutdown();
}

TEST(JobSystemControlTests, RetryResubmitsFinishedJob)
{
	JobSystem jobSystem;
	const auto id = jobSystem.Submit(CreateRef<SleepJob>("retry-me", 1, Time::Milliseconds(5)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(500)));

	EXPECT_TRUE(jobSystem.Retry(id));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Queued);

	ASSERT_TRUE(WaitUntil([&]() {
		auto snap = jobSystem.GetJob(id);
		return snap.has_value() && snap->status == JobStatus::Completed;
	}, Time::Milliseconds(500)));

	jobSystem.Shutdown();
}

TEST(JobSystemControlTests, RetryFailsOnRunningJob)
{
	JobSystem jobSystem;
	auto gate = CreateRef<Gate>();
	const auto id = jobSystem.Submit(CreateRef<BlockingJob>(gate));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Running;
	}, Time::Milliseconds(500)));

	EXPECT_FALSE(jobSystem.Retry(id));

	gate->Open();
	jobSystem.Shutdown();
}

TEST(JobSystemControlTests, RemoveFromHistoryDeletesOnlyFinishedJobs)
{
	JobSystem jobSystem;
	auto gate = CreateRef<Gate>();

	const auto runningId = jobSystem.Submit(CreateRef<BlockingJob>(gate));
	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(runningId);
		return snapshot.has_value() && snapshot->status == JobStatus::Running;
	}, Time::Milliseconds(500)));

	EXPECT_FALSE(jobSystem.RemoveFromHistory(runningId));

	gate->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(runningId);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(1000)));

	EXPECT_TRUE(jobSystem.RemoveFromHistory(runningId));
	EXPECT_FALSE(jobSystem.GetJob(runningId).has_value());

	jobSystem.Shutdown();
}
