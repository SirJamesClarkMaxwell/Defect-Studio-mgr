#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Capabilities/Capability.hpp"

namespace DefectStudio
{
	class CapabilityRegistry
	{
	public:
		void RegisterCapability(CapabilityEntry capability);
		void LockAfterStartup();

		[[nodiscard]] bool IsAvailable(const std::string &name) const;
		[[nodiscard]] std::vector<CapabilityEntry> GetAll() const;
		[[nodiscard]] bool IsLocked() const;

	private:
		mutable std::mutex m_Mutex;
		std::unordered_map<std::string, CapabilityEntry> m_Capabilities;
		bool m_Locked = false;
	};
} // namespace DefectStudio