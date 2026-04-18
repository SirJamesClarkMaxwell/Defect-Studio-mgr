#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Time.hpp"

namespace DefectStudio::Tests
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

	class BlockingJob final : public IJob
	{
	public:
		explicit BlockingJob(Ref<Gate> gate)
			: m_Gate(std::move(gate))
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

		void Execute(JobContext &context) override
		{
			context.SetStage("waiting");
			m_Gate->Wait();
			context.SetStage("done");
			context.SetProgress(1.0f, 1.0f);
		}

	private:
		Ref<Gate> m_Gate;
	};

	class LoggingJob final : public IJob
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

		void Execute(JobContext &context) override
		{
			context.LogInfo("first");
			context.LogWarning("second");
			context.LogError("third");
		}
	};

	class ChainedJob final : public IJob
	{
	public:
		ChainedJob(int jobIndex, Ref<std::vector<int>> executionOrder, Ref<std::mutex> executionOrderMutex)
			: m_JobIndex(jobIndex),
			  m_ExecutionOrder(std::move(executionOrder)),
			  m_ExecutionOrderMutex(std::move(executionOrderMutex))
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

		void Execute(JobContext &context) override
		{
			std::lock_guard<std::mutex> lock(*m_ExecutionOrderMutex);
			m_ExecutionOrder->push_back(m_JobIndex);
			context.SetProgress(1.0f, 1.0f);
			context.SetStage("completed");
		}

	private:
		int m_JobIndex;
		Ref<std::vector<int>> m_ExecutionOrder;
		Ref<std::mutex> m_ExecutionOrderMutex;
	};

	class SequentialSubmitterJob final : public IJob
	{
	public:
		SequentialSubmitterJob(int chainDepth, Ref<std::vector<int>> executionOrder, Ref<std::mutex> executionOrderMutex)
			: m_ChainDepth(chainDepth),
			  m_ExecutionOrder(std::move(executionOrder)),
			  m_ExecutionOrderMutex(std::move(executionOrderMutex))
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

		void Execute(JobContext &context) override
		{
			context.SetStage("submitting");
			for (int i = 0; i < m_ChainDepth; ++i)
			{
				auto chainedJob = CreateRef<ChainedJob>(i, m_ExecutionOrder, m_ExecutionOrderMutex);
				const auto id = context.SubmitJobSequential(chainedJob, JobPriority::Normal);
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
		Ref<std::vector<int>> m_ExecutionOrder;
		Ref<std::mutex> m_ExecutionOrderMutex;
	};

	class ConcurrencyProbeJob final : public IJob
	{
	public:
		ConcurrencyProbeJob(
			Ref<Gate> startGate,
			Ref<Gate> releaseGate,
			Ref<std::atomic<int>> runningNow,
			Ref<std::atomic<int>> maxRunning)
			: m_StartGate(std::move(startGate)),
			  m_ReleaseGate(std::move(releaseGate)),
			  m_RunningNow(std::move(runningNow)),
			  m_MaxRunning(std::move(maxRunning))
		{
		}

		[[nodiscard]] std::string GetName() const override
		{
			return "ConcurrencyProbeJob";
		}

		[[nodiscard]] std::string GetType() const override
		{
			return "ConcurrencyProbeJob";
		}

		void Execute(JobContext &context) override
		{
			m_StartGate->Wait();

			const int active = m_RunningNow->fetch_add(1, std::memory_order_relaxed) + 1;
			int observed = m_MaxRunning->load(std::memory_order_relaxed);
			while (active > observed && !m_MaxRunning->compare_exchange_weak(observed, active, std::memory_order_relaxed))
			{
			}

			context.SetStage("running");
			m_ReleaseGate->Wait();
			context.SetProgress(1.0f, 1.0f);

			m_RunningNow->fetch_sub(1, std::memory_order_relaxed);
		}

	private:
		Ref<Gate> m_StartGate;
		Ref<Gate> m_ReleaseGate;
		Ref<std::atomic<int>> m_RunningNow;
		Ref<std::atomic<int>> m_MaxRunning;
	};

	inline bool WaitUntil(const std::function<bool()> &predicate, Time::Milliseconds timeout)
	{
		const auto start = Time::NowSteady();
		while (Time::NowSteady() - start < timeout)
		{
			if (predicate())
				return true;
			std::this_thread::sleep_for(Time::Milliseconds(2));
		}
		return predicate();
	}
}
