#include "Core/dspch.hpp"

#include "Domain/DomainIds.hpp"

#include <mutex>
#include <random>

namespace DefectStudio
{

	struct UuidGeneratorState
	{
		std::random_device randomDevice;
		std::mt19937 engine{randomDevice()};
		uuids::uuid_random_generator generator{engine};
		std::mutex mutex;
	};

	UuidGeneratorState &GetUuidGeneratorState()
	{
		static UuidGeneratorState state;
		return state;
	}


	uuids::uuid GenerateDomainUuid()
	{
		UuidGeneratorState &state = GetUuidGeneratorState();
		std::lock_guard lock(state.mutex);
		return state.generator();
	}

	std::string ToString(const uuids::uuid &id)
	{
		return uuids::to_string(id);
	}
} // namespace DefectStudio
