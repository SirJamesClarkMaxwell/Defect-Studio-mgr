#include <gtest/gtest.h>

#include <algorithm>
#include <thread>
#include <vector>

#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;
}

TEST(JobSystemThreadingTests, ConcurrentSubmitDoesNotCorruptRecords)
{
	JobSystem jobSystem;
	constexpr int threadCount = 8;
	constexpr int jobsPerThread = 15;

	std::vector<std::thread> submitters;
	submitters.reserve(threadCount);

	for (int thread = 0; thread < threadCount; ++thread)
	{
		submitters.emplace_back([&jobSystem]() {
			for (int i = 0; i < jobsPerThread; ++i)
				(void)jobSystem.Submit(CreateRef<SleepJob>("batch", 1, Time::Milliseconds(1)));
		});
	}

	for (auto &thread : submitters)
		thread.join();

	jobSystem.Shutdown();
	const auto all = jobSystem.GetAllJobs();
	EXPECT_EQ(all.size(), static_cast<std::size_t>(threadCount * jobsPerThread));
}

TEST(JobSystemThreadingTests, SetThreadCountScalesWorkerPool)
{
	JobSystem jobSystem;
	const auto initialCount = jobSystem.GetThreadCount();
	EXPECT_GT(initialCount, 0u);

	const auto newCount = initialCount > 1 ? (initialCount - 1) : (initialCount + 1);
	EXPECT_TRUE(jobSystem.SetThreadCount(newCount));
	ASSERT_TRUE(WaitUntil([&]() {
		return jobSystem.GetThreadCount() == newCount;
	}, Time::Milliseconds(2000)));
	EXPECT_EQ(jobSystem.GetThreadCount(), newCount);

	const auto id = jobSystem.Submit(CreateRef<SleepJob>("after-scale", 1, Time::Milliseconds(1)));
	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(500)));

	jobSystem.Shutdown();
}

TEST(JobSystemThreadingTests, SetThreadCountPreservesJobRecords)
{
	JobSystem jobSystem;
	const auto id1 = jobSystem.Submit(CreateRef<SleepJob>("before-scale", 1, Time::Milliseconds(1)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id1);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(500)));

	const auto beforeScale = jobSystem.GetAllJobs();
	const auto beforeCount = beforeScale.size();

	EXPECT_TRUE(jobSystem.SetThreadCount(4));

	const auto afterScale = jobSystem.GetAllJobs();
	EXPECT_EQ(afterScale.size(), beforeCount);
	EXPECT_TRUE(std::any_of(afterScale.begin(), afterScale.end(), [id1](const JobSnapshot &job) {
		return job.id == id1 && job.status == JobStatus::Completed;
	}));

	jobSystem.Shutdown();
}

TEST(JobSystemThreadingTests, SetThreadCountWhileWorkIsQueuedPreservesAndCompletesJobs)
{
	JobSystem jobSystem({}, 1);
	auto release = CreateRef<Gate>();
	const JobId blockerId = jobSystem.Submit(CreateRef<BlockingJob>(release), JobPriority::Highest);
	std::vector<JobId> queuedIds;
	for (int i = 0; i < 8; ++i)
		queuedIds.push_back(jobSystem.Submit(CreateRef<SleepJob>("queued-scale", 1, Time::Milliseconds(1))));

	ASSERT_TRUE(WaitUntil([&]() {
		auto blocker = jobSystem.GetJob(blockerId);
		return blocker.has_value() && blocker->status == JobStatus::Running;
	}, Time::Milliseconds(800)));

	EXPECT_TRUE(jobSystem.SetThreadCount(4));
	release->Open();

	ASSERT_TRUE(WaitUntil([&]() {
		if (jobSystem.GetThreadCount() != 4)
			return false;
		const auto jobs = jobSystem.GetAllJobs();
		return jobs.size() == queuedIds.size() + 1
			&& std::all_of(jobs.begin(), jobs.end(), [](const JobSnapshot &job) {
				return job.status == JobStatus::Completed;
			});
	}, Time::Milliseconds(3000)));

	jobSystem.Shutdown();
}

TEST(JobSystemThreadingTests, ReducingThreadCountKeepsRunningJobsAndLimitsNewParallelism) // this test case is taking too long
{
	JobSystem jobSystem({}, 3);

	auto firstStart = CreateRef<Gate>();
	auto firstRelease = CreateRef<Gate>();
	auto firstRunning = CreateRef<std::atomic<int>>(0);
	auto firstMax = CreateRef<std::atomic<int>>(0);

	for (int i = 0; i < 6; ++i)
		(void)jobSystem.Submit(CreateRef<ConcurrencyProbeJob>(firstStart, firstRelease, firstRunning, firstMax));

	firstStart->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		return firstRunning->load(std::memory_order_relaxed) >= 3;
	}, Time::Milliseconds(800)));

	EXPECT_TRUE(jobSystem.SetThreadCount(2));

	EXPECT_GE(firstMax->load(std::memory_order_relaxed), 3);

	firstRelease->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		if (jobSystem.GetThreadCount() != 2)
			return false;

		const auto all = jobSystem.GetAllJobs();
		return std::all_of(all.begin(), all.end(), [](const JobSnapshot &job) {
			return job.status == JobStatus::Completed;
		});
	}, Time::Milliseconds(4000)));

	auto secondStart = CreateRef<Gate>();
	auto secondRelease = CreateRef<Gate>();
	auto secondRunning = CreateRef<std::atomic<int>>(0);
	auto secondMax = CreateRef<std::atomic<int>>(0);

	for (int i = 0; i < 4; ++i)
		(void)jobSystem.Submit(CreateRef<ConcurrencyProbeJob>(secondStart, secondRelease, secondRunning, secondMax));

	secondStart->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		return secondRunning->load(std::memory_order_relaxed) >= 2;
	}, Time::Milliseconds(800)));

	std::this_thread::sleep_for(Time::Milliseconds(80));
	EXPECT_LE(secondMax->load(std::memory_order_relaxed), 2);

	secondRelease->Open();
	jobSystem.Shutdown();
}

TEST(JobSystemThreadingTests, SynchronizationBetweenConcurrentOperations)
{
	JobSystem jobSystem;
	std::vector<JobId> ids;
	std::mutex idsMutex;
	std::vector<std::thread> workers;

	for (int i = 0; i < 5; ++i)
	{
		workers.emplace_back([&jobSystem, &ids, &idsMutex, i]() {
			for (int j = 0; j < 10; ++j)
			{
				const auto id = jobSystem.Submit(CreateRef<SleepJob>(
					"sync-job-" + std::to_string(i * 10 + j), 1, Time::Milliseconds(1)));
				{
					std::lock_guard<std::mutex> lock(idsMutex);
				ids.push_back(id);
				}
			}
		});
	}

	for (auto &w : workers)
		w.join();

	ASSERT_TRUE(WaitUntil([&]() {
		auto finished = jobSystem.GetFinishedJobs();
		return finished.size() == ids.size();
	}, Time::Milliseconds(2000)));

	jobSystem.Shutdown();
	const auto allJobs = jobSystem.GetAllJobs();
	EXPECT_EQ(allJobs.size(), ids.size());
	for (const auto &job : allJobs)
		EXPECT_EQ(job.status, JobStatus::Completed);
}
