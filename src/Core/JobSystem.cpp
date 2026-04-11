#include "Core/dspch.hpp"

#include "Core/JobSystem.hpp"

namespace DefectStudio
{
	static std::size_t ResolveThreadCount(std::size_t requested)
	{
		if (requested != 0)
			return requested;

		const std::size_t detected = std::thread::hardware_concurrency();
		return detected == 0 ? 1 : detected;
	}

	CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> state) : m_State(std::move(state)) {}

	bool CancellationToken::IsCancellationRequested() const
	{
		return m_State != nullptr && m_State->load(std::memory_order_relaxed);
	}

	bool CancellationToken::IsValid() const
	{
		return m_State != nullptr;
	}

	CancellationSource::CancellationSource() : m_State(std::make_shared<std::atomic_bool>(false)) {}

	CancellationToken CancellationSource::GetToken() const
	{
		return CancellationToken(m_State);
	}

	void CancellationSource::Cancel()
	{
		if (m_State != nullptr)
			m_State->store(true, std::memory_order_relaxed);
	}

	JobSystem::JobSystem(std::size_t threadCount)
	    : m_Pool(ResolveThreadCount(threadCount))
	{
	}

	std::size_t JobSystem::GetThreadCount() const
	{
		return m_Pool.get_thread_count();
	}

	std::size_t JobSystem::GetQueuedJobs() const
	{
		return m_Pool.get_tasks_queued();
	}

	std::size_t JobSystem::GetRunningJobs() const
	{
		return m_Pool.get_tasks_running();
	}

	std::size_t JobSystem::GetTotalJobs() const
	{
		return m_Pool.get_tasks_total();
	}

	void JobSystem::Wait()
	{
		m_Pool.wait();
	}

	void JobSystem::Purge()
	{
		m_Pool.purge();
	}
} // namespace DefectStudio