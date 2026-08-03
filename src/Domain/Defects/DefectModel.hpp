#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainIds.hpp"

namespace DefectStudio
{
	enum class PointDefectType
	{
		Vacancy,
		Interstitial,
		Antisite,
		SubstitutionalDopant,
		None
	};

	struct DefectConcept
	{
		std::string displayName;
		PointDefectType type = PointDefectType::None;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 fractional = glm::vec3(0.0f);
		std::vector<std::string> tags;
		std::vector<int> chargeStates;
	};

	struct PointDefectOperation
	{
		PointDefectType type = PointDefectType::None;
		std::size_t atomIndex = 0;
		AtomSite atom;
		std::string replacementSpecies;
		std::string label;
	};

	struct DefectConfiguration
	{
		DefectId defectId;
		StructureId pristineStructureId;
		std::string displayName;
		std::vector<PointDefectOperation> operations;
		std::optional<int> chargeState;
	};

	struct CalculationRecord
	{
		CalculationRecordId id;
		StructureId inputStructureId;
		std::optional<StructureId> outputStructureId;
		std::optional<DefectConfigurationId> defectConfigurationId;
		std::string displayName;
		std::string method;
	};

	[[nodiscard]] Result<CrystalStructure> BuildDefectedStructure(
		const CrystalStructure &pristine,
		const DefectConfiguration &configuration);
} // namespace DefectStudio
