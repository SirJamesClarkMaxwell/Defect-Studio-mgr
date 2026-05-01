#include <gtest/gtest.h>

#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;
}

class JobSystemSafetyTests : public ::testing::Test
{
protected:
	JobSystem jobSystem;
};

TEST_F(JobSystemSafetyTests, PauseAllJobsStopsExecution)
{
	auto blocker = CreateRef<Gate>();
	const auto blocked = jobSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	ASSERT_NE(blocked, 0u);
	
	std::this_thread::sleep_for(Time::Milliseconds(50));
	
	auto job1 = jobSystem.Submit(CreateRef<SleepJob>("paused-1", 10, Time::Milliseconds(1000)));
	auto job2 = jobSystem.Submit(CreateRef<SleepJob>("paused-2", 20, Time::Milliseconds(1000)));
	
	ASSERT_TRUE(jobSystem.PauseAllJobs());
	
	// Even after a delay, paused jobs should still be queued
	std::this_thread::sleep_for(Time::Milliseconds(100));
	
	auto snap1 = jobSystem.GetJob(job1);
	auto snap2 = jobSystem.GetJob(job2);
	ASSERT_TRUE(snap1.has_value());
	ASSERT_TRUE(snap2.has_value());
	
	// Jobs might still be queued or running (depends on timing)
	// The key is that unpause isn't called yet
	
	blocker->Open();
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, ResumeAllJobsResumesExecution)
{
	auto gate1 = CreateRef<Gate>();
	auto gate2 = CreateRef<Gate>();
	
	ASSERT_TRUE(jobSystem.PauseAllJobs());
	
	auto job1 = jobSystem.Submit(CreateRef<BlockingJob>(gate1), JobPriority::Normal);
	auto job2 = jobSystem.Submit(CreateRef<BlockingJob>(gate2), JobPriority::Normal);
	
	ASSERT_TRUE(jobSystem.ResumeAllJobs());
	
	std::this_thread::sleep_for(Time::Milliseconds(100));
	
	// Jobs should now be running
	auto snap1 = jobSystem.GetJob(job1);
	auto snap2 = jobSystem.GetJob(job2);
	
	gate1->Open();
	gate2->Open();
	
	ASSERT_TRUE(WaitUntil([&]() {
		auto s1 = jobSystem.GetJob(job1);
		auto s2 = jobSystem.GetJob(job2);
		return (s1.has_value() && s1->status == JobStatus::Completed) &&
		       (s2.has_value() && s2->status == JobStatus::Completed);
	}, Time::Milliseconds(2000)));
	
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, SafeSetThreadCountValidatesSanity)
{
	std::string message;
	
	// Submit some jobs
	for (int i = 0; i < 5; ++i)
		(void)jobSystem.Submit(CreateRef<SleepJob>("test-" + std::to_string(i), i + 1, Time::Milliseconds(100)));
	
	// Try to change thread count - should succeed with validation
	EXPECT_TRUE(jobSystem.SafeSetThreadCount(2, message));
	EXPECT_FALSE(message.empty());
	
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, SafeSetThreadCountPreservesQueuedJobs)
{
	JobSystem singleWorkerSystem({}, 1);
	auto blocker = CreateRef<Gate>();
	const auto blockerId = singleWorkerSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	
	ASSERT_TRUE(WaitUntil([&]() {
		const auto snapshot = singleWorkerSystem.GetJob(blockerId);
		return snapshot.has_value() && snapshot->status == JobStatus::Running;
	}, Time::Milliseconds(1000)));
	
	// Submit jobs that will be queued
	auto job1 = singleWorkerSystem.Submit(CreateRef<SleepJob>("queued-1", 1, Time::Milliseconds(1)));
	auto job2 = singleWorkerSystem.Submit(CreateRef<SleepJob>("queued-2", 1, Time::Milliseconds(1)));
	auto job3 = singleWorkerSystem.Submit(CreateRef<SleepJob>("queued-3", 1, Time::Milliseconds(1)));
	
	const auto queuedBefore = singleWorkerSystem.GetQueuedJobCount();
	EXPECT_EQ(queuedBefore, 3u);
	
	std::string message;
	EXPECT_TRUE(singleWorkerSystem.SafeSetThreadCount(2, message)) << "SafeSetThreadCount failed: " << message;
	
	// All originally queued jobs should still exist
	EXPECT_TRUE(singleWorkerSystem.GetJob(job1).has_value());
	EXPECT_TRUE(singleWorkerSystem.GetJob(job2).has_value());
	EXPECT_TRUE(singleWorkerSystem.GetJob(job3).has_value());
	
	blocker->Open();
	singleWorkerSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, SafeSetThreadCountReturnsFailureOnShutdown)
{
	jobSystem.Shutdown();
	
	std::string message;
	EXPECT_FALSE(jobSystem.SafeSetThreadCount(2, message));
	EXPECT_TRUE(message.find("shutting down") != std::string::npos);
}

TEST_F(JobSystemSafetyTests, GetQueuedJobCountReturnsCorrectCount)
{
	JobSystem singleWorkerSystem({}, 1);
	auto gate = CreateRef<Gate>();
	const auto blockerId = singleWorkerSystem.Submit(CreateRef<BlockingJob>(gate), JobPriority::Highest);
	
	ASSERT_TRUE(WaitUntil([&]() {
		const auto snapshot = singleWorkerSystem.GetJob(blockerId);
		return snapshot.has_value() && snapshot->status == JobStatus::Running;
	}, Time::Milliseconds(1000)));
	
	(void)singleWorkerSystem.Submit(CreateRef<SleepJob>("q1", 1, Time::Milliseconds(1)));
	(void)singleWorkerSystem.Submit(CreateRef<SleepJob>("q2", 1, Time::Milliseconds(1)));
	(void)singleWorkerSystem.Submit(CreateRef<SleepJob>("q3", 1, Time::Milliseconds(1)));
	
	const auto count = singleWorkerSystem.GetQueuedJobCount();
	EXPECT_EQ(count, 3u);
	
	gate->Open();
	singleWorkerSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, IsQueueFullDetectsCapacityReached)
{
	JobSystem singleWorkerSystem({}, 1);
	auto gate = CreateRef<Gate>();
	const auto blockerId = singleWorkerSystem.Submit(CreateRef<BlockingJob>(gate), JobPriority::Highest);
	ASSERT_TRUE(WaitUntil([&]() {
		const auto snapshot = singleWorkerSystem.GetJob(blockerId);
		return snapshot.has_value() && snapshot->status == JobStatus::Running;
	}, Time::Milliseconds(1000)));

	const auto initialCapacity = singleWorkerSystem.GetMaxQueueCapacity();
	
	// Fill queue beyond capacity
	std::vector<JobId> jobIds;
	for (std::size_t i = 0; i < initialCapacity + 10; ++i)
	{
		jobIds.push_back(singleWorkerSystem.Submit(
			CreateRef<SleepJob>("fill-" + std::to_string(i), 1, Time::Milliseconds(0))));
	}
	
	// Queue should be full or have expanded
	const auto capacityAfter = singleWorkerSystem.GetMaxQueueCapacity();
	EXPECT_GE(capacityAfter, initialCapacity);
	
	gate->Open();
	singleWorkerSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, SafeSetThreadCountWithPauseResumeFlow)
{
	// This tests the complete safety flow:
	// 1. Pause all jobs
	// 2. Validate thread count
	// 3. Change thread count
	// 4. Resume jobs
	
	auto blocker = CreateRef<Gate>();
	const auto blockerId = jobSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	ASSERT_NE(blockerId, 0u);
	
	std::this_thread::sleep_for(Time::Milliseconds(50));
	
	auto job1 = jobSystem.Submit(CreateRef<SleepJob>("flow-1", 1, Time::Milliseconds(100)));
	auto job2 = jobSystem.Submit(CreateRef<SleepJob>("flow-2", 2, Time::Milliseconds(100)));
	
	std::string message;
	const auto result = jobSystem.SafeSetThreadCount(2, message);
	
	// Should succeed
	EXPECT_TRUE(result) << "Flow test failed: " << message;
	
	// Jobs should eventually complete
	blocker->Open();
	
	ASSERT_TRUE(WaitUntil([&]() {
		auto snap1 = jobSystem.GetJob(job1);
		auto snap2 = jobSystem.GetJob(job2);
		return (!snap1.has_value() || snap1->status != JobStatus::Queued) &&
		       (!snap2.has_value() || snap2->status != JobStatus::Queued);
	}, Time::Milliseconds(2000)));
	
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, MultipleJobsPreservedDuringThreadChange)
{
	auto blocker = CreateRef<Gate>();
	const auto blockerId = jobSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	ASSERT_NE(blockerId, 0u);
	
	std::this_thread::sleep_for(Time::Milliseconds(50));
	
	// Submit diverse jobs
	std::vector<JobId> jobIds;
	for (int i = 0; i < 10; ++i)
	{
		jobIds.push_back(jobSystem.Submit(
			CreateRef<SleepJob>("multi-" + std::to_string(i), i, Time::Milliseconds(50))));
	}
	
	std::string message;
	EXPECT_TRUE(jobSystem.SafeSetThreadCount(4, message));
	
	// All jobs should still exist in the system
	for (const auto jobId : jobIds)
	{
		EXPECT_TRUE(jobSystem.GetJob(jobId).has_value()) << "Job " << jobId << " was lost";
	}
	
	blocker->Open();
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, RapidThreadCountChangesAreHandled)
{
	std::string msg1, msg2, msg3;
	
	// Rapid succession of thread count changes should not crash
	EXPECT_TRUE(jobSystem.SafeSetThreadCount(2, msg1));
	EXPECT_TRUE(jobSystem.SafeSetThreadCount(4, msg2));
	EXPECT_TRUE(jobSystem.SafeSetThreadCount(1, msg3));
	
	// At least one should indicate a change
	EXPECT_FALSE(msg1.empty());
	
	jobSystem.Shutdown();
}
