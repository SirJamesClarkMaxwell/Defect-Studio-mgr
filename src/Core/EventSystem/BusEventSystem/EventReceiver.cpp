#include "Core/dspch.hpp"

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	void EventReceiver::AddSubscription(SubscriptionHandle handle)
	{
		ZoneScoped;
		if (!handle.IsValid())
			return;

		m_Subscriptions.push_back(std::move(handle));
		DS_LOG_TRACE("EventReceiver add subscription count={}", m_Subscriptions.size());
	}

	void EventReceiver::ClearSubscriptions()
	{
		ZoneScoped;
		m_Subscriptions.clear();
		DS_LOG_TRACE("EventReceiver clear subscriptions");
	}

	void EventReceiver::SetEnabled(bool enabled)
	{
		ZoneScoped;
		m_Enabled = enabled;
		DS_LOG_TRACE("EventReceiver enabled={}", m_Enabled);
	}

	bool EventReceiver::IsEnabled() const
	{
		return m_Enabled;
	}
} // namespace DefectStudio
