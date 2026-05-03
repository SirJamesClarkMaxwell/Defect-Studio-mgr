#pragma once

#include <string>

#include "Core/Capabilities/CapabilityRegistry.hpp"
#include "Core/Diagnostics/StructuredError.hpp"

namespace DefectStudio
{
	class CapabilityService
	{
	public:
		explicit CapabilityService(CapabilityRegistry &registry);

		[[nodiscard]] bool IsAvailable(const std::string &name) const;
		[[nodiscard]] Result<void> Require(const std::string &name) const;

	private:
		CapabilityRegistry &m_Registry;
	};
} // namespace DefectStudio
