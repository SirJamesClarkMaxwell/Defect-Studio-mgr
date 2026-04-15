#pragma once

#include <chrono>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"

namespace DefectStudio
{
	class SleepJob final : public IJob
	{
	public:
		SleepJob(std::string name, int steps, Milliseconds::milliseconds stepDelay);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		std::string m_Name;
		int m_Steps = 1;
		Time::Milliseconds m_StepDelay{1};
	};

	class FailingJob final : public IJob
	{
	public:
		explicit FailingJob(std::string message);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		std::string m_Message;
	};
} // namespace DefectStudio
