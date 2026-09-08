#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	enum class CreationMode
	{
		FromTemplate,
		FromScratch,
		AnalyzeExisting,
		ImportFile
	};

	[[nodiscard]] const char *ToString(CreationMode mode) noexcept;

	enum class CreationSessionState
	{
		Draft,      // Being edited in NewStructure, not handed to StructureHub yet
		Ready,      // Handed over: renderer tab open, no Add-to-Project attempt yet
		Submitted,  // An attempt is in flight (activeAttemptId set)
		Completing, // Job finished, coordinator is registering in domain (activeAttemptId still set)
		Success,
		Failed,
		Closing     // Tab closed; waiting for any in-flight attempt before the session is removed
	};

	[[nodiscard]] const char *ToString(CreationSessionState state) noexcept;

	// One in-progress structure creation. Ephemeral: lives only in CreationSessionRegistry (App),
	// never in the domain. Registration into the domain is what ENDS a session, not what backs it.
	struct CreationSession
	{
		Uuid sessionId;
		CreationMode mode = CreationMode::FromTemplate;
		CrystalStructure draftStructure;
		std::string displayName;
		Path targetDirectory;
		// Renderer window keys of the ephemeral 2+1 preview tab; no StructureId, by design.
		std::vector<std::string> previewWindowIds;
		// h x k x l of the supercell shown in the second preview window, and part of the tab title.
		glm::ivec3 supercellCounts{1, 1, 1};
		bool dirty = false;
		CreationSessionState state = CreationSessionState::Draft;
		std::optional<StructuredError> lastError; // Preserved across failures so the user can retry
		// Invariant: set iff state is Submitted or Completing. At most one attempt is ever in flight
		// per session - the coordinator rejects a second request instead of racing two writes.
		std::optional<Uuid> activeAttemptId;
		Time::TimePoint createdAt{};
		Time::TimePoint lastModifiedAt{};
	};

	// Single source of truth for active creation sessions, owned by the composition root. NewStructure
	// and StructureHub both read through this instead of holding their own copies of the draft.
	// Main thread only.
	class CreationSessionRegistry
	{
	public:
		using SessionList = std::vector<Ref<CreationSession>>;

		[[nodiscard]] Ref<CreationSession> Create(CreationMode mode);
		[[nodiscard]] WeakRef<CreationSession> Find(const Uuid &sessionId) const;
		// Idempotent: removing an unknown sessionId is a no-op returning false, never an error. Close
		// events arrive from three independent paths (tab X, NewStructure panel close, settings
		// auto-close after a successful add), so duplicates are expected traffic, not a bug.
		bool Remove(const Uuid &sessionId);
		[[nodiscard]] const SessionList &Sessions() const noexcept;

	private:
		SessionList m_Sessions;
	};
} // namespace DefectStudio
