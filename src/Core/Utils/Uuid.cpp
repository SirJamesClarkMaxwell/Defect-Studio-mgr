#include "Core/dspch.hpp"

#include "Core/Utils/Uuid.hpp"

#include <mutex>
#include <random>

namespace DefectStudio
{
	namespace
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
	} // namespace

	Uuid GenerateUuid()
	{
		UuidGeneratorState &state = GetUuidGeneratorState();
		std::lock_guard lock(state.mutex);
		return state.generator();
	}

	std::string ToString(const Uuid &id)
	{
		return uuids::to_string(id);
	}

	std::optional<Uuid> ParseUuid(const std::string &text)
	{
		return uuids::uuid::from_string(text);
	}
} // namespace DefectStudio
