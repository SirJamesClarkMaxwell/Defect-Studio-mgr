#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "Domain/Defects/CalculationCatalogue.hpp"

namespace DefectStudio
{
	// task/97: VASP runs from OUTCAR without reading it whole. On the Okeanos mount (SSHFS) a read
	// runs at ~0.5 MB/s and a PBE relaxation's OUTCAR is ~24 MB, while everything the energetics
	// module needs sits in the first ~256 KB (POTCAR titles, ZVAL, ions per type, NELECT, the INCAR
	// echo) and the last ~128 KB (last TOTEN, "reached required accuracy", the timing footer).
	namespace VaspRunRecordIO
	{
		constexpr std::size_t kHeadBytes = 256 * 1024;
		constexpr std::size_t kTailBytes = 128 * 1024;

		// Fills a record from the two pieces of one OUTCAR (head = its first bytes, tail = its last
		// bytes; for a short file both may be the whole text). Pure text parsing, no file access -
		// `directory`, the file flags and the cache key are left as they are. Missing pieces of
		// information stay empty; `error` is set only when the head has no POTCAR titles or ion
		// counts (not an OUTCAR, or a truncated one).
		void ParseOutcar(std::string_view head, std::string_view tail, VaspRunRecord &record);

		// The record of the run in `directory`: OUTCAR head + tail, size and write time, and whether
		// LOCPOT and CONTCAR are next to it. A missing OUTCAR -> `error` set.
		[[nodiscard]] VaspRunRecord Read(const Path &directory);

		// True when OUTCAR in the record's folder still has the size and write time `Read` saw - a
		// rescan then reuses the record instead of reading the file again.
		[[nodiscard]] bool IsUpToDate(const VaspRunRecord &record);

		// Every folder under `root` (itself included) holding an OUTCAR, sorted. Keeps descending
		// below a run (HSE runs live inside PBE ones). Unreadable folders are skipped, symlinks are
		// not followed. `cancelled` is polled between folders; true stops the walk early.
		[[nodiscard]] std::vector<Path> FindRuns(const Path &root, const std::function<bool()> &cancelled = {});

		// The band edges of a bulk run from its EIGENVAL text (ISPIN 1 or 2, any number of k-points):
		// VBM = the highest eigenvalue with occupation > 0.5, CBM = the lowest with occupation <= 0.5,
		// over all spins and k-points (the puntukas convention). nullopt for a malformed file or no
		// empty / no occupied band.
		struct BandEdges
		{
			double vbm = 0.0;
			double cbm = 0.0;
		};
		[[nodiscard]] std::optional<BandEdges> ParseEigenval(std::string_view text);
		// ParseEigenval of `directory`/EIGENVAL (~50 KB for a 512-atom Gamma-only run).
		[[nodiscard]] std::optional<BandEdges> ReadBandEdges(const Path &directory);
		// Cell volume in A^3 from POSCAR/CONTCAR text: |a1 . (a2 x a3)| * scale^3, or -scale when the
		// scale line is negative (VASP's "this is the volume"). nullopt when the header is unreadable.
		[[nodiscard]] std::optional<double> ParsePoscarVolume(std::string_view text);
		// ParsePoscarVolume of `directory`/CONTCAR, else POSCAR.
		[[nodiscard]] std::optional<double> ReadCellVolume(const Path &directory);
	} // namespace VaspRunRecordIO
} // namespace DefectStudio
