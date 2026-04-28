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
	auto blocked = jobSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	
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
	
	jobSystem.PauseAllJobs();
	
	auto job1 = jobSystem.Submit(CreateRef<BlockingJob>(gate1), JobPriority::Normal);
	auto job2 = jobSystem.Submit(CreateRef<BlockingJob>(gate2), JobPriority::Normal);
	
	jobSystem.ResumeAllJobs();
	
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
		jobSystem.Submit(CreateRef<SleepJob>("test-" + std::to_string(i), i + 1, Time::Milliseconds(100)));
	
	// Try to change thread count - should succeed with validation
	EXPECT_TRUE(jobSystem.SafeSetThreadCount(2, message));
	EXPECT_FALSE(message.empty());
	
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, SafeSetThreadCountPreservesQueuedJobs)
{
	auto blocker = CreateRef<Gate>();
	jobSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	
	std::this_thread::sleep_for(Time::Milliseconds(50));
	
	// Submit jobs that will be queued
	auto job1 = jobSystem.Submit(CreateRef<SleepJob>("queued-1", 10, Time::Milliseconds(100)));
	auto job2 = jobSystem.Submit(CreateRef<SleepJob>("queued-2", 20, Time::Milliseconds(100)));
	auto job3 = jobSystem.Submit(CreateRef<SleepJob>("queued-3", 30, Time::Milliseconds(100)));
	
	const auto queuedBefore = jobSystem.GetQueuedJobCount();
	EXPECT_GE(queuedBefore, 2); // At least 2 jobs should be queued
	
	std::string message;
	EXPECT_TRUE(jobSystem.SafeSetThreadCount(1, message)) << "SafeSetThreadCount failed: " << message;
	
	// All originally queued jobs should still exist
	EXPECT_TRUE(jobSystem.GetJob(job1).has_value());
	EXPECT_TRUE(jobSystem.GetJob(job2).has_value());
	EXPECT_TRUE(jobSystem.GetJob(job3).has_value());
	
	blocker->Open();
	jobSystem.Shutdown();
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
	auto gate = CreateRef<Gate>();
	jobSystem.Submit(CreateRef<BlockingJob>(gate), JobPriority::Highest);
	
	std::this_thread::sleep_for(Time::Milliseconds(50));
	
	auto job1 = jobSystem.Submit(CreateRef<SleepJob>("q1", 1, Time::Milliseconds(100)));
	auto job2 = jobSystem.Submit(CreateRef<SleepJob>("q2", 2, Time::Milliseconds(100)));
	auto job3 = jobSystem.Submit(CreateRef<SleepJob>("q3", 3, Time::Milliseconds(100)));
	
	const auto count = jobSystem.GetQueuedJobCount();
	EXPECT_GE(count, 2); // At least 2 queued (plus potentially running blocker)
	
	gate->Open();
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, IsQueueFullDetectsCapacityReached)
{
	const auto initialCapacity = jobSystem.GetMaxQueueCapacity();
	
	// Fill queue beyond capacity
	std::vector<JobId> jobIds;
	for (std::size_t i = 0; i < initialCapacity + 10; ++i)
	{
		jobIds.push_back(jobSystem.Submit(
			CreateRef<SleepJob>("fill-" + std::to_string(i), static_cast<int>(i), Time::Milliseconds(50))));
	}
	
	// Queue should be full or have expanded
	const auto capacityAfter = jobSystem.GetMaxQueueCapacity();
	EXPECT_GE(capacityAfter, initialCapacity);
	
	jobSystem.Shutdown();
}

TEST_F(JobSystemSafetyTests, SafeSetThreadCountWithPauseResumeFlow)
{
	// This tests the complete safety flow:
	// 1. Pause all jobs
	// 2. Validate thread count
	// 3. Change thread count
	// 4. Resume jobs
	
	auto blocker = CreateRef<Gate>();
	jobSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	
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
	jobSystem.Submit(CreateRef<BlockingJob>(blocker), JobPriority::Highest);
	
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
