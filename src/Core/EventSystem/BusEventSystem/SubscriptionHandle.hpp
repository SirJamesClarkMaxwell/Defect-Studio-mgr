#pragma once

#include <cstddef>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;

	class SubscriptionHandle
	{
	public:
		SubscriptionHandle() = default;
		~SubscriptionHandle();

		SubscriptionHandle(const SubscriptionHandle &) = delete;
		SubscriptionHandle &operator=(const SubscriptionHandle &) = delete;
		SubscriptionHandle(SubscriptionHandle &&other) noexcept;
		SubscriptionHandle &operator=(SubscriptionHandle &&other) noexcept;

		void Reset();
		[[nodiscard]] bool IsValid() const;

	private:
		SubscriptionHandle(WeakRef<EventBus> bus, std::size_t typeId, std::size_t subscriptionId);

		WeakRef<EventBus> m_Bus;
		std::size_t m_TypeId = 0;
		std::size_t m_SubscriptionId = 0;

		friend class EventBus;
	};
} // namespace DefectStudio
