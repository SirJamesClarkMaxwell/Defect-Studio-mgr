#include "Core/dspch.hpp"

#include "Domain/ProjectWorkspace.hpp"

#include <utility>

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/StructureValidation.hpp"

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

	Result<StructureId> StructureRegistry::RegisterAsProjectMember(
		CrystalStructure structure,
		Path sourcePath,
		std::string displayName)
	{
		if (sourcePath.Empty())
			return StructuredError(
				ErrorCategory::Validation,
				Severity::Error,
				"Cannot register a project structure without a file",
				"RegisterAsProjectMember called with an empty sourcePath",
				"This is an internal error; report it with the log.",
				"StructureRegistry::RegisterAsProjectMember");

		if (Result<void> valid = ValidateStructureForPersistence(structure); !valid)
			return valid.Error();

		// Note this cannot tell a real duplicate from a record left dangling by a deleted folder:
		// by the time it runs, the caller has already written a file at exactly this path. Dangling
		// records are dropped earlier, by StructureLifecycleCoordinator's pre-flight, which runs
		// before anything is written and can still see that the file was missing.
		for (const Ref<StructureRecord> &existing : m_Records)
		{
			if (existing == nullptr || existing->sourcePath.Empty() || existing->sourcePath != sourcePath)
				continue;

			return StructuredError(
				ErrorCategory::Validation,
				Severity::Error,
				"A structure from this file is already in the project",
				"Duplicate sourcePath: " + sourcePath.String(),
				"Open the existing structure, or add this one under a different name.",
				"StructureRegistry::RegisterAsProjectMember");
		}

		Ref<StructureRecord> record = CreateRef<StructureRecord>();
		record->id = GenerateUuid();
		record->displayName = displayName.empty() ? structure.name : std::move(displayName);
		record->sourcePath = std::move(sourcePath);
		record->structure = std::move(structure);
		// For project members, savedRevision is initialized to 0 (dirty from start until saved)
		record->revision = 0;
		record->savedRevision = 0;
		m_Records.push_back(record);
		return record->id;
	}

	bool StructureRegistry::Remove(const StructureId &id)
	{
		for (auto it = m_Records.begin(); it != m_Records.end(); ++it)
		{
			if (*it != nullptr && (*it)->id == id)
			{
				m_Records.erase(it);
				return true;
			}
		}
		return false;
	}

	bool StructureRegistry::UpdateSourcePath(const StructureId &id, Path newPath)
	{
		for (Ref<StructureRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
			{
				record->sourcePath = std::move(newPath);
				return true;
			}
		}
		return false;
	}

	bool StructureRegistry::UpdateSavedRevision(const StructureId &id, int newRevision)
	{
		for (Ref<StructureRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
			{
				record->savedRevision = newRevision;
				return true;
			}
		}
		return false;
	}

	bool StructureRegistry::MarkModified(const StructureId &id)
	{
		for (const Ref<StructureRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
			{
				++record->revision;
				return true;
			}
		}
		return false;
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

	WeakRef<StructureRecord> StructureRegistry::FindMutable(const StructureId &id) noexcept
	{
		for (const Ref<StructureRecord> &record : m_Records)
		{
			if (record != nullptr && record->id == id)
				return CreateWeakRef(record);
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
