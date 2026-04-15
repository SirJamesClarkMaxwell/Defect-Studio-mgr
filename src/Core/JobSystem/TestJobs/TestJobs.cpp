#include "Core/dspch.hpp"

#include "Core/JobSystem/TestJobs/TestJobs.hpp"


#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	SleepJob::SleepJob(std::string name, int steps, Time::Milliseconds stepDelay)
		: m_Name(std::move(name)),
		  m_Steps(std::max(1, steps)),
		  m_StepDelay(stepDelay)
	{
	}

	std::string SleepJob::GetName() const
	{
		return m_Name;
	}

	std::string SleepJob::GetType() const
	{
		return "SleepJob";
	}

	void SleepJob::Execute(JobContext &context)
	{
		context.SetStage("running");
		for (int step = 1; step <= m_Steps; ++step)
		{
			context.ThrowIfCancellationRequested();
			std::this_thread::sleep_for(m_StepDelay);
			context.SetProgress(static_cast<float>(step), static_cast<float>(m_Steps));
			context.SetMessage("step " + std::to_string(step));
		}
		context.SetStage("finished");
	}

	FailingJob::FailingJob(std::string message)
		: m_Message(std::move(message))
	{
	}

	std::string FailingJob::GetName() const
	{
		return "FailingJob";
	}

	std::string FailingJob::GetType() const
	{
		return "FailingJob";
	}

	void FailingJob::Execute(JobContext &context)
	{
		context.SetStage("failed");
		context.LogError(m_Message);
		throw std::runtime_error(m_Message);
	}
} // namespace DefectStudio
