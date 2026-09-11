#pragma once

#include <array>
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
		// The EXPANDED conventional cell - `lattice (x) basis`, i.e. what reaches POSCAR.
		CrystalStructure draftStructure;
		// The motif alone, without the centering expansion. The basis pane renders this, and the
		// session is the only thing the creation tabs panel can read.
		CrystalStructure motifStructure;
		std::string displayName;
		Path targetDirectory;
		// Renderer window keys of the three preview panes; no StructureId, by design.
		std::vector<std::string> previewWindowIds;
		// Which of the three panes (basis, unit cell, supercell) are shown. Toggled from the
		// NewStructure panel; a hidden pane keeps its renderer window and is purely a layout
		// decision - destroying windows on toggle is what used to crash ImGui's DockBuilder.
		std::array<bool, 3> previewVisible{true, true, true};
		// Splitter proportions and active pane of the three-pane creation window. UI layout state
		// in an App-layer type, deliberately: it is per-session view state with no domain meaning,
		// and the session is the only place the panes can persist it across frames.
		float leftFraction = 0.5f;
		float bottomFraction = 0.4f;
		int activePaneIndex = 1;
		// Primitive cell of a centred lattice, drawn inside the conventional cell box. Set by the
		// NewStructure panel - the chosen centering is only known there.
		std::optional<glm::mat3> primitiveCellOverlay;
		// h x k x l of the supercell shown in the third preview window, part of the tab title, and -
		// via BuildSessionExportStructure below - what Add to Project actually writes.
		glm::ivec3 supercellCounts{1, 1, 1};
		// Write a POTCAR next to the POSCAR once the structure is registered. Needs
		// ui.pseudopotential_dir; a missing one is reported as a notification, never a silent skip.
		bool exportPotcar = false;
		bool dirty = false;
		CreationSessionState state = CreationSessionState::Draft;
		std::optional<StructuredError> lastError; // Preserved across failures so the user can retry
		// Invariant: set iff state is Submitted or Completing. At most one attempt is ever in flight
		// per session - the coordinator rejects a second request instead of racing two writes.
		std::optional<Uuid> activeAttemptId;
		Time::TimePoint createdAt{};
		Time::TimePoint lastModifiedAt{};
	};

	// What the session commits: the conventional cell repeated supercellCounts times. draftStructure
	// stays the UNIT cell because the middle preview pane renders it, so the expansion has to happen
	// where the whole structure is needed - in the supercell pane and in Add to Project alike, which
	// is why this lives here instead of in either of them.
	[[nodiscard]] CrystalStructure BuildSessionExportStructure(const CreationSession &session);

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
