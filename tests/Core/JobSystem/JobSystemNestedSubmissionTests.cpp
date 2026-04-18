#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <vector>

#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;

	class ChildProbeJob final : public IJob
	{
	public:
		explicit ChildProbeJob(Ref<std::atomic_bool> ran)
			: m_Ran(std::move(ran))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "ChildProbeJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "ChildProbeJob";
		}

		void Execute(JobContext &) override
		{
			m_Ran->store(true, std::memory_order_relaxed);
		}

	private:
		Ref<std::atomic_bool> m_Ran;
	};

	class ParentSequentialWaitProbeJob final : public IJob
	{
	public:
		ParentSequentialWaitProbeJob(Ref<std::atomic_bool> childRan, Ref<std::atomic_bool> waitSucceeded)
			: m_ChildRan(std::move(childRan)),
			  m_WaitSucceeded(std::move(waitSucceeded))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "ParentSequentialWaitProbeJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "ParentSequentialWaitProbeJob";
		}

		void Execute(JobContext &context) override
		{
			const auto child = CreateRef<ChildProbeJob>(m_ChildRan);
			const JobId childId = context.SubmitJobSequential(child, JobPriority::Normal);
			m_WaitSucceeded->store(childId != 0, std::memory_order_relaxed);
		}

	private:
		Ref<std::atomic_bool> m_ChildRan;
		Ref<std::atomic_bool> m_WaitSucceeded;
	};
}

TEST(JobSystemNestedSubmissionTests, JobCanSubmitOtherJobsSequentially)
{
	JobSystem jobSystem;
	const int chainDepth = 5;
	auto executionOrder = CreateRef<std::vector<int>>();
	auto executionOrderMutex = CreateRef<std::mutex>();

	const auto mainId = jobSystem.Submit(CreateRef<SequentialSubmitterJob>(chainDepth, executionOrder, executionOrderMutex));
	(void)mainId;

	ASSERT_TRUE(WaitUntil([&]() {
		const auto allJobs = jobSystem.GetAllJobs();
		return std::count_if(allJobs.begin(), allJobs.end(), [](const JobSnapshot &snap) {
			return snap.status == JobStatus::Completed;
		}) == chainDepth + 1;
	}, Time::Milliseconds(2000)));

	EXPECT_EQ(executionOrder->size(), chainDepth);
	for (int i = 0; i < chainDepth; ++i)
		EXPECT_EQ((*executionOrder)[i], i);

	jobSystem.Shutdown();
}

TEST(JobSystemNestedSubmissionTests, MultipleJobsCanSubmitOtherJobsConcurrently)
{
	JobSystem jobSystem;
	const int submitterCount = 3;
	const int chainPerSubmitter = 4;
	auto executionOrder = CreateRef<std::vector<int>>();
	auto executionOrderMutex = CreateRef<std::mutex>();

	for (int i = 0; i < submitterCount; ++i)
		(void)jobSystem.Submit(CreateRef<SequentialSubmitterJob>(chainPerSubmitter, executionOrder, executionOrderMutex));

	ASSERT_TRUE(WaitUntil([&]() {
		const auto allJobs = jobSystem.GetAllJobs();
		return std::count_if(allJobs.begin(), allJobs.end(), [](const JobSnapshot &snap) {
			return snap.status == JobStatus::Completed;
		}) == submitterCount + (submitterCount * chainPerSubmitter);
	}, Time::Milliseconds(3000)));

	EXPECT_EQ(executionOrder->size(), submitterCount * chainPerSubmitter);
	jobSystem.Shutdown();
}

TEST(JobSystemNestedSubmissionTests, SequentialNestedSubmissionEscapesDeadlockInSingleWorkerMode)
{
	JobSystem jobSystem({}, 1);
	auto childRan = CreateRef<std::atomic_bool>(false);
	auto waitSucceeded = CreateRef<std::atomic_bool>(true);

	const JobId parentId = jobSystem.Submit(CreateRef<ParentSequentialWaitProbeJob>(childRan, waitSucceeded));

	ASSERT_TRUE(WaitUntil([&]() {
		const auto parent = jobSystem.GetJob(parentId);
		return parent.has_value() && (parent->status == JobStatus::Completed || parent->status == JobStatus::Failed || parent->status == JobStatus::Cancelled);
	}, Time::Milliseconds(2000)));

	// In single-worker mode, cooperative wait should fail fast to avoid deadlock.
	EXPECT_FALSE(waitSucceeded->load(std::memory_order_relaxed));

	jobSystem.Shutdown();
}

TEST(JobSystemNestedSubmissionTests, SequentialNestedSubmissionWaitsSuccessfullyWithTwoWorkers)
{
	JobSystem jobSystem({}, 2);
	auto childRan = CreateRef<std::atomic_bool>(false);
	auto waitSucceeded = CreateRef<std::atomic_bool>(false);

	const JobId parentId = jobSystem.Submit(CreateRef<ParentSequentialWaitProbeJob>(childRan, waitSucceeded));

	ASSERT_TRUE(WaitUntil([&]() {
		const auto parent = jobSystem.GetJob(parentId);
		return parent.has_value() && parent->status == JobStatus::Completed;
	}, Time::Milliseconds(2000)));

	EXPECT_TRUE(waitSucceeded->load(std::memory_order_relaxed));
	EXPECT_TRUE(childRan->load(std::memory_order_relaxed));

	jobSystem.Shutdown();
}
