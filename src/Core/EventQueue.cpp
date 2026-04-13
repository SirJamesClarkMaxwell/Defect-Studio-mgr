#include "Core/dspch.hpp"

#include "Core/EventQueue.hpp"

#include <algorithm>

#include "Core/Logger.hpp"

namespace DefectStudio
{
	void EventQueue::Configure(std::size_t initialCapacity, std::size_t growthStep)
	{
		ZoneScoped;
		const std::size_t safeCapacity = std::max<std::size_t>(initialCapacity, 32);
		std::scoped_lock lock(m_Guard);
		m_GrowthStep = std::max<std::size_t>(growthStep, 32);
		m_Pending.clear();
		m_Pending.reserve(safeCapacity);
		DS_LOG_INFO("EventQueue configured capacity={} growthStep={}", safeCapacity, m_GrowthStep);
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

		std::vector<EventVariant> resized;
		resized.reserve(targetCapacity);
		for (auto &event : m_Pending)
			resized.push_back(std::move(event));

		m_Pending.swap(resized);
	}

	void EventQueue::FitToSize()
	{
		std::scoped_lock lock(m_Guard);
		std::vector<EventVariant> fitted;
		fitted.reserve(m_Pending.size());
		for (auto &event : m_Pending)
			fitted.push_back(std::move(event));

		m_Pending.swap(fitted);
	}

	void EventQueue::Add(EventVariant event)
	{
		ZoneScoped;
		std::scoped_lock lock(m_Guard);
		EnsureCapacityLocked();
		m_Pending.push_back(std::move(event));
		DS_LOG_TRACE("EventQueue add size={} capacity={}", m_Pending.size(), m_Pending.capacity());
	}

	std::vector<EventVariant> EventQueue::Drain()
	{
		ZoneScoped;
		std::vector<EventVariant> events;
		std::scoped_lock lock(m_Guard);
		if (m_Pending.empty())
			return events;

		events.swap(m_Pending);
		DS_LOG_TRACE("EventQueue drain count={}", events.size());
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
