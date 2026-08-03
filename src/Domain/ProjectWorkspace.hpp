#pragma once

#include <deque>
#include <string>

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
	};

	class StructureRegistry
	{
	public:
		using RecordList = std::deque<Ref<StructureRecord>>;

		[[nodiscard]] Ref<const StructureRecord> Add(
			CrystalStructure structure,
			Path sourcePath = {},
			std::string displayName = {});
		[[nodiscard]] WeakRef<const StructureRecord> Find(const StructureId &id) const;
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
