#include "Core/dspch.hpp"
#include "Core/Utils/Logger.hpp"

#include "Core/Capabilities/CapabilityRegistry.hpp"

namespace DefectStudio
{
	void CapabilityRegistry::RegisterCapability(CapabilityEntry capability)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_Locked)
		{
			DS_LOG_WARN("CapabilityRegistry registration ignored after lock: {}", capability.name);
			return;
		}

		m_Capabilities[capability.name] = std::move(capability);
	}

	void CapabilityRegistry::LockAfterStartup()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Locked = true;
	}

	bool CapabilityRegistry::IsAvailable(const std::string &name) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Capabilities.find(name);
		if (it == m_Capabilities.end())
			return false;
		return it->second.available;
	}

	std::vector<CapabilityEntry> CapabilityRegistry::GetAll() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		std::vector<CapabilityEntry> entries;
		entries.reserve(m_Capabilities.size());
		for (const auto &[name, capability] : m_Capabilities)
		{
			(void)name;
			entries.push_back(capability);
		}
		return entries;
	}

	bool CapabilityRegistry::IsLocked() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Locked;
	}
} // namespace DefectStudio