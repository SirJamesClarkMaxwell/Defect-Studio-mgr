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
		record.id = "structure-" + std::to_string(m_NextId++);
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

	StructureRegistry &ProjectWorkspace::Structures() noexcept
	{
		return m_Structures;
	}

	const StructureRegistry &ProjectWorkspace::Structures() const noexcept
	{
		return m_Structures;
	}
} // namespace DefectStudio
