#include "Core/dspch.hpp"

#include "Domain/ProjectWorkspace.hpp"

#include <utility>

namespace DefectStudio
{
	const StructureRecord &StructureRegistry::Add(
		CrystalStructure structure,
		Path sourcePath,
		std::string displayName)
	{
		StructureRecord record;
		record.id = GenerateDomainUuid();
		record.displayName = displayName.empty() ? structure.name : std::move(displayName);
		record.sourcePath = std::move(sourcePath);
		record.structure = std::move(structure);
		m_Records.push_back(std::move(record));
		return m_Records.back();
	}

	const StructureRecord *StructureRegistry::Find(const StructureId &id) const
	{
		for (const StructureRecord &record : m_Records)
		{
			if (record.id == id)
				return &record;
		}
		return nullptr;
	}

	const std::deque<StructureRecord> &StructureRegistry::Records() const noexcept
	{
		return m_Records;
	}

	const DefectConceptRecord &DefectRegistry::Add(DefectConcept data)
	{
		DefectConceptRecord record;
		record.id = GenerateDomainUuid();
		record.data = std::move(data);
		m_Records.push_back(std::move(record));
		return m_Records.back();
	}

	const DefectConceptRecord *DefectRegistry::Find(const DefectId &id) const
	{
		for (const DefectConceptRecord &record : m_Records)
		{
			if (record.id == id)
				return &record;
		}
		return nullptr;
	}

	const std::deque<DefectConceptRecord> &DefectRegistry::Records() const noexcept
	{
		return m_Records;
	}

	const DefectConfigurationRecord &DefectConfigurationRegistry::Add(DefectConfiguration configuration)
	{
		DefectConfigurationRecord record;
		record.id = GenerateDomainUuid();
		record.configuration = std::move(configuration);
		m_Records.push_back(std::move(record));
		return m_Records.back();
	}

	const DefectConfigurationRecord *DefectConfigurationRegistry::Find(const DefectConfigurationId &id) const
	{
		for (const DefectConfigurationRecord &record : m_Records)
		{
			if (record.id == id)
				return &record;
		}
		return nullptr;
	}

	const std::deque<DefectConfigurationRecord> &DefectConfigurationRegistry::Records() const noexcept
	{
		return m_Records;
	}

	const CalculationRecord &CalculationRegistry::Add(CalculationRecord calculation)
	{
		if (calculation.id.is_nil())
			calculation.id = GenerateDomainUuid();
		m_Records.push_back(std::move(calculation));
		return m_Records.back();
	}

	const CalculationRecord *CalculationRegistry::Find(const CalculationRecordId &id) const
	{
		for (const CalculationRecord &record : m_Records)
		{
			if (record.id == id)
				return &record;
		}
		return nullptr;
	}

	const std::deque<CalculationRecord> &CalculationRegistry::Records() const noexcept
	{
		return m_Records;
	}

	StructureRegistry &ProjectWorkspace::Structures() noexcept
	{
		return m_Structures;
	}

	const StructureRegistry &ProjectWorkspace::Structures() const noexcept
	{
		return m_Structures;
	}

	DefectRegistry &ProjectWorkspace::Defects() noexcept
	{
		return m_Defects;
	}

	const DefectRegistry &ProjectWorkspace::Defects() const noexcept
	{
		return m_Defects;
	}

	DefectConfigurationRegistry &ProjectWorkspace::DefectConfigurations() noexcept
	{
		return m_DefectConfigurations;
	}

	const DefectConfigurationRegistry &ProjectWorkspace::DefectConfigurations() const noexcept
	{
		return m_DefectConfigurations;
	}

	CalculationRegistry &ProjectWorkspace::Calculations() noexcept
	{
		return m_Calculations;
	}

	const CalculationRegistry &ProjectWorkspace::Calculations() const noexcept
	{
		return m_Calculations;
	}
} // namespace DefectStudio
