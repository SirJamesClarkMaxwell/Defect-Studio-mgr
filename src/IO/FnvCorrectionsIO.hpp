#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"
#include "Domain/Defects/FnvCorrection.hpp"

namespace DefectStudio
{
	// One FNV correction as the FNV Correction panel left it: the inputs, the chosen G = 0
	// convention, the axes in use and - once "Oblicz profile" has run - the profiles from Python
	// plus the plateau window per axis. With the profiles saved, reopening the project shows the
	// result straight away instead of re-reading two LOCPOTs for a minute. The alignments are not
	// saved: they are recomputed from the profiles and the windows (AlignFnvInWindow), so the file
	// can never disagree with itself.
	struct PersistedFnvCorrection
	{
		Path defectLocpot; // absolute in memory; project-relative in the file when inside the project
		Path bulkLocpot;
		int charge = 0;
		double dielectric = 5.7;
		std::string dielectricSource;
		glm::dvec3 centreFractional = glm::dvec3(0.0);
		double energyCutoff = 0.0; // eV, 0 = from INCAR
		FnvModelConvention convention = FnvModelConvention::Sxdefectalign;
		std::array<bool, 3> useAxis{true, true, true};
		// Present only after a computation. `axes[a].position` is not stored: it is rebuilt as
		// k * length / N on load, which is exactly what the bridge produced.
		std::optional<FnvProfiles> profiles;
		// The plateau window per axis, as last shown (proposed or dragged). Read only with `profiles`.
		std::array<FnvPlateauWindow, 3> windows{};
	};

	struct FnvCorrectionsFile
	{
		int formatVersion = 1;
		// One entry per defect LOCPOT, so every defect of a project keeps its correction.
		std::vector<PersistedFnvCorrection> corrections;
		// Index into `corrections` of the one the panel shows; -1 = none (the panel starts empty).
		int active = -1;
	};

	// Stores `entry` keyed by its defect LOCPOT: replaces the entry with the same path, else appends.
	// Makes it active and returns its index. An entry without a defect LOCPOT is not stored (-1,
	// `file` unchanged) - there is nothing to tell it apart from the next one by.
	int UpsertFnvCorrection(FnvCorrectionsFile &file, PersistedFnvCorrection entry);

	// fnv_corrections.yaml next to manifest.yaml. Schema:
	//   formatVersion: 1
	//   active: 0
	//   corrections:
	//     - defectLocpot: data/GeV/q-1/LOCPOT       # '/' separators; relative when inside the project
	//       bulkLocpot: D:/calc/bulk/LOCPOT
	//       charge: -1
	//       dielectric: 5.7
	//       dielectricSource: promotor (diament)
	//       centreFractional: [x, y, z]
	//       energyCutoff: 0
	//       convention: Sxdefectalign | Pymatgen
	//       useAxis: [true, true, true]
	//       profiles:                                 # optional
	//         charge: -1
	//         dielectric: 5.7
	//         centreFractional: [x, y, z]
	//         energyCutoff: 600.1
	//         latticeEnergy: 0.2526
	//         modelPotentialMean: -0.00078
	//         axes:                                   # exactly three
	//           - length: 14.18
	//             deltaV: [...]                       # flow sequences, full double precision
	//             modelPotential: [...]
	//             window: [centre, width]
	class FnvCorrectionsIO
	{
	public:
		static constexpr int kFormatVersion = 1;

		[[nodiscard]] static Path FilePath(const Path &projectDirectory); // projectDirectory / "fnv_corrections.yaml"

		// False (outError set) when the text is not YAML, the root is not a map, or formatVersion is
		// greater than kFormatVersion. Empty/whitespace text is a valid empty file. A correction entry
		// with a bad value (non-numeric number, unknown convention, a profile whose arrays differ in
		// length or whose axes are not three) is dropped with a line appended to outWarnings - never
		// aborts the rest. `active` out of range after dropping becomes -1. Relative LOCPOT paths are
		// resolved against `projectDirectory`.
		[[nodiscard]] static bool Parse(const std::string &text, const Path &projectDirectory,
			FnvCorrectionsFile &outFile, std::vector<std::string> &outWarnings, std::string &outError);

		// LOCPOT paths inside `projectDirectory` are written relative to it, others absolute; both
		// with '/' separators (as SceneObjectsIO::MakeStructureKey does for structures).
		[[nodiscard]] static std::string Serialize(const FnvCorrectionsFile &file, const Path &projectDirectory);

		// Missing file = success with an empty FnvCorrectionsFile.
		[[nodiscard]] static bool Load(const Path &projectDirectory, FnvCorrectionsFile &outFile,
			std::vector<std::string> &outWarnings, std::string &outError);

		// Sibling temp file renamed over FilePath(); a failure leaves any existing file untouched.
		[[nodiscard]] static bool Save(const Path &projectDirectory, const FnvCorrectionsFile &file, std::string &outError);
	};
} // namespace DefectStudio
