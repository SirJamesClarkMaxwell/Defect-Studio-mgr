#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace DefectStudio
{
	enum class CapabilityCategory
	{
		BuildTime,
		RuntimeDetected,
		Policy
	};

	struct CapabilityEntry
	{
		std::string name;
		CapabilityCategory category = CapabilityCategory::RuntimeDetected;
		bool available = false;
		std::string description;
	};

	class CapabilityNotAvailableException final : public std::runtime_error
	{
	public:
		explicit CapabilityNotAvailableException(std::string capability)
			: std::runtime_error("Capability not available: " + capability), m_Capability(std::move(capability))
		{
		}

		[[nodiscard]] const std::string &GetCapability() const noexcept
		{
			return m_Capability;
		}

	private:
		std::string m_Capability;
	};
} // namespace DefectStudio