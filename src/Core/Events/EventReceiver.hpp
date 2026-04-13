#pragma once

#include <vector>

#include "Core/Events/SubscriptionHandle.hpp"

namespace DefectStudio
{
	class EventReceiver
	{
	public:
		virtual ~EventReceiver() = default;

		void AddSubscription(SubscriptionHandle handle);
		void ClearSubscriptions();
		void SetEnabled(bool enabled);
		[[nodiscard]] bool IsEnabled() const;

	protected:
		std::vector<SubscriptionHandle> m_Subscriptions;
		bool m_Enabled = true;
	};
} // namespace DefectStudio
