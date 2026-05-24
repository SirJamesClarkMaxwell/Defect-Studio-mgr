#pragma once

#include <string>
#include <vector>

#include "Core/Capabilities/Capability.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CapabilityRegistry;

	class CapabilityService
	{
	public:
		CapabilityService();
		~CapabilityService();

		void RegisterCapability(CapabilityEntry capability);
		void LockAfterStartup();
		[[nodiscard]] bool IsLocked() const;
		[[nodiscard]] std::vector<CapabilityEntry> GetAll() const;

		[[nodiscard]] bool IsAvailable(const std::string &name) const;
		[[nodiscard]] Result<void> Require(const std::string &name) const;

	private:
		Unique<CapabilityRegistry> m_Registry;
	};
} // namespace DefectStudio
