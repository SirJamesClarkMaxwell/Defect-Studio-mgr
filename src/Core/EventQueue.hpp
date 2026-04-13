#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

#include "Core/Platform/Events/PlatformEvent.hpp"

namespace DefectStudio
{
	class EventQueue
	{
	public:
		void Configure(std::size_t initialCapacity, std::size_t growthStep = 256);
		void Lock();
		void Unlock();
		void Resize(std::size_t newCapacity);
		void FitToSize();

		void Add(EventVariant event);

		[[nodiscard]] std::vector<EventVariant> Drain();
		[[nodiscard]] std::size_t Size() const;
		[[nodiscard]] std::size_t Capacity() const;
		[[nodiscard]] bool Empty() const;

		void SetGrowthStep(std::size_t growthStep);

	private:
		void EnsureCapacityLocked();

		std::size_t m_GrowthStep = 256;
		std::vector<EventVariant> m_Pending;
		mutable std::recursive_mutex m_Guard;
	};
} // namespace DefectStudio
