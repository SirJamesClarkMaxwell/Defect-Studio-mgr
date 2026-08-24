#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Electronic/ElectronicStructureModel.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	class JobSystem;
	class VaspOutputJob;
	class VaspOrbitalGridJob;

	// Shared model/controller behind ElectronicStructurePanel (controls) and
	// OccupationDiagramPanel (the plot, in its own window so it can fill it entirely) - both are
	// thin UI wrappers over one session so band/orbital state, caches and in-flight jobs stay in
	// sync regardless of which panel is docked/visible where. Owned once (EditorLayer), handed to
	// both panels by Ref<>.
	class ElectronicStructureSession
	{
	public:
		ElectronicStructureSession(RendererLayer &layer, WeakRef<JobSystem> jobSystem);

		// Per-window UI + cached-data state. Keyed by windowId; a window's calculation directory
		// never changes after creation, so there is nothing to invalidate on refocus.
		struct WindowState
		{
			Path calculationDirectory;
			std::optional<ElectronicStructureData> data;
			std::string lastError;

			// ponytail: hardcoded default instead of a derived HOMO-centered window - deriving it
			// needs a first wide probe load we don't have yet for this fixture. "Center on gap"
			// below re-centers once any load actually spans the occupied/unoccupied transition.
			int bandStart = 1022;
			int bandEnd = 1027;
			// How many bands above/below the detected gap-adjacent band "Center on gap" loads.
			int gapWindowMargin = 10;
			float localizationThreshold = 0.0f;
			bool splitSpinChannels = true;
			bool relativeToVbm = false;
			// Irrep (group-theory) labeling - opt-in, real per-band cost (get_symmetry per orbital),
			// see vasp_output_load.py's _orbitals_payload comment. irrepTol/symprec passed straight
			// to puntukas' get_orbital_data_for_two_spins.
			bool showIrreps = false;
			float irrepTol = 0.1f;
			float irrepSymprec = 1e-3f;

			int selectedBand = -1;
			// Shared default for orbitals not yet individually tuned - EnsureChannelRendered syncs
			// this to whatever the just-selected orbital's own remembered value is (see
			// isoValueByKey/ResolveIsoValue), so the slider always reflects the active orbital.
			float isoValue = 0.03f;
			// Per-(spin,band) iso value override, keyed by PackGridKey - lets different orbitals
			// keep their own visually-tuned isosurface threshold instead of sharing one global
			// value. Absent entries fall back to `isoValue` (see ResolveIsoValue).
			std::unordered_map<std::uint64_t, float> isoValueByKey;
			std::string gridError;
			std::string csvExportMessage;
			// Keyed by packGridKey(spinChannel, band) - every (spin, band) wavefunction grid ever
			// loaded for this window stays resident, so re-selecting an already-viewed band/channel
			// is an instant GPU re-mesh, not another ~2s WAVECAR read.
			std::unordered_map<std::uint64_t, OrbitalGridData> gridCache;
			// Keyed same as gridCache - set by pollGridJobs for ANY key whose fetch failed (not just
			// the two live GPU slots' active keys, unlike gridError above). Lets a caller like
			// TryGetOrDispatchGrid's batch-export consumer (ExportImagePanel) tell "still pending" (no
			// entry here, not cached yet) apart from "gave up" (entry here) instead of retrying a
			// permanently-broken band forever.
			std::unordered_map<std::uint64_t, std::string> gridFetchErrors;
			// The (spin, band) each GPU slot (0=up, 1=down) is currently showing - when that key's
			// job completes, the GPU mesh for that slot is regenerated. A pending key that isn't
			// either slot's active key is a silent background prefetch.
			std::array<std::optional<std::uint64_t>, 2> activeGridKeys;
			std::unordered_map<std::uint64_t, Ref<VaspOrbitalGridJob>> pendingGridJobs;
			std::unordered_map<std::uint64_t, JobId> pendingGridJobIds;

			Ref<VaspOutputJob> pendingOutputJob;
			JobId pendingOutputJobId = 0;
		};

		[[nodiscard]] RendererLayer &Layer() noexcept
		{
			return m_Layer;
		}

		// nullptr if no viewport window has ever been focused this session.
		[[nodiscard]] RendererWindowState *FindFocusedWindow();
		// Read-only lookup, no get-or-create/polling side effects - for EditorLayer's project-state
		// save, which just needs to read whatever's already there. nullptr if windowId was never
		// touched by Update() this session.
		[[nodiscard]] const WindowState *FindWindowState(const std::string &windowId) const;

		// Gets or creates windowState's session state, polls all its in-flight jobs, and (the
		// first time this window is seen) auto-dispatches the initial load. Idempotent - safe to
		// call once per frame from every panel that touches this window.
		WindowState &Update(RendererWindowState &windowState);

		// Instant preview from the window's own orbitals_export.csv (if present) merged with a
		// normal async VaspOutputJob dispatch, which confirms/refreshes the data shortly after
		// (and is the only source of the band-gap, which the CSV doesn't carry).
		void DispatchOutputLoad(WindowState &state);

		// Pristine/bulk calculation used as the VBM/CBM energy reference instead of a (possibly
		// defect-containing) calculation's own global HOMO/LUMO, which can land on a mid-gap
		// defect level rather than the true bulk band edges. One bulk reference for the whole
		// session (not per-window) - every open defect window shares the same pristine crystal,
		// so there is no reason to re-fetch it once per window.
		// Set directly (SetBulkDirectory + DispatchBulkLoad) from EditorLayer, either from the
		// active project's manifest.yaml (T07.5.1/T07.5.5) on open, or from ProjectTreePanel's
		// per-node RMB "Set as Bulk Reference" event - no in-panel free-text entry any more, see
		// ElectronicStructurePanel::renderBulkReferenceControls().
		[[nodiscard]] const Path &BulkDirectory() const noexcept
		{
			return m_BulkDirectory;
		}
		void SetBulkDirectory(Path directory) noexcept
		{
			m_BulkDirectory = std::move(directory);
		}
		[[nodiscard]] const std::optional<BandGapData> &BulkGap() const noexcept
		{
			return m_BulkGap;
		}
		[[nodiscard]] const std::string &BulkError() const noexcept
		{
			return m_BulkError;
		}
		[[nodiscard]] bool IsBulkLoading() const noexcept
		{
			return m_PendingBulkJob != nullptr;
		}
		// Minimal band range (orbitals irrelevant here) - just refreshes BulkGap()/BulkError().
		void DispatchBulkLoad();
		// Ensures GPU slot `slot` (0=up, 1=down) shows spin channel `spinChannel` of the currently
		// selected band - instant from cache, or dispatches the load. Also silently prefetches the
		// opposite spin channel so toggling the other slot on later is instant too.
		void EnsureChannelRendered(WindowState &state, RendererWindowState &windowState, int slot, int spinChannel);
		// Cached grid for (spinChannel, band) if already loaded; nullptr and a background fetch
		// dispatched otherwise (poll via the next Update()/pollGridJobs() pass - same machinery
		// EnsureChannelRendered uses). Unlike EnsureChannelRendered, doesn't touch any GPU slot -
		// for callers (ExportImagePanel) that decide separately when/where to render the result.
		[[nodiscard]] const OrbitalGridData *TryGetOrDispatchGrid(WindowState &state, int spinChannel, int band);
		// nullptr if key's fetch hasn't failed (still pending, not yet dispatched, or never asked
		// for) - see WindowState::gridFetchErrors.
		[[nodiscard]] static const std::string *GridFetchError(const WindowState &state, std::uint64_t key);
		void ExportOrbitalsCsv(WindowState &state);
		// Same columns/values as ExportOrbitalsCsv, tab-delimited to orbitals_export.tsv - T08.6.3.
		void ExportOrbitalsTsv(WindowState &state);
		// Matplotlib rendering of the occupation diagram (same colors/arrows as OccupationDiagramPanel,
		// plus irrep labels when state.showIrreps/data has them) - one-shot subprocess via
		// ScriptRunner (scripts/python/examples/electronic_structure_plot.py), not InteractiveProcess;
		// this is a single result, not a session. outputPath empty = default
		// (<calculationDirectory>/occupation_diagram.png, ElectronicStructurePanel's "Browse..." lets
		// the user pick a different one). Message reported the same way ExportOrbitalsCsv does.
		void ExportOccupationDiagramImage(WindowState &state, float vbm, const Path &outputPath = {});

		// Per-project custom irrep label overrides (irrep -> user's own text), pushed by EditorLayer
		// from the active project's manifest.yaml on project open/create (same trigger points as
		// SetBulkDirectory) - see ProjectManifest::irrepLabelOverrides. Shared session-wide, not
		// per-window (same reasoning as bulk directory - one project, one label scheme).
		void SetIrrepLabelOverrides(std::vector<std::pair<std::string, std::string>> overrides) noexcept
		{
			m_IrrepLabelOverrides = std::move(overrides);
		}
		[[nodiscard]] const std::vector<std::pair<std::string, std::string>> &IrrepLabelOverrides() const noexcept
		{
			return m_IrrepLabelOverrides;
		}
		// nullptr if `irrep` has no custom override (or is empty/nullopt).
		[[nodiscard]] const std::string *FindIrrepLabelOverride(const std::optional<std::string> &irrep) const;
		// Iso value for grid key `key` (spin+band) - state.isoValue if this orbital hasn't been
		// individually tuned yet, otherwise its own remembered value.
		[[nodiscard]] static float ResolveIsoValue(const WindowState &state, std::uint64_t key);
		// Warms gridCache for every band currently in `orbitals` (both spin channels) so clicking
		// through the whole table afterwards is instant instead of one ~2s WAVECAR read per row.
		// Does not enable/render anything itself - EnsureChannelRendered (row click / checkbox)
		// still decides what's actually shown, this just avoids the wait once it does.
		void PrefetchAllOrbitals(WindowState &state, const std::vector<OrbitalRecord> &orbitals);
		[[nodiscard]] static std::uint64_t PackGridKey(int spinChannel, int band);

	private:
		void pollOutputJob(WindowState &state);
		void pollBulkJob();
		void pollGridJobs(WindowState &state, RendererWindowState &windowState);
		void dispatchGridJob(WindowState &state, std::uint64_t key, int spinChannel, int band);
		// Minimal stand-in for real T07.5.1 persistence (see ProjectTreePanel's equivalent) - one
		// plain-text file with the last-used defaults, independent of the main (documented-unsafe-
		// to-extend) YamlConfigSerializer. loadPersistedDefaults() runs once at construction;
		// savePersistedDefaultsIfChanged() runs every Update() but only actually writes when the
		// settings differ from what's already on disk.
		void loadPersistedDefaults();
		void savePersistedDefaultsIfChanged();

		// Snapshot of the user-configurable fields of whichever window's session was last touched -
		// seeded into every newly-opened window's WindowState so band range/localization/margin/
		// iso value carry over instead of resetting to the hardcoded struct defaults each time.
		struct LastUsedSettings
		{
			int bandStart;
			int bandEnd;
			int gapWindowMargin;
			float localizationThreshold;
			bool splitSpinChannels;
			bool relativeToVbm;
			float isoValue;
			bool showIrreps;
			float irrepTol;
			float irrepSymprec;

			[[nodiscard]] bool operator==(const LastUsedSettings &) const = default;
		};
		std::optional<LastUsedSettings> m_LastUsedSettings;
		std::optional<LastUsedSettings> m_PersistedSettingsCache;
		Path m_PersistedBulkDirectoryCache;

		std::vector<std::pair<std::string, std::string>> m_IrrepLabelOverrides;

		Path m_BulkDirectory; // empty = unset; see the accessor comment above for how this gets set
		std::optional<BandGapData> m_BulkGap;
		std::string m_BulkError;
		Ref<VaspOutputJob> m_PendingBulkJob;
		JobId m_PendingBulkJobId = 0;
		// Auto-dispatched once (from the first window's Update()), not once per window - the bulk
		// reference is shared across every open defect, so there's nothing to redo per-window.
		bool m_BulkLoadAttempted = false;

		RendererLayer &m_Layer;
		WeakRef<JobSystem> m_JobSystem;
		ScriptRunner m_ScriptRunner; // ExportOccupationDiagramImage only - one-shot, not a session.
		std::unordered_map<std::string, WindowState> m_WindowStates;
	};
} // namespace DefectStudio
