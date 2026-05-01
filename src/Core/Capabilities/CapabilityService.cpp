#include "Core/dspch.hpp"

#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	CapabilityService::CapabilityService(CapabilityRegistry &registry)
		: m_Registry(registry)
	{
	}

	bool CapabilityService::IsAvailable(const std::string &name) const
	{
		return m_Registry.IsAvailable(name);
	}

	void CapabilityService::Require(const std::string &name) const
	{
		if (!IsAvailable(name))
		{
			DS_LOG_ERROR("Capability requirement failed: capability '{}' not available", name);
			throw CapabilityNotAvailableException(name);
		}
	}
} // namespace DefectStudio