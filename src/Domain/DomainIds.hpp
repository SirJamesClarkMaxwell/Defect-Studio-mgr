#pragma once

#include <string>

#include <uuid.h>

namespace DefectStudio
{
	using StructureId = uuids::uuid;
	using DefectId = uuids::uuid;
	using DefectConfigurationId = uuids::uuid;
	using CalculationRecordId = uuids::uuid;

	[[nodiscard]] uuids::uuid GenerateDomainUuid();
	[[nodiscard]] std::string ToString(const uuids::uuid &id);
} // namespace DefectStudio
