#include "Core/dspch.hpp"

#include "Core/Events/SubscriptionHandle.hpp"

#include "Core/Events/EventBus.hpp"

namespace DefectStudio
{
	SubscriptionHandle::SubscriptionHandle(WeakRef<EventBus> bus, std::size_t typeId, std::size_t subscriptionId)
		: m_Bus(std::move(bus)), m_TypeId(typeId), m_SubscriptionId(subscriptionId)
	{
	}

	SubscriptionHandle::~SubscriptionHandle()
	{
		Reset();
	}

	SubscriptionHandle::SubscriptionHandle(SubscriptionHandle &&other) noexcept
		: m_Bus(std::move(other.m_Bus)), m_TypeId(other.m_TypeId), m_SubscriptionId(other.m_SubscriptionId)
	{
		other.m_TypeId = 0;
		other.m_SubscriptionId = 0;
	}

	SubscriptionHandle &SubscriptionHandle::operator=(SubscriptionHandle &&other) noexcept
	{
		if (this == &other)
			return *this;

		Reset();
		m_Bus = std::move(other.m_Bus);
		m_TypeId = other.m_TypeId;
		m_SubscriptionId = other.m_SubscriptionId;

		other.m_TypeId = 0;
		other.m_SubscriptionId = 0;
		return *this;
	}

	void SubscriptionHandle::Reset()
	{
		if (m_SubscriptionId == 0)
			return;

		auto bus = m_Bus.lock();
		if (bus)
			bus->unsubscribeById(m_TypeId, m_SubscriptionId);

		m_Bus.reset();
		m_TypeId = 0;
		m_SubscriptionId = 0;
	}

	bool SubscriptionHandle::IsValid() const
	{
		return !m_Bus.expired() && m_SubscriptionId != 0;
	}
} // namespace DefectStudio
