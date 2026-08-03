#pragma once

#include <deque>
#include <string>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Defects/DefectModel.hpp"
#include "Domain/DomainIds.hpp"

namespace DefectStudio
{
	struct StructureRecord
	{
		StructureId id;
		CrystalStructure structure;
		Path sourcePath;
		std::string displayName;
	};

	class StructureRegistry
	{
	public:
		[[nodiscard]] const StructureRecord &Add(
			CrystalStructure structure,
			Path sourcePath = {},
			std::string displayName = {});
		[[nodiscard]] const StructureRecord *Find(const StructureId &id) const;
		[[nodiscard]] const std::deque<StructureRecord> &Records() const noexcept;

	private:
		std::deque<StructureRecord> m_Records;
	};

	struct DefectConceptRecord
	{
		DefectId id;
		DefectConcept data;
	};

	class DefectRegistry
	{
	public:
		[[nodiscard]] const DefectConceptRecord &Add(DefectConcept data);
		[[nodiscard]] const DefectConceptRecord *Find(const DefectId &id) const;
		[[nodiscard]] const std::deque<DefectConceptRecord> &Records() const noexcept;

	private:
		std::deque<DefectConceptRecord> m_Records;
	};

	struct DefectConfigurationRecord
	{
		DefectConfigurationId id;
		DefectConfiguration configuration;
	};

	class DefectConfigurationRegistry
	{
	public:
		[[nodiscard]] const DefectConfigurationRecord &Add(DefectConfiguration configuration);
		[[nodiscard]] const DefectConfigurationRecord *Find(const DefectConfigurationId &id) const;
		[[nodiscard]] const std::deque<DefectConfigurationRecord> &Records() const noexcept;

	private:
		std::deque<DefectConfigurationRecord> m_Records;
	};

	class CalculationRegistry
	{
	public:
		[[nodiscard]] const CalculationRecord &Add(CalculationRecord calculation);
		[[nodiscard]] const CalculationRecord *Find(const CalculationRecordId &id) const;
		[[nodiscard]] const std::deque<CalculationRecord> &Records() const noexcept;

	private:
		std::deque<CalculationRecord> m_Records;
	};

	class ProjectWorkspace
	{
	public:
		[[nodiscard]] StructureRegistry &Structures() noexcept;
		[[nodiscard]] const StructureRegistry &Structures() const noexcept;
		[[nodiscard]] DefectRegistry &Defects() noexcept;
		[[nodiscard]] const DefectRegistry &Defects() const noexcept;
		[[nodiscard]] DefectConfigurationRegistry &DefectConfigurations() noexcept;
		[[nodiscard]] const DefectConfigurationRegistry &DefectConfigurations() const noexcept;
		[[nodiscard]] CalculationRegistry &Calculations() noexcept;
		[[nodiscard]] const CalculationRegistry &Calculations() const noexcept;

	private:
		StructureRegistry m_Structures;
		DefectRegistry m_Defects;
		DefectConfigurationRegistry m_DefectConfigurations;
		CalculationRegistry m_Calculations;
	};
} // namespace DefectStudio
