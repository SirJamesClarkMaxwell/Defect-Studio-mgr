#include "Core/dspch.hpp"

#include "Domain/ProjectWorkspace.hpp"

#include <utility>

namespace DefectStudio
{
	Ref<const StructureRecord> StructureRegistry::Add(
		CrystalStructure structure,
		Path sourcePath,
		std::string displayName)
	{
		Ref<StructureRecord> record = CreateRef<StructureRecord>();
		record->id = GenerateUuid();
		record->displayName = displayName.empty() ? structure.name : std::move(displayName);
		record->sourcePath = std::move(sourcePath);
		record->structure = std::move(structure);
		m_Records.push_back(record);
		return record;
	}

	WeakRef<const StructureRecord> StructureRegistry::Find(const StructureId &id) const
	{
		for (const Ref<StructureRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
			{
				Ref<const StructureRecord> constRecord = record;
				return CreateWeakRef(constRecord);
			}
		}
		return {};
	}

	const StructureRegistry::RecordList &StructureRegistry::Records() const noexcept
	{
		return m_Records;
	}

	Ref<const DefectConceptRecord> DefectRegistry::Add(DefectConcept data)
	{
		Ref<DefectConceptRecord> record = CreateRef<DefectConceptRecord>();
		record->id = GenerateUuid();
		record->data = std::move(data);
		m_Records.push_back(record);
		return record;
	}

	WeakRef<const DefectConceptRecord> DefectRegistry::Find(const DefectId &id) const
	{
		for (const Ref<DefectConceptRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
			{
				Ref<const DefectConceptRecord> constRecord = record;
				return CreateWeakRef(constRecord);
			}
		}
		return {};
	}

	const DefectRegistry::RecordList &DefectRegistry::Records() const noexcept
	{
		return m_Records;
	}

	Ref<const DefectConfigurationRecord> DefectConfigurationRegistry::Add(DefectConfiguration configuration)
	{
		Ref<DefectConfigurationRecord> record = CreateRef<DefectConfigurationRecord>();
		record->id = GenerateUuid();
		record->configuration = std::move(configuration);
		m_Records.push_back(record);
		return record;
	}

	WeakRef<const DefectConfigurationRecord> DefectConfigurationRegistry::Find(const DefectConfigurationId &id) const
	{
		for (const Ref<DefectConfigurationRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
			{
				Ref<const DefectConfigurationRecord> constRecord = record;
				return CreateWeakRef(constRecord);
			}
		}
		return {};
	}

	const DefectConfigurationRegistry::RecordList &DefectConfigurationRegistry::Records() const noexcept
	{
		return m_Records;
	}

	Ref<const CalculationRecord> CalculationRegistry::Add(CalculationRecord calculation)
	{
		if (calculation.id.is_nil())
			calculation.id = GenerateUuid();
		Ref<CalculationRecord> record = CreateRef<CalculationRecord>(std::move(calculation));
		m_Records.push_back(record);
		return record;
	}

	WeakRef<const CalculationRecord> CalculationRegistry::Find(const CalculationRecordId &id) const
	{
		for (const Ref<CalculationRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
			{
				Ref<const CalculationRecord> constRecord = record;
				return CreateWeakRef(constRecord);
			}
		}
		return {};
	}

	const CalculationRegistry::RecordList &CalculationRegistry::Records() const noexcept
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
