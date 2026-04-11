#include "Core/dspch.hpp"

#include "Core/EventQueue.hpp"

#include <algorithm>

namespace DefectStudio
{
	void EventQueue::Configure(std::size_t initialCapacity, std::size_t growthStep)
	{
		const std::size_t safeCapacity = std::max<std::size_t>(initialCapacity, 32);
		std::scoped_lock lock(m_Guard);
		m_GrowthStep = std::max<std::size_t>(growthStep, 32);
		m_Pending.clear();
		m_Pending.reserve(safeCapacity);
	}

	void EventQueue::Lock()
	{
		m_Guard.lock();
	}

	void EventQueue::Unlock()
	{
		m_Guard.unlock();
	}

	void EventQueue::Resize(std::size_t newCapacity)
	{
		std::scoped_lock lock(m_Guard);
		const std::size_t targetCapacity = std::max(newCapacity, m_Pending.size());
		if (targetCapacity == m_Pending.capacity())
			return;

		std::vector<Unique<Event>> resized;
		resized.reserve(targetCapacity);
		for (auto &event : m_Pending)
			resized.push_back(std::move(event));

		m_Pending.swap(resized);
	}

	void EventQueue::FitToSize()
	{
		std::scoped_lock lock(m_Guard);
		std::vector<Unique<Event>> fitted;
		fitted.reserve(m_Pending.size());
		for (auto &event : m_Pending)
			fitted.push_back(std::move(event));

		m_Pending.swap(fitted);
	}

	void EventQueue::Add(Unique<Event> event)
	{
		std::scoped_lock lock(m_Guard);
		EnsureCapacityLocked();
		m_Pending.push_back(std::move(event));
	}

	std::vector<Unique<Event>> EventQueue::Drain()
	{
		std::vector<Unique<Event>> events;
		std::scoped_lock lock(m_Guard);
		if (m_Pending.empty())
			return events;

		events.swap(m_Pending);
		return events;
	}

	std::size_t EventQueue::Size() const
	{
		std::scoped_lock lock(m_Guard);
		return m_Pending.size();
	}

	std::size_t EventQueue::Capacity() const
	{
		std::scoped_lock lock(m_Guard);
		return m_Pending.capacity();
	}

	bool EventQueue::Empty() const
	{
		return Size() == 0;
	}

	void EventQueue::SetGrowthStep(std::size_t growthStep)
	{
		std::scoped_lock lock(m_Guard);
		m_GrowthStep = std::max<std::size_t>(growthStep, 32);
	}

	void EventQueue::EnsureCapacityLocked()
	{
		if (m_Pending.size() != m_Pending.capacity())
			return;

		m_Pending.reserve(m_Pending.capacity() + m_GrowthStep);
	}
} // namespace DefectStudio
