#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Events/Event.hpp"
#include "Core/Events/EventPriority.hpp"
#include "Core/Events/SubscriptionHandle.hpp"
#include "Core/Memory.hpp"

namespace DefectStudio
{

	template <typename TEvent>
	concept BusEventType = std::derived_from<TEvent, BusEvent>;

	template <typename THandler, typename TEvent>
	concept BusEventHandler = std::invocable<THandler, TEvent &> || std::invocable<THandler, const TEvent &>;

	struct Listener
	{
		std::size_t id = 0;
		EventPriority priority = EventPriority::Normal;
		std::function<void(BusEvent &)> callback;
		bool pendingRemoval = false;
		bool skipThisDispatch = false;
	};

	struct ListenerList
	{
		std::vector<Listener> listeners;
		std::vector<Listener> pendingAdditions;
		bool dispatching = false;
	};

	struct QueuedEvent
	{
		std::size_t typeId = 0;
		Unique<BusEvent> event;
	};


	class EventBus : public std::enable_shared_from_this<EventBus>
	{
	public:
		EventBus() = default;
		~EventBus() = default;

		EventBus(const EventBus &) = delete;
		EventBus &operator=(const EventBus &) = delete;
		EventBus(EventBus &&) = delete;
		EventBus &operator=(EventBus &&) = delete;

		template <BusEventType TEvent, typename THandler>
		requires BusEventHandler<THandler, TEvent>
		SubscriptionHandle Subscribe(THandler &&handler, EventPriority priority = EventPriority::Normal);

		template <BusEventType TEvent>
		void Publish(const TEvent &event);

		template <BusEventType TEvent, typename... TArgs>
		void PublishNew(TArgs &&...args); // Convenience: construct the event inline and publish immediately.

		template <BusEventType TEvent>
		void Queue(const TEvent &event);

		template <BusEventType TEvent, typename... TArgs>
		void QueueNew(TArgs &&...args);  // Convenience: construct the event inline and enqueue it.

		void ProcessQueue();
		void ClearAllListeners();
		void ClearQueue();

		template <BusEventType TEvent>
		[[nodiscard]] bool HasSubscribers() const;

		template <BusEventType TEvent>
		[[nodiscard]] std::size_t GetSubscriberCount() const;

		[[nodiscard]] std::size_t GetTotalSubscriberCount() const;
		[[nodiscard]] std::size_t GetQueuedEventCount() const;
		[[nodiscard]] bool IsDispatching() const;

	private:
		static std::size_t GetTypeId(const std::type_index &typeIndex);
		void dispatchByType(std::size_t typeId, BusEvent &event);
		bool unsubscribeById(std::size_t typeId, std::size_t subscriptionId);
		static void sortByPriority(std::vector<Listener> &listeners);
		static std::size_t countActive(const ListenerList &list);

		std::unordered_map<std::size_t, ListenerList> m_Listeners;
		std::size_t m_NextSubscriptionId = 0;

		std::vector<QueuedEvent> m_Queue;
		mutable std::mutex m_QueueMutex;

		std::size_t m_DispatchDepth = 0;

		friend class SubscriptionHandle;
	};

	template <typename TObject, BusEventType TEvent>
	SubscriptionHandle SubscribeMember(
		EventBus &bus,
		const Ref<TObject> &instance,
		void (TObject::*method)(const TEvent &),
		EventPriority priority = EventPriority::Normal);

	// ===== Template implementation =====

	template <BusEventType TEvent, typename THandler>
	requires BusEventHandler<THandler, TEvent>
	SubscriptionHandle EventBus::Subscribe(THandler &&handler, EventPriority priority)
	{
		using Handler = std::decay_t<THandler>;

		Listener listener;
		listener.id = ++m_NextSubscriptionId;
		listener.priority = priority;

		if constexpr (std::is_invocable_v<Handler, TEvent &>)
		{
			listener.callback = [handler = std::forward<THandler>(handler)](BusEvent &event) mutable
			{
				handler(static_cast<TEvent &>(event));
			};
		}
		else
		{
			static_assert(std::is_invocable_v<Handler, const TEvent &>,
				"Subscribe handler must accept TEvent& or const TEvent&");
			listener.callback = [handler = std::forward<THandler>(handler)](BusEvent &event) mutable
			{
				handler(static_cast<const TEvent &>(event));
			};
		}

		const std::size_t typeId = GetTypeId(std::type_index(typeid(TEvent)));
		auto &list = m_Listeners[typeId];
		if (list.dispatching)
		{
			list.pendingAdditions.push_back(std::move(listener));
		}
		else
		{
			list.listeners.push_back(std::move(listener));
			sortByPriority(list.listeners);
		}

		return SubscriptionHandle(shared_from_this(), typeId, m_NextSubscriptionId);
	}

	template <BusEventType TEvent>
	void EventBus::Publish(const TEvent &event)
	{
		dispatchByType(GetTypeId(std::type_index(typeid(TEvent))), const_cast<TEvent &>(event));
	}

	template <BusEventType TEvent, typename... TArgs>
	void EventBus::PublishNew(TArgs &&...args)
	{
		TEvent event(std::forward<TArgs>(args)...);
		Publish(event);
	}

	template <BusEventType TEvent>
	void EventBus::Queue(const TEvent &event)
	{
		QueuedEvent queued;
		queued.typeId = GetTypeId(std::type_index(typeid(TEvent)));
		queued.event = CreateUnique<TEvent>(event);

		std::scoped_lock lock(m_QueueMutex);
		m_Queue.push_back(std::move(queued));
	}

	template <BusEventType TEvent, typename... TArgs>
	void EventBus::QueueNew(TArgs &&...args)
	{
		TEvent event(std::forward<TArgs>(args)...);
		Queue(event);
	}

	template <BusEventType TEvent>
	[[nodiscard]] bool EventBus::HasSubscribers() const
	{
		const std::size_t typeId = GetTypeId(std::type_index(typeid(TEvent)));
		auto it = m_Listeners.find(typeId);
		if (it == m_Listeners.end())
			return false;

		return countActive(it->second) > 0;
	}

	template <BusEventType TEvent>
	[[nodiscard]] std::size_t EventBus::GetSubscriberCount() const
	{
		const std::size_t typeId = GetTypeId(std::type_index(typeid(TEvent)));
		auto it = m_Listeners.find(typeId);
		if (it == m_Listeners.end())
			return 0;

		return countActive(it->second);
	}

	template <typename TObject, BusEventType TEvent>
	SubscriptionHandle SubscribeMember(
		EventBus &bus,
		const Ref<TObject> &instance,
		void (TObject::*method)(const TEvent &),
		EventPriority priority)
	{
		WeakRef<TObject> weakInstance = CreateWeakRef(instance);
		return bus.Subscribe<TEvent>(
			[weakInstance, method](const TEvent &event) {
				auto locked = weakInstance.lock();
				if (locked)
					(locked.get()->*method)(event);
			},
			priority);
	}
} // namespace DefectStudio
