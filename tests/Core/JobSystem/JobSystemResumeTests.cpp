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

TEST(JobSystemResumeTests, ResumeRequeuesCancelledJobAndCompletes)
{
	JobSystem jobSystem({}, 2);

	const auto id = jobSystem.SubmitAfter(
		CreateRef<SleepJob>("resume-delayed", 5, Time::Milliseconds(5)),
		Time::Milliseconds(200),
		JobPriority::Normal);
	ASSERT_GT(id, 0u);

	std::this_thread::sleep_for(Time::Milliseconds(30));
	ASSERT_TRUE(jobSystem.RequestCancel(id));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Cancelled;
	}, Time::Milliseconds(800)));

	ASSERT_TRUE(jobSystem.Resume(id));

	ASSERT_TRUE(WaitUntil([&]() {
		auto snapshot = jobSystem.GetJob(id);
		return snapshot.has_value() && snapshot->status == JobStatus::Completed;
	}, Time::Milliseconds(1500)));

	const auto logs = jobSystem.GetLogs(id);
	EXPECT_TRUE(std::any_of(logs.begin(), logs.end(), [](const JobLogEntry &entry) {
		return entry.message == "Resume";
	}));

	jobSystem.Shutdown();
}
