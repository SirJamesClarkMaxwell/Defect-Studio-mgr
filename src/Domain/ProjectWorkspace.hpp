#pragma once

#include <cstdint>
#include <string>
#include <deque>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	using StructureId = std::string;

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
		std::uint64_t m_NextId = 1;
	};

	class ProjectWorkspace
	{
	public:
		[[nodiscard]] StructureRegistry &Structures() noexcept;
		[[nodiscard]] const StructureRegistry &Structures() const noexcept;

	private:
		StructureRegistry m_Structures;
	};
} // namespace DefectStudio
