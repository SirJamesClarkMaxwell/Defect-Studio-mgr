#pragma once

#include <filesystem>
#include <string>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/DomainIds.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Core/Diagnostics/StructuredError.hpp"

namespace DefectStudio::DomainEvents
{
	struct ProjectTreeSelectionChanged final : public BusEvent
	{
		std::filesystem::path selectedPath;
		enum class Kind { File, Directory, ProjectRoot } kind = Kind::ProjectRoot;
		std::filesystem::path resolvedTargetDirectory;
	};

	// A creation session was registered in CreationSessionRegistry (NewStructure mode selection).
	struct CreationSessionCreated final : public BusEvent
	{
		Uuid sessionId;
	};

	// "Move to Structure Hub": the draft is handed over, the ephemeral 2+1 renderer tab opens, and
	// the session captures the ProjectTree folder it was launched against. No attempt yet.
	struct SessionReadyForStructureHub final : public BusEvent
	{
		Uuid sessionId;
		std::filesystem::path targetDirectory;
	};

	// User intent only. attemptId is deliberately absent: attempt identity is coordinator-owned, and
	// is assigned in the same main-thread step that enforces the single-in-flight-attempt constraint.
	struct AddStructureToProjectRequested final : public BusEvent
	{
		Uuid sessionId;
		CrystalStructure structure;
		std::string displayName;
		std::filesystem::path targetDirectory;
	};

	// Published only after Domain registration succeeded - never merely after the file was written.
	struct ProjectStructureAdded final : public BusEvent
	{
		Uuid sessionId;
		Uuid attemptId;
		StructureId newStructureId;
		std::filesystem::path poscarPath;
	};

	struct ProjectStructureAddFailed final : public BusEvent
	{
		Uuid sessionId;
		Uuid attemptId;
		StructuredError error;
	};

	// The single close event for a session, published by all three close paths: the renderer tab's X,
	// the NewStructure panel closing, and the settings-driven auto-close after a successful add.
	// Handling is idempotent.
	struct RendererTabClosed final : public BusEvent
	{
		Uuid sessionId;
	};
} // namespace DefectStudio::DomainEvents
