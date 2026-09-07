#pragma once

#include <filesystem>
#include <string>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Domain/DomainIds.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Common/StructuredError.hpp"

namespace DefectStudio::DomainEvents
{
	struct ProjectTreeSelectionChanged final : public BusEvent
	{
		std::filesystem::path selectedPath;
		enum class Kind { File, Directory, ProjectRoot } kind = Kind::ProjectRoot;
		std::filesystem::path resolvedTargetDirectory;
	};

	struct AddStructureToProjectRequested final : public BusEvent
	{
		CrystalStructure structure;
		std::string displayName;
		std::filesystem::path targetDirectory;
	};

	struct ProjectStructureAdded final : public BusEvent
	{
		StructureId newStructureId;
		std::filesystem::path poscarPath;
	};

	struct ProjectStructureAddFailed final : public BusEvent
	{
		StructuredError error;
	};
} // namespace DefectStudio::DomainEvents
