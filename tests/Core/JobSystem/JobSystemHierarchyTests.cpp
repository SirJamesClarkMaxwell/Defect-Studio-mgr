#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "JobSystemTestUtils.hpp"

namespace
{
	using namespace DefectStudio;
	using namespace DefectStudio::Tests;

	class NestedFanoutJob final : public IJob
	{
	public:
		NestedFanoutJob(int level, int maxLevel, int fanout)
			: m_Level(level), m_MaxLevel(maxLevel), m_Fanout(fanout)
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "NestedFanoutJob-L" + std::to_string(m_Level);
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "NestedFanoutJob";
		}

		void Execute(JobContext &context) override
		{
			if (m_Level < m_MaxLevel)
			{
				for (int index = 0; index < m_Fanout; ++index)
				{
					auto child = CreateRef<NestedFanoutJob>(m_Level + 1, m_MaxLevel, m_Fanout);
					if (context.SubmitJob(child, JobPriority::Normal) == 0)
						context.LogError("Failed to submit nested child");
				}
			}

			context.SetStage("done");
			context.SetProgress(1.0f, 1.0f);
		}

	private:
		int m_Level = 0;
		int m_MaxLevel = 0;
		int m_Fanout = 0;
	};
}

TEST(JobSystemHierarchyTests, NestedSubtasksThreeLevelsBranchingThreeWithTwoThreads)
{
	auto eventBus = CreateRef<EventBus>();
	JobSystem jobSystem(eventBus, 2);
	ProgressTracker tracker(eventBus);

	const auto rootId = jobSystem.Submit(CreateRef<NestedFanoutJob>(0, 3, 3));
	ASSERT_GT(rootId, 0u);

	constexpr std::size_t expectedJobCount = 40;
	ASSERT_TRUE(WaitUntil([&]() {
		eventBus->ProcessQueue();
		const auto all = jobSystem.GetAllJobs();
		if (all.size() != expectedJobCount)
			return false;

		return std::all_of(all.begin(), all.end(), [](const JobSnapshot &snapshot) {
			return snapshot.status == JobStatus::Completed;
		});
	}, Time::Milliseconds(5000)));

	const auto allJobs = jobSystem.GetAllJobs();
	ASSERT_EQ(allJobs.size(), expectedJobCount);

	std::vector<JobId> level1;
	std::vector<JobId> level2;
	std::vector<JobId> level3;

	for (const auto &job : allJobs)
	{
		if (job.parentId == rootId)
			level1.push_back(job.id);
	}
	ASSERT_EQ(level1.size(), 3u);

	for (const auto &job : allJobs)
	{
		if (std::find(level1.begin(), level1.end(), job.parentId) != level1.end())
			level2.push_back(job.id);
	}
	ASSERT_EQ(level2.size(), 9u);

	for (const auto &job : allJobs)
	{
		if (std::find(level2.begin(), level2.end(), job.parentId) != level2.end())
			level3.push_back(job.id);
	}
	ASSERT_EQ(level3.size(), 27u);

	const auto trackerSnapshots = tracker.GetAllSnapshots();
	ASSERT_EQ(trackerSnapshots.size(), expectedJobCount);
	EXPECT_TRUE(std::any_of(trackerSnapshots.begin(), trackerSnapshots.end(), [rootId](const ProgressEntrySnapshot &snapshot) {
		return snapshot.parentId == rootId;
	}));

	jobSystem.Shutdown();
}
