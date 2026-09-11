#pragma once

#include <deque>
#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
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
		int revision = 0;         // dirty flag: bumped on every mutation, compared to savedRevision
		int savedRevision = 0;    // revision at last save time
		bool exportPotcar = false; // export POTCAR file on save (if pseudodir configured)
	};

	class StructureRegistry
	{
	public:
		using RecordList = std::deque<Ref<StructureRecord>>;

		[[nodiscard]] Ref<const StructureRecord> Add(
			CrystalStructure structure,
			Path sourcePath = {},
			std::string displayName = {});

		// Lifecycle operations - these are the ONLY way to set sourcePath going forward.
		// Result-based, not a bare reference: the caller (StructureLifecycleCoordinator) has already
		// committed a directory to disk by the time it calls this, so it needs to know whether the
		// registration actually took, in order to clean that directory up when it did not.
		// Fails on: an empty sourcePath, a structure that fails ValidateStructureForPersistence, or a
		// sourcePath already registered by another record.
		[[nodiscard]] Result<StructureId> RegisterAsProjectMember(
			CrystalStructure structure,
			Path sourcePath,
			std::string displayName);

		// Drops a record. Currently used for exactly one case: a record whose file has been deleted
		// from the Project Tree, which would otherwise block re-adding the structure under the same
		// path for the rest of the session. Returns false for an unknown id.
		bool Remove(const StructureId &id);

		// Update the source path of an existing structure (e.g., if file moved)
		bool UpdateSourcePath(const StructureId &id, Path newPath);

		// Update the saved revision (e.g., after successful save)
		bool UpdateSavedRevision(const StructureId &id, int newRevision);

		[[nodiscard]] WeakRef<const StructureRecord> Find(const StructureId &id) const;
		// Non-const counterpart to Find - for in-place edits of an already-registered structure
		// (atom add/delete/duplicate/change-type), which Add-only/const-Find can't support.
		[[nodiscard]] WeakRef<StructureRecord> FindMutable(const StructureId &id) noexcept;
		[[nodiscard]] const RecordList &Records() const noexcept;

	private:
		RecordList m_Records;
	};

	struct DefectConceptRecord
	{
		DefectId id;
		DefectConcept data;
	};

	class DefectRegistry
	{
	public:
		using RecordList = std::deque<Ref<DefectConceptRecord>>;

		[[nodiscard]] Ref<const DefectConceptRecord> Add(DefectConcept data);
		[[nodiscard]] WeakRef<const DefectConceptRecord> Find(const DefectId &id) const;
		[[nodiscard]] const RecordList &Records() const noexcept;

	private:
		RecordList m_Records;
	};

	struct DefectConfigurationRecord
	{
		DefectConfigurationId id;
		DefectConfiguration configuration;
	};

	class DefectConfigurationRegistry
	{
	public:
		using RecordList = std::deque<Ref<DefectConfigurationRecord>>;

		[[nodiscard]] Ref<const DefectConfigurationRecord> Add(DefectConfiguration configuration);
		[[nodiscard]] WeakRef<const DefectConfigurationRecord> Find(const DefectConfigurationId &id) const;
		[[nodiscard]] const RecordList &Records() const noexcept;

	private:
		RecordList m_Records;
	};

	class CalculationRegistry
	{
	public:
		using RecordList = std::deque<Ref<CalculationRecord>>;

		[[nodiscard]] Ref<const CalculationRecord> Add(CalculationRecord calculation);
		[[nodiscard]] WeakRef<const CalculationRecord> Find(const CalculationRecordId &id) const;
		[[nodiscard]] const RecordList &Records() const noexcept;

	private:
		RecordList m_Records;
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
