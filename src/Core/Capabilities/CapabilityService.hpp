#pragma once

#include <string>

#include "Core/Capabilities/CapabilityRegistry.hpp"

namespace DefectStudio
{
	class CapabilityService
	{
	public:
		explicit CapabilityService(CapabilityRegistry &registry);

		[[nodiscard]] bool IsAvailable(const std::string &name) const;
		void Require(const std::string &name) const;

	private:
		CapabilityRegistry &m_Registry;
	};
} // namespace DefectStudio