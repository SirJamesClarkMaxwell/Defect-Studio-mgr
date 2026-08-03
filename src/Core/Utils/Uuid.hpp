#pragma once

#include <string>

#include <uuid.h>

namespace DefectStudio
{
	using Uuid = uuids::uuid;

	[[nodiscard]] Uuid GenerateUuid();
	[[nodiscard]] std::string ToString(const Uuid &id);
} // namespace DefectStudio
