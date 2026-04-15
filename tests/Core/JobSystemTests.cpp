#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "App/Events/JobEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
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

	class ChainedJob final : public DefectStudio::IJob
	{
	public:
		ChainedJob(int jobIndex, DefectStudio::Ref<std::vector<int>> executionOrder)
			: m_JobIndex(jobIndex), m_ExecutionOrder(std::move(executionOrder))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "ChainedJob-" + std::to_string(m_JobIndex);
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "ChainedJob";
		}

		void Execute(DefectStudio::JobContext &context) override
		{
			// Record this job's execution
			m_ExecutionOrder->push_back(m_JobIndex);
			context.SetProgress(1.0f, 1.0f);
			context.SetStage("completed");
		}

	private:
		int m_JobIndex;
		DefectStudio::Ref<std::vector<int>> m_ExecutionOrder;
	};

	class SequentialSubmitterJob final : public DefectStudio::IJob
	{
	public:
		SequentialSubmitterJob(int chainDepth, DefectStudio::Ref<std::vector<int>> executionOrder)
			: m_ChainDepth(chainDepth), m_ExecutionOrder(std::move(executionOrder))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "SequentialSubmitterJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "SequentialSubmitterJob";
		}

		void Execute(DefectStudio::JobContext &context) override
		{
			context.SetStage("submitting");
			// This job submits m_ChainDepth new jobs sequentially
			for (int i = 0; i < m_ChainDepth; ++i)
			{
				auto chainedJob = DefectStudio::CreateRef<ChainedJob>(i, m_ExecutionOrder);
				const auto id = context.SubmitJobSequential(chainedJob, DefectStudio::JobPriority::Normal);
				if (id == 0)
				{
					context.LogError("Failed to submit chained job " + std::to_string(i));
					return;
				}
				context.SetMessage("Submitted job " + std::to_string(i));
			}
			context.SetStage("all-submitted");
			context.SetProgress(1.0f, 1.0f);
		}

	private:
		int m_ChainDepth;
		DefectStudio::Ref<std::vector<int>> m_ExecutionOrder;
	};
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

TEST(JobSystemTests, SubmitAfterDelaysExecutionUntilDueTime)
{
	DefectStudio::JobSystem jobSystem;
	const auto submitTime = DefectStudio::Time::Now();
	const auto delayMs = DefectStudio::Time::Milliseconds(100);
	const auto id = jobSystem.SubmitAfter(DefectStudio::CreateRef<DefectStudio::SleepJob>("delayed", 1, DefectStudio::Time::Milliseconds(5)), delayMs);

	EXPECT_GT(id, 0u);
	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Queued);

	// Should still be queued shortly after submit
	std::this_thread::sleep_for(DefectStudio::Time::Milliseconds(10));
	snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Queued);

	// Wait for it to complete
	ASSERT_TRUE(WaitUntil([&]() {
		auto snap = jobSystem.GetJob(id);
		return snap.has_value() && snap->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(500)));

	snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(snapshot->startedAt - submitTime);
	EXPECT_GE(elapsedMs.count(), delayMs.count() - 20); // 20ms tolerance
	jobSystem.Shutdown();
}

TEST(JobSystemTests, SubmitAfterZeroDelayExecutesImmediately)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.SubmitAfter(DefectStudio::CreateRef<DefectStudio::SleepJob>("zero-delay", 1, DefectStudio::Time::Milliseconds(1)), DefectStudio::Time::Milliseconds(0));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(500)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Completed);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, SetThreadCountScalesWorkerPool)
{
	DefectStudio::JobSystem jobSystem;
	const auto initialCount = jobSystem.GetThreadCount();
	EXPECT_GT(initialCount, 0u);

	const auto newCount = initialCount + 2;
	EXPECT_TRUE(jobSystem.SetThreadCount(newCount));
	EXPECT_EQ(jobSystem.GetThreadCount(), newCount);

	// Submit job to verify pool still works
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("after-scale", 1, DefectStudio::Time::Milliseconds(1)));
	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(500)));

	jobSystem.Shutdown();
}

TEST(JobSystemTests, SetThreadCountPreservesJobRecords)
{
	DefectStudio::JobSystem jobSystem;
	const auto id1 = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("before-scale", 1, DefectStudio::Time::Milliseconds(1)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id1);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(500)));

	const auto beforeScale = jobSystem.GetAllJobs();
	const auto beforeCount = beforeScale.size();

	EXPECT_TRUE(jobSystem.SetThreadCount(4));

	const auto afterScale = jobSystem.GetAllJobs();
	EXPECT_EQ(afterScale.size(), beforeCount);
	EXPECT_TRUE(std::any_of(afterScale.begin(), afterScale.end(), [id1](const DefectStudio::JobSnapshot &job) {
		return job.id == id1 && job.status == DefectStudio::JobStatus::Completed;
	}));

	jobSystem.Shutdown();
}

TEST(JobSystemTests, ResetMovesFinishedJobToQueued)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("reset-me", 1, DefectStudio::Time::Milliseconds(5)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(500)));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	const auto errorMsg = snapshot->errorMessage;

	EXPECT_TRUE(jobSystem.Reset(id));

	snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Queued);
	EXPECT_TRUE(snapshot->errorMessage.empty());
	EXPECT_EQ(snapshot->completedWork, 0.0f);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, ResetFailsOnRunningJob)
{
	DefectStudio::JobSystem jobSystem;
	auto gate = DefectStudio::CreateRef<Gate>();
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<BlockingJob>(gate));

	// Let it start running
	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Running;
	}, DefectStudio::Time::Milliseconds(500)));

	EXPECT_FALSE(jobSystem.Reset(id));

	gate->Open();
	jobSystem.Shutdown();
}

TEST(JobSystemTests, RetryResubmitsFinishedJob)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("retry-me", 1, DefectStudio::Time::Milliseconds(5)));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(500)));

	EXPECT_TRUE(jobSystem.Retry(id));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Queued);

	ASSERT_TRUE(WaitUntil([&]() {
		auto snap = jobSystem.GetJob(id);
		return snap.has_value() && snap->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(500)));

	jobSystem.Shutdown();
}

TEST(JobSystemTests, RetryFailsOnRunningJob)
{
	DefectStudio::JobSystem jobSystem;
	auto gate = DefectStudio::CreateRef<Gate>();
	const auto id = jobSystem.Submit(DefectStudio::CreateRef<BlockingJob>(gate));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Running;
	}, DefectStudio::Time::Milliseconds(500)));

	EXPECT_FALSE(jobSystem.Retry(id));

	gate->Open();
	jobSystem.Shutdown();
}

TEST(JobSystemTests, CancelledDelayedJobDoesNotExecute)
{
	DefectStudio::JobSystem jobSystem;
	const auto id = jobSystem.SubmitAfter(DefectStudio::CreateRef<DefectStudio::SleepJob>("delayed-cancel", 5, DefectStudio::Time::Milliseconds(5)), DefectStudio::Time::Milliseconds(200));

	// Cancel before it executes
	std::this_thread::sleep_for(DefectStudio::Time::Milliseconds(50));
	EXPECT_TRUE(jobSystem.RequestCancel(id));

	// Wait longer than the delay
	std::this_thread::sleep_for(DefectStudio::Time::Milliseconds(300));

	auto snapshot = jobSystem.GetJob(id);
	ASSERT_TRUE(snapshot.has_value());
	EXPECT_EQ(snapshot->status, DefectStudio::JobStatus::Cancelled);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, MultipleDelayedJobsExecuteInOrder)
{
	DefectStudio::JobSystem jobSystem;
	const auto id1 = jobSystem.SubmitAfter(DefectStudio::CreateRef<DefectStudio::SleepJob>("delayed1", 1, DefectStudio::Time::Milliseconds(2)), DefectStudio::Time::Milliseconds(100));
	const auto id2 = jobSystem.SubmitAfter(DefectStudio::CreateRef<DefectStudio::SleepJob>("delayed2", 1, DefectStudio::Time::Milliseconds(2)), DefectStudio::Time::Milliseconds(50));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snap1 = jobSystem.GetJob(id1);
		auto snap2 = jobSystem.GetJob(id2);
		return snap1.has_value() && snap1->status == DefectStudio::JobStatus::Completed &&
		       snap2.has_value() && snap2->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(1000)));

	auto snap1 = jobSystem.GetJob(id1);
	auto snap2 = jobSystem.GetJob(id2);
	ASSERT_TRUE(snap1.has_value() && snap2.has_value());
	// id2 should have started roughly 50ms before id1
	EXPECT_LT(snap2->startedAt, snap1->startedAt);
	jobSystem.Shutdown();
}

TEST(JobSystemTests, SynchronizationBetweenConcurrentOperations)
{
	DefectStudio::JobSystem jobSystem;
	std::vector<DefectStudio::JobId> ids;
	std::vector<std::thread> workers;

	// Submit jobs from multiple threads
	for (int i = 0; i < 5; ++i)
	{
		workers.emplace_back([&jobSystem, &ids, i]() {
			for (int j = 0; j < 10; ++j)
			{
				const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>(
					"sync-job-" + std::to_string(i * 10 + j), 1, DefectStudio::Time::Milliseconds(1)));
				ids.push_back(id);
			}
		});
	}

	for (auto &w : workers)
		w.join();

	// Wait for all to complete
	ASSERT_TRUE(WaitUntil([&]() {
		auto finished = jobSystem.GetFinishedJobs();
		return finished.size() == ids.size();
	}, DefectStudio::Time::Milliseconds(2000)));

	jobSystem.Shutdown();
	const auto allJobs = jobSystem.GetAllJobs();
	EXPECT_EQ(allJobs.size(), ids.size());
	for (const auto &job : allJobs)
		EXPECT_EQ(job.status, DefectStudio::JobStatus::Completed);
}

TEST(JobSystemTests, JobCanSubmitOtherJobsSequentially)
{
	DefectStudio::JobSystem jobSystem;
	const int chainDepth = 5;
	auto executionOrder = DefectStudio::CreateRef<std::vector<int>>();

	// Submit a job that will submit N other jobs
	const auto mainId = jobSystem.Submit(
		DefectStudio::CreateRef<SequentialSubmitterJob>(chainDepth, executionOrder));
	(void)mainId; // Used implicitly by WaitUntil count

	// Wait for main job and all chained jobs to complete
	ASSERT_TRUE(WaitUntil([&]() {
		const auto allJobs = jobSystem.GetAllJobs();
		return std::count_if(allJobs.begin(), allJobs.end(), [](const DefectStudio::JobSnapshot &snap) {
			return snap.status == DefectStudio::JobStatus::Completed;
		}) == chainDepth + 1; // Main job + chain depth jobs
	}, DefectStudio::Time::Milliseconds(2000)));

	// Verify execution order matches submission order
	EXPECT_EQ(executionOrder->size(), chainDepth);
	for (int i = 0; i < chainDepth; ++i)
		EXPECT_EQ((*executionOrder)[i], i);

	jobSystem.Shutdown();
}

TEST(JobSystemTests, MultipleJobsCanSubmitOtherJobsConcurrently)
{
	DefectStudio::JobSystem jobSystem;
	const int submitterCount = 3;
	const int chainPerSubmitter = 4;
	auto executionOrder = DefectStudio::CreateRef<std::vector<int>>();

	// Submit multiple jobs that will each submit N other jobs
	std::vector<DefectStudio::JobId> mainIds;
	for (int i = 0; i < submitterCount; ++i)
	{
		auto id = jobSystem.Submit(
			DefectStudio::CreateRef<SequentialSubmitterJob>(chainPerSubmitter, executionOrder));
		mainIds.push_back(id);
	}

	// Wait for all to complete
	ASSERT_TRUE(WaitUntil([&]() {
		const auto allJobs = jobSystem.GetAllJobs();
		return std::count_if(allJobs.begin(), allJobs.end(), [](const DefectStudio::JobSnapshot &snap) {
			return snap.status == DefectStudio::JobStatus::Completed;
		}) == submitterCount + (submitterCount * chainPerSubmitter); // Submitters + all chains
	}, DefectStudio::Time::Milliseconds(3000)));

	// All submitted jobs should be in execution order
	EXPECT_EQ(executionOrder->size(), submitterCount * chainPerSubmitter);

	jobSystem.Shutdown();
}

TEST(JobSystemTests, PublishesQueuedStartedProgressAndCompletedEvents)
{
	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	DefectStudio::JobSystem jobSystem(DefectStudio::CreateWeakRef(eventBus));

	std::atomic<int> queuedCount = 0;
	std::atomic<int> startedCount = 0;
	std::atomic<int> progressCount = 0;
	std::atomic<int> completedCount = 0;

	auto queuedSub = eventBus->Subscribe<DefectStudio::JobQueuedEvent>([&](const DefectStudio::JobQueuedEvent &) {
		++queuedCount;
	});
	auto startedSub = eventBus->Subscribe<DefectStudio::JobStartedEvent>([&](const DefectStudio::JobStartedEvent &) {
		++startedCount;
	});
	auto progressSub = eventBus->Subscribe<DefectStudio::JobProgressEvent>([&](const DefectStudio::JobProgressEvent &) {
		++progressCount;
	});
	auto completedSub = eventBus->Subscribe<DefectStudio::JobCompletedEvent>([&](const DefectStudio::JobCompletedEvent &) {
		++completedCount;
	});

	const auto id = jobSystem.Submit(DefectStudio::CreateRef<DefectStudio::SleepJob>("events", 5, DefectStudio::Time::Milliseconds(2)));
	ASSERT_GT(id, 0u);

	ASSERT_TRUE(WaitUntil([&]() {
		eventBus->ProcessQueue();
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == DefectStudio::JobStatus::Completed;
	}, DefectStudio::Time::Milliseconds(1500)));

	eventBus->ProcessQueue();

	EXPECT_GE(queuedCount.load(), 1);
	EXPECT_GE(startedCount.load(), 1);
	EXPECT_GE(progressCount.load(), 1);
	EXPECT_GE(completedCount.load(), 1);

	jobSystem.Shutdown();
}

TEST(JobSystemTests, ProgressTrackerBuildsSnapshotFromLifecycleEvents)
{
	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	DefectStudio::ProgressTracker tracker(DefectStudio::CreateWeakRef(eventBus));

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

TEST(JobSystemTests, ProgressTrackerStoresErrorSummaryAndViews)
{
	auto eventBus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	DefectStudio::ProgressTracker tracker(DefectStudio::CreateWeakRef(eventBus));

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
