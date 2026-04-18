#include <gtest/gtest.h>

#include <algorithm>
#include <thread>

#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;
}

TEST(JobSystemLifecycleTests, SubmitReturnsValidJobId)
{
	JobSystem jobSystem;
	const auto id = jobSystem.Submit(CreateRef<SleepJob>("sleep", 1, Time::Milliseconds(1)));
	EXPECT_GT(id, 0u);
	jobSystem.Shutdown();
}

TEST(JobSystemLifecycleTests, SubmittedJobTransitionsQueuedRunningCompleted)
{
	JobSystem jobSystem;
	auto gate = CreateRef<Gate>();
	const auto id = jobSystem.Submit(CreateRef<BlockingJob>(gate));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		if (!snapshot.has_value())
			return false;
		return snapshot->status == JobStatus::Running || snapshot->status == JobStatus::Queued;
	}, Time::Milliseconds(500)));

	gate->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(1000)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Completed);
	EXPECT_GE(snapshot->finishedAt, snapshot->createdAt);
	jobSystem.Shutdown();
}

TEST(JobSystemLifecycleTests, FailedJobStoresExceptionMessage)
{
	JobSystem jobSystem;
	const auto id = jobSystem.Submit(CreateRef<FailingJob>("expected failure"));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Failed;
	}, Time::Milliseconds(1000)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->errorMessage, "expected failure");
	jobSystem.Shutdown();
}

TEST(JobSystemLifecycleTests, ActiveAndFinishedViewsAreSeparated)
{
	JobSystem jobSystem;
	auto gate = CreateRef<Gate>();
	const auto runningId = jobSystem.Submit(CreateRef<BlockingJob>(gate));

	const auto activeJobs = jobSystem.GetActiveJobs();
	EXPECT_TRUE(std::any_of(activeJobs.begin(), activeJobs.end(), [runningId](const JobSnapshot &job) {
		return job.id == runningId;
	}));

	gate->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		auto running = jobSystem.GetJob(runningId);
		return running.has_value() && running->status == JobStatus::Completed;
	}, Time::Milliseconds(1000)));

	const auto failedId = jobSystem.Submit(CreateRef<FailingJob>("boom"));
	ASSERT_TRUE(WaitUntil([&]() {
		auto failed = jobSystem.GetJob(failedId);
		return failed.has_value() && failed->status == JobStatus::Failed;
	}, Time::Milliseconds(1000)));

	const auto finishedJobs = jobSystem.GetFinishedJobs();
	EXPECT_TRUE(std::any_of(finishedJobs.begin(), finishedJobs.end(), [failedId](const JobSnapshot &job) {
		return job.id == failedId;
	}));
	jobSystem.Shutdown();
}

TEST(JobSystemLifecycleTests, GetLogsReturnsEntriesInAppendOrder)
{
	JobSystem jobSystem;
	const auto id = jobSystem.Submit(CreateRef<LoggingJob>());

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(1000)));

	const auto logs = jobSystem.GetLogs(id);
	ASSERT_GE(logs.size(), 5u);
	EXPECT_EQ(logs[2].message, "first");
	EXPECT_EQ(logs[3].message, "second");
	EXPECT_EQ(logs[4].message, "third");
	jobSystem.Shutdown();
}

TEST(JobSystemLifecycleTests, SleepJobReportsProgress)
{
	JobSystem jobSystem;
	const auto id = jobSystem.Submit(CreateRef<SleepJob>("progress", 10, Time::Milliseconds(5)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		if (!snapshot.has_value())
			return false;
		if (snapshot->status == JobStatus::Completed)
			return true;
		return snapshot->completedWork > 0.0f;
	}, Time::Milliseconds(1000)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_GE(snapshot->completedWork, 0.0f);
	jobSystem.Shutdown();
}

TEST(JobSystemLifecycleTests, ShutdownWaitsAndIsIdempotent)
{
	JobSystem jobSystem;
	auto gate = CreateRef<Gate>();
	const auto id = jobSystem.Submit(CreateRef<BlockingJob>(gate));

	std::thread shutdownThread([&]() {
		jobSystem.Shutdown();
	});

	std::this_thread::sleep_for(Time::Milliseconds(20));
	auto snapshotBeforeOpen = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshotBeforeOpen.has_value());
	EXPECT_NE(snapshotBeforeOpen->status, JobStatus::Completed);

	gate->Open();
	shutdownThread.join();

	jobSystem.Shutdown();
	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Completed);
}
