#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "Core/JobSystem.hpp"
#include "Core/ProgressTracker.hpp"

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
} // namespace

TEST(JobSystemTests, SubmitsJobsThroughTheWrapper)
{
	DefectStudio::JobSystem jobSystem(1);
	auto future = jobSystem.Submit([] { return 42; });
	EXPECT_EQ(future.get(), 42);
	jobSystem.Wait();
}

TEST(JobSystemTests, HonorsPriorityOrder)
{
	DefectStudio::JobSystem jobSystem(1);
	Gate gate;
	std::atomic<int> executionOrder = 0;
	std::atomic<int> highPriorityOrder = 0;
	std::atomic<int> lowPriorityOrder = 0;

	auto blocker = jobSystem.Submit([&gate] { gate.Wait(); return 0; });
	auto low = jobSystem.Submit([&] {
		lowPriorityOrder = ++executionOrder;
		return 0;
	}, -10);
	auto high = jobSystem.Submit([&] {
		highPriorityOrder = ++executionOrder;
		return 0;
	}, 10);

	gate.Open();
	EXPECT_EQ(blocker.get(), 0);
	EXPECT_EQ(high.get(), 0);
	EXPECT_EQ(low.get(), 0);
	EXPECT_EQ(highPriorityOrder.load(), 1);
	EXPECT_EQ(lowPriorityOrder.load(), 2);
}

TEST(JobSystemTests, CancellationTokenStopsQueuedWork)
{
	DefectStudio::JobSystem jobSystem(1);
	DefectStudio::CancellationSource cancellationSource;
	DefectStudio::CancellationToken token = cancellationSource.GetToken();
	Gate gate;
	std::atomic<bool> ranCancelledJob = false;

	auto blocker = jobSystem.Submit([&gate] { gate.Wait(); return 0; });
	auto cancelled = jobSystem.SubmitCancelable(token, [&ranCancelledJob](const DefectStudio::CancellationToken &) {
		ranCancelledJob = true;
		return 7;
	});

	cancellationSource.Cancel();
	gate.Open();
	EXPECT_EQ(blocker.get(), 0);
	EXPECT_EQ(cancelled.get(), 0);
	EXPECT_FALSE(ranCancelledJob.load());
}

TEST(JobSystemTests, ProgressTrackerStoresSnapshots)
{
	DefectStudio::ProgressTracker tracker;
	const auto id = tracker.Register("Import project", 10);
	tracker.Report(id, 4);
	tracker.Finish(id);

	const auto entries = tracker.Snapshot();
	ASSERT_EQ(entries.size(), 1u);
	EXPECT_EQ(entries.front().id, id);
	EXPECT_EQ(entries.front().label, "Import project");
	EXPECT_EQ(entries.front().totalWork, 10u);
	EXPECT_EQ(entries.front().completedWork, 10u);
	EXPECT_TRUE(entries.front().finished);
	EXPECT_TRUE(tracker.Has(id));
}