#pragma once

#include <optional>
#include <string>

#include <uuid.h>

namespace DefectStudio
{
	using Uuid = uuids::uuid;

	[[nodiscard]] Uuid GenerateUuid();
	[[nodiscard]] std::string ToString(const Uuid &id);
	[[nodiscard]] std::optional<Uuid> ParseUuid(const std::string &text);
} // namespace DefectStudio
