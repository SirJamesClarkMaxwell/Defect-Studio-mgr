#pragma once

#include "Core/dspch.hpp"

namespace DefectStudio
{
	class EventBus
	{
	public:
		using SubscriptionId = std::uint64_t;

		template <typename TEvent, typename THandler> 
		SubscriptionId Subscribe(THandler &&handler)
		{
			std::function<void(const TEvent &)> typedHandler(std::forward<THandler>(handler));

			Subscription subscription;
			subscription.id = ++m_LastId;
			subscription.callback = [typedHandler = std::move(typedHandler)](const void *event)
			{ typedHandler(*static_cast<const TEvent *>(event)); };

			auto &subscriptions = m_Subscriptions[std::type_index(typeid(TEvent))];
			subscriptions.push_back(std::move(subscription));
			return subscriptions.back().id;
		}

		template <typename TEvent> 
		bool Unsubscribe(SubscriptionId id)
		{
			auto it = m_Subscriptions.find(std::type_index(typeid(TEvent)));
			if (it == m_Subscriptions.end())
				return false;

			auto &subscriptions = it->second;
			auto removeIt = std::find_if(subscriptions.begin(), subscriptions.end(),
			                             [id](const Subscription &entry) { return entry.id == id; });
			if (removeIt == subscriptions.end())
				return false;

			subscriptions.erase(removeIt);
			if (subscriptions.empty())
				m_Subscriptions.erase(it);
			return true;
		}

		template <typename TEvent> 
		void Publish(const TEvent &event) const
		{
			auto it = m_Subscriptions.find(std::type_index(typeid(TEvent)));
			if (it == m_Subscriptions.end())
				return;

			for (const Subscription &subscription : it->second)
				subscription.callback(&event);
		}

	private:
		struct Subscription
		{
			SubscriptionId id = 0;
			std::function<void(const void *)> callback;
		};

		std::unordered_map<std::type_index, std::vector<Subscription>> m_Subscriptions;
		SubscriptionId m_LastId = 0;
	};
} // namespace DefectStudio
