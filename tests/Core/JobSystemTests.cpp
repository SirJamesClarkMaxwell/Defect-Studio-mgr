#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/JobSystem/TestJobs/TestJobs.hpp"
#include "Core/Utils/Time.hpp"

namespace
{
	class Gate
	{
	public:
		void Open()
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Open = true;
			}
			m_Condition.notify_all();
		}

		void Wait()
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Condition.wait(lock, [this] { return m_Open; });
		}

	private:
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Open = false;
	};

	class BlockingJob final : public DefectStudio::IJob
	{
	public:
		explicit BlockingJob(DefectStudio::Ref<Gate> gate) : m_Gate(std::move(gate))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "BlockingJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "BlockingJob";
		}

		void Execute(DefectStudio::JobContext &context) override
		{
			context.SetStage("waiting");
			m_Gate->Wait();
			context.SetStage("done");
			context.SetProgress(1.0f, 1.0f);
		}

	private:
		DefectStudio::Ref<Gate> m_Gate;
	};

	class LoggingJob final : public DefectStudio::IJob
	{
	public:
		[[nodiscard]] std::string GetName() const override
		{
			return "LoggingJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "LoggingJob";
		}

		void Execute(DefectStudio::JobContext &context) override
		{
			context.LogInfo("first");
			context.LogWarning("second");
			context.LogError("third");
		}
	};

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
} // namespace

TEST(JobSystemTests, SubmitReturnsValidJobId)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("sleep", 1, DefectStudio::Time::Milliseconds(1)));
	EXPECT_GT(id, 0u);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, SubmittedJobTransitionsQueuedRunningCompleted)
{
	DefectStudio::JobSystem jobSystem;
	auto gate = DefectStudio::CreateRef<Gate>();
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<BlockingJob>(gate));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		if (!snapshot.has_value())
			return false;
		return snapshot->status == DefectStudio::JobStatus::Running || snapshot->status == DefectStudio::JobStatus::Queued;
	}, DefectStudio::Time::Milliseconds(500)));

	gate->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(1000)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Completed);
	EXPECT_GE(snapshot->finishedAt, snapshot->createdAt);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, FailedJobStoresExceptionMessage)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::FailingJob>("expected failure"));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Failed;
	}, DefectStudio::Time::Milliseconds(1000)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->errorMessage, "expected failure");
	jobSystem.Shutdown();
}

TEST(JobSystemTests, CancelledJobIsMarkedCancelled)
{
	DefectStudio::JobSystem jobSystem;
	auto gate = DefectStudio::CreateRef<Gate>();

	const auto blockerId = jobSystem.Submit(DefectStudio::CreateRef<BlockingJob>(gate), DefectStudio::JobPriority::Highest);
	const auto cancellableId = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("cancel-me", 20, DefectStudio::Time::Milliseconds(5)));
	EXPECT_TRUE(jobSystem.RequestCancel(cancellableId));
	gate->Open();

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(cancellableId);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Cancelled;
	}, DefectStudio::Time::Milliseconds(1500)));

	auto blocker = jobSystem.GetJob(blockerId);
	ASSERT_TRUE(blocker.has_value());
	EXPECT_EQ(blocker->status, DefectStudio::JobStatus::Completed);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, ActiveAndFinishedViewsAreSeparated)
{
	DefectStudio::JobSystem jobSystem;
	auto gate = DefectStudio::CreateRef<Gate>();
	const auto runningId = jobSystem.Submit(DefectStudio::CreateRef<BlockingJob>(gate));

	const auto activeJobs = jobSystem.GetActiveJobs();
	EXPECT_TRUE(std::any_of(activeJobs.begin(), activeJobs.end(), [runningId](const DefectStudio::JobSnapshot &job) {
		return job.id == runningId;
	}));

	gate->Open();
	ASSERT_TRUE(WaitUntil([&]() {
		auto running = jobSystem.GetJob(runningId);
		return running.has_value() && running->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(1000)));

	const auto failedId = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::FailingJob>("boom"));
	ASSERT_TRUE(WaitUntil([&]() {
		auto failed = jobSystem.GetJob(failedId);
		return failed.has_value() && failed->status == DefectStudio::JobStatus::Failed;
	}, DefectStudio::Time::Milliseconds(1000)));

	const auto finishedJobs = jobSystem.GetFinishedJobs();
	EXPECT_TRUE(std::any_of(finishedJobs.begin(), finishedJobs.end(), [failedId](const DefectStudio::JobSnapshot &job) {
		return job.id == failedId;
	}));
	jobSystem.Shutdown();
}

TEST(JobSystemTests, GetLogsReturnsEntriesInAppendOrder)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<LoggingJob>());

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(1000)));

	const auto logs = jobSystem.GetLogs(id);
	ASSERT_GE(logs.size(), 5u);
	EXPECT_EQ(logs[2].message, "first");
	EXPECT_EQ(logs[3].message, "second");
	EXPECT_EQ(logs[4].message, "third");
	jobSystem.Shutdown();
}

TEST(JobSystemTests, SleepJobReportsProgress)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("progress", 10, DefectStudio::Time::Milliseconds(5)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		if (!snapshot.has_value())
			return false;
		if (snapshot->status == DefectStudio::JobStatus::Completed)
			return true;
		return snapshot->completedWork > 0.0f;
	}, DefectStudio::Time::Milliseconds(1000)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_GE(snapshot->completedWork, 0.0f);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, ShutdownWaitsAndIsIdempotent)
{
	DefectStudio::JobSystem jobSystem;
	auto gate = DefectStudio::CreateRef<Gate>();
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<BlockingJob>(gate));

	std::thread shutdownThread([&]() {
		jobSystem.Shutdown();
	});

	std::this_thread::sleep_for(DefectStudio::Time::Milliseconds(20));
	auto snapshotBeforeOpen = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshotBeforeOpen.has_value());
	EXPECT_NE(snapshotBeforeOpen->status, DefectStudio::JobStatus::Completed);

	gate->Open();
	shutdownThread.join();

	jobSystem.Shutdown();
	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Completed);
}

TEST(JobSystemTests, ConcurrentSubmitDoesNotCorruptRecords)
{
	DefectStudio::JobSystem jobSystem;
	constexpr int threadCount = 8;
	constexpr int jobsPerThread = 15;

	std::vector<std::thread> submitters;
	submitters.reserve(threadCount);

	for (int thread = 0; thread < threadCount; ++thread)
	{
			submitters.emplace_back([&jobSystem]() {
			for (int i = 0; i < jobsPerThread; ++i)
				(void)jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("batch", 1, DefectStudio::Time::Milliseconds(1)));
		});
	}

	for (auto &thread : submitters)
		thread.join();

	jobSystem.Shutdown();
	const auto all = jobSystem.GetAllJobs();
	EXPECT_EQ(all.size(), static_cast<std::size_t>(threadCount * jobsPerThread));
}
