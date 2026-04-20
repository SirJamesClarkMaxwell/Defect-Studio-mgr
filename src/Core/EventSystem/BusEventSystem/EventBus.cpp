#include "Core/dspch.hpp"

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	std::size_t EventBus::GetTypeId(const std::type_index &typeIndex)
	{
		return typeIndex.hash_code();
	}

	void EventBus::dispatchByType(std::size_t typeId, BusEvent &event)
	{
		ZoneScoped;
		auto it = m_Listeners.find(typeId);
		if (it == m_Listeners.end())
			return;

		event.stopPropagation = false;

		ListenerList &list = it->second;
		list.dispatching = true;
		++m_DispatchDepth;
		DS_LOG_TRACE("EventBus dispatch typeId={} listeners={}", typeId, list.listeners.size());

		for (auto &listener : list.listeners)
		{
			if (listener.skipThisDispatch)
				continue;

			listener.callback(event);
			if (event.stopPropagation)
				break;
		}

		list.dispatching = false;
		--m_DispatchDepth;

		for (auto &listener : list.listeners)
			listener.skipThisDispatch = false;

		if (!list.pendingAdditions.empty())
		{
			list.listeners.insert(list.listeners.end(),
			                      std::make_move_iterator(list.pendingAdditions.begin()),
			                      std::make_move_iterator(list.pendingAdditions.end()));
			list.pendingAdditions.clear();
		}

		if (!list.listeners.empty())
		{
			list.listeners.erase(
				std::remove_if(list.listeners.begin(), list.listeners.end(),
					[](const Listener &listener) { return listener.pendingRemoval; }),
				list.listeners.end());
		}

		if (list.listeners.empty())
		{
			m_Listeners.erase(it);
			return;
		}

		sortByPriority(list.listeners);
	}

	bool EventBus::unsubscribeById(std::size_t typeId, std::size_t subscriptionId)
	{
		ZoneScoped;
		auto it = m_Listeners.find(typeId);
		if (it == m_Listeners.end())
			return false;

		ListenerList &list = it->second;
		for (auto &listener : list.listeners)
		{
			if (listener.id != subscriptionId)
				continue;

			if (list.dispatching)
			{
				listener.pendingRemoval = true;
				listener.skipThisDispatch = true;
				return true;
			}

			list.listeners.erase(
				std::remove_if(list.listeners.begin(), list.listeners.end(),
					[subscriptionId](const Listener &entry) { return entry.id == subscriptionId; }),
				list.listeners.end());

			if (list.listeners.empty())
				m_Listeners.erase(it);

			return true;
		}

		return false;
	}

	void EventBus::sortByPriority(std::vector<Listener> &listeners)
	{
		// Ascending enum order delivers Highest(0) before Lower priorities.
		std::stable_sort(listeners.begin(), listeners.end(),
                        [](const Listener &left, const Listener &right) {
                            return static_cast<int>(left.priority) < static_cast<int>(right.priority);
                        });
	}

	std::size_t EventBus::countActive(const ListenerList &list)
	{
		std::size_t count = 0;
		for (const auto &listener : list.listeners)
		{
			if (!listener.pendingRemoval)
				++count;
		}
		count += list.pendingAdditions.size();
		return count;
	}

	void EventBus::ProcessQueue()
	{
		ZoneScoped;
		std::vector<QueuedEvent> pending;
		{
			std::scoped_lock lock(m_QueueMutex);
			pending.swap(m_Queue);
		}
		DS_LOG_TRACE("EventBus queue processing count={}", pending.size());

		for (auto &queued : pending)
		{
			if (!queued.event)
				continue;

			dispatchByType(queued.typeId, *queued.event);
		}
	}

	void EventBus::ClearAllListeners()
	{
		ZoneScoped;
		DS_LOG_TRACE("EventBus clearing listeners types={}", m_Listeners.size());
		for (auto &entry : m_Listeners)
		{
			ListenerList &list = entry.second;
			if (list.dispatching)
			{
				for (auto &listener : list.listeners)
					listener.pendingRemoval = true;

				list.pendingAdditions.clear();
				continue;
			}

			list.listeners.clear();
			list.pendingAdditions.clear();
		}

		for (auto it = m_Listeners.begin(); it != m_Listeners.end();)
		{
			if (it->second.listeners.empty() && it->second.pendingAdditions.empty())
				it = m_Listeners.erase(it);
			else
				++it;
		}
	}

	void EventBus::ClearQueue()
	{
		ZoneScoped;
		std::scoped_lock lock(m_QueueMutex);
		DS_LOG_TRACE("EventBus clearing queue count={}", m_Queue.size());
		m_Queue.clear();
	}

	std::size_t EventBus::GetTotalSubscriberCount() const
	{
		std::size_t total = 0;
		for (const auto &[typeId, list] : m_Listeners)
			(void)typeId, total += countActive(list);
		return total;
	}

	std::size_t EventBus::GetQueuedEventCount() const
	{
		ZoneScoped;
		std::scoped_lock lock(m_QueueMutex);
		return m_Queue.size();
	}

	bool EventBus::IsDispatching() const
	{
		ZoneScoped;
		return m_DispatchDepth > 0;
	}
} // namespace DefectStudio
