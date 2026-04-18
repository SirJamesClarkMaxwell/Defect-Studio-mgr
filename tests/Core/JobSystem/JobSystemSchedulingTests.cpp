#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;
}

TEST(JobSystemSchedulingTests, SubmitAfterDelaysExecutionUntilDueTime)
{
	JobSystem jobSystem;
	const auto submitTime = Time::Now();
	const auto delayMs = Time::Milliseconds(100);
	const auto id = jobSystem.SubmitAfter(CreateRef<SleepJob>("delayed", 1, Time::Milliseconds(5)), delayMs);

	EXPECT_GT(id, 0u);
	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Queued);

	std::this_thread::sleep_for(Time::Milliseconds(10));
	snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Queued);

	ASSERT_TRUE(WaitUntil([&]() {
		auto snap = jobSystem.GetJob(id);
		return snap.has_value() && snap->status == JobStatus::Completed;
	}, Time::Milliseconds(500)));

	snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(snapshot->startedAt - submitTime);
	EXPECT_GE(elapsedMs.count(), delayMs.count() - 20);
	jobSystem.Shutdown();
}

TEST(JobSystemSchedulingTests, SubmitAfterZeroDelayExecutesImmediately)
{
	JobSystem jobSystem;
	const auto id = jobSystem.SubmitAfter(CreateRef<SleepJob>("zero-delay", 1, Time::Milliseconds(1)), Time::Milliseconds(0));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(500)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Completed);
	jobSystem.Shutdown();
}

TEST(JobSystemSchedulingTests, CancelledDelayedJobDoesNotExecute)
{
	JobSystem jobSystem;
	const auto id = jobSystem.SubmitAfter(CreateRef<SleepJob>("delayed-cancel", 5, Time::Milliseconds(5)), Time::Milliseconds(200));

	std::this_thread::sleep_for(Time::Milliseconds(50));
	EXPECT_TRUE(jobSystem.RequestCancel(id));

	std::this_thread::sleep_for(Time::Milliseconds(300));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, JobStatus::Cancelled);
	jobSystem.Shutdown();
}

TEST(JobSystemSchedulingTests, MultipleDelayedJobsExecuteInOrder)
{
	JobSystem jobSystem;
	const auto id1 = jobSystem.SubmitAfter(CreateRef<SleepJob>("delayed1", 1, Time::Milliseconds(2)), Time::Milliseconds(100));
	const auto id2 = jobSystem.SubmitAfter(CreateRef<SleepJob>("delayed2", 1, Time::Milliseconds(2)), Time::Milliseconds(50));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snap1 = jobSystem.GetJob(id1);
		auto snap2 = jobSystem.GetJob(id2);
		return snap1.has_value() && snap1->status == JobStatus::Completed &&
		       snap2.has_value() && snap2->status == JobStatus::Completed;
	}, Time::Milliseconds(1000)));

	auto snap1 = jobSystem.GetJob(id1);
	auto snap2 = jobSystem.GetJob(id2);
	ASSERT_TRUE(snap1.has_value() && snap2.has_value());
	EXPECT_LT(snap2->startedAt, snap1->startedAt);
	jobSystem.Shutdown();
}
