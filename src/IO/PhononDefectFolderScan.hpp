#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "IO/AnalysisDatasetIO.hpp"
#include "IO/PhononServerResultsIO.hpp"

namespace DefectStudio
{
	// task/113e: "Dodaj defekt z folderu..." for the Phonons module, like the energetics module does for
	// formation energies: scan a folder, propose one case per defect system, the user confirms the runs.
	enum class PhononFolderRole
	{
		ForceConstants,   // dephonopy_disp.yaml, phonopy_disp.yaml, FORCE_SETS or FORCE_CONSTANTS
		GroundState,      // relaxed run: OUTCAR + CONTCAR, name relax / ground / gs / *ground_state*/singlet...
		ExcitedState,     // relaxed run under excited / exc* / es, not triplet
		Triplet,          // relaxed run whose name contains triplet / tryplet
		GroundAtExcited,  // single point: ground-state run whose name contains excited_geometry / gs@es / gs_at_es
	};

	struct PhononFolderCandidate
	{
		PhononFolderRole role;
		Path directory;
	};

	struct PhononFolderProposal
	{
		Path systemDirectory;  // lowest common ancestor of the proposed GS and ES runs
		std::string name;      // systemDirectory relative to the scanned root ('/' separators); root's own name if equal
		std::vector<PhononFolderCandidate> candidates; // every candidate below systemDirectory, any role, sorted by path
		// Proposed pick per role (empty = none found). The user confirms or changes it in the UI.
		Path forceConstantDirectory, groundStateDirectory, excitedStateDirectory, tripletDirectory, groundAtExcitedDirectory;
		std::vector<ServerHrResult> serverResults; // FindServerHrResults(systemDirectory)
		std::vector<AnalysisDatasetRef> datasets;  // FindDatasets(systemDirectory)
	};

	// Rules (binding, tested):
	// - A "run" is a directory holding OUTCAR and CONTCAR. Directories named .git, .venv, __pycache__,
	//   defectstudio, embedded, spectral-function, lineshape, run_computations and disp-<digits> are not descended into.
	// - Role from the run's path below the scanned root, case-insensitive, checked on the run's own name first and
	//   then on its parent's name: triplet/tryplet -> Triplet; excited_geometry / gs_at_es / gs@es -> GroundAtExcited;
	//   relax / ground / gs -> GroundState; excited / exc / es (as a whole word or a prefix like "exc_ms") -> ExcitedState.
	//   A run matching nothing is not a candidate. A directory holding a force-constant file is a ForceConstants
	//   candidate (whatever its name).
	// - One proposal per system. A system directory has both a GroundState and an ExcitedState candidate below it
	//   (directly or deeper) while none of its subdirectories has both (so a root holding systems a/ and b/ gives
	//   a and b, not the root). Candidates of other roles join the system they lie under; candidates under no
	//   system are dropped. A root with no GS+ES pair gives no proposal.
	// - Proposed pick per role = the candidate with the shortest path below the system, ties by name; for
	//   ExcitedState, names containing "singlet" win over others at the same depth.
	// - Sorted by systemDirectory. Returns what was found so far when cancelled() becomes true.
	[[nodiscard]] std::vector<PhononFolderProposal> ScanPhononDefectFolder(const Path &root, const std::function<bool()> &cancelled);
} // namespace DefectStudio
