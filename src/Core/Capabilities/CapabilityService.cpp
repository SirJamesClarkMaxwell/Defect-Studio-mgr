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

	Result<void> CapabilityService::Require(const std::string &name) const
	{
		if (!IsAvailable(name))
		{
			DS_LOG_ERROR("CapabilityService: required capability '{}' is not available", name);
			return StructuredError{
				ErrorCategory::Capability,
				Severity::Error,
				"Required capability is not available.",
				"Capability '" + name + "' was required but is not registered or is disabled.",
				"Check capability registration order and ensure LockAfterStartup() was called.",
				"CapabilityService",
				"capability.require.unavailable"};
		}

		return {};
	}
} // namespace DefectStudio
