#include "Core/dspch.hpp"

#include "IO/PoscarWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <string_view>

#ifdef DS_PLATFORM_WINDOWS
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

#include "Core/Utils/Uuid.hpp"
#include "Domain/Crystal/StructureValidation.hpp"

namespace DefectStudio
{
	namespace
	{
		struct TempFileCleanup
		{
			FilePath path;
			bool created = false;
			~TempFileCleanup()
			{
				if (created)
				{
					std::error_code error;
					FileSystem::Remove(path, error);
				}
			}
		};

		[[nodiscard]] StructuredError WriteError(std::string details)
		{
			return StructuredError(ErrorCategory::IO, Severity::Error,
				"Nie udało się zapisać pliku POSCAR", std::move(details),
				"Sprawdź folder docelowy, uprawnienia i dostępną przestrzeń na dysku.", "PoscarWriter");
		}
	} // namespace

	Result<void> PoscarWriter::Write(
		const CrystalStructure &structure, const Path &outputPath, bool overwriteExisting)
	{
		if (Result<void> valid = ValidateStructureForPersistence(structure); !valid)
			return valid.Error();
		if (outputPath.Empty() || outputPath.Native().filename().empty())
			return WriteError("Empty POSCAR output path");

		// ASE accepted only chemical symbols (including its dummy element X), not arbitrary labels.
		constexpr std::string_view symbols =
			" X H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn "
			"Ga Ge As Se Br Kr Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce "
			"Pr Nd Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn "
			"Fr Ra Ac Th Pa U Np Pu Am Cm Bk Cf Es Fm Md No Lr Rf Db Sg Bh Hs Mt Ds Rg Cn Nh Fl Mc Lv Ts Og ";
		for (const AtomSite &atom : structure.atoms)
			if (atom.species.find_first_of(" \t\r\n") != std::string::npos
				|| symbols.find(" " + atom.species + " ") == std::string_view::npos)
				return StructuredError(ErrorCategory::Validation, Severity::Error,
					"Nieprawidłowy symbol pierwiastka", "Invalid species: " + atom.species,
					"Wybierz prawidłowy pierwiastek przed zapisaniem struktury.", "PoscarWriter");

		try
		{
			std::vector<std::size_t> indices(structure.atoms.size());
			std::iota(indices.begin(), indices.end(), std::size_t{0});
			std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
				const auto &left = structure.atoms[a];
				const auto &right = structure.atoms[b];
				if (left.species != right.species)
					return left.species < right.species;
				if (left.fractional.x != right.fractional.x)
					return left.fractional.x < right.fractional.x;
				if (left.fractional.y != right.fractional.y)
					return left.fractional.y < right.fractional.y;
				return left.fractional.z < right.fractional.z;
			});

			std::vector<std::pair<std::string, std::size_t>> speciesCounts;
			for (std::size_t index : indices)
			{
				const std::string &species = structure.atoms[index].species;
				if (speciesCounts.empty() || speciesCounts.back().first != species)
					speciesCounts.emplace_back(species, 0);
				++speciesCounts.back().second;
			}

			// Short on purpose: a path past MAX_PATH makes the ofstream below fail on Windows (ase handled
			// long paths, this does not), and 8 hex digits are plenty inside one destination folder.
			TempFileCleanup temp{outputPath.Native().parent_path() / (".poscar_" + ToString(GenerateUuid()).substr(0, 8) + ".tmp")};
			std::ofstream file(temp.path, std::ios::out | std::ios::binary | std::ios::noreplace);
			if (!file)
				return WriteError("Could not create temporary POSCAR in " + outputPath.parent_path().Utf8());
			temp.created = true;
			file.imbue(std::locale::classic());
			// ASE's default comment lists the species in their output order.
			for (std::size_t i = 0; i < speciesCounts.size(); ++i)
				file << (i == 0 ? "" : " ") << speciesCounts[i].first;
			file << "\n1.0000000000000000\n";
			// Scientific notation keeps 17 significant digits even for very small coordinates.
			file << std::scientific << std::setprecision(std::numeric_limits<double>::max_digits10 - 1);
			for (const auto &row : structure.cell.vectors)
				file << "  " << static_cast<double>(row.x) << ' ' << static_cast<double>(row.y) << ' '
					<< static_cast<double>(row.z) << '\n';
			for (const auto &entry : speciesCounts)
				file << ' ' << entry.first;
			file << '\n';
			for (const auto &entry : speciesCounts)
				file << ' ' << entry.second;
			file << "\nDirect\n";
			for (std::size_t index : indices)
			{
				const auto &position = structure.atoms[index].fractional;
				file << "  " << static_cast<double>(position.x) << ' ' << static_cast<double>(position.y) << ' '
					<< static_cast<double>(position.z) << '\n';
			}
			file.close();
			if (!file)
				return WriteError("Writing or closing temporary POSCAR failed for " + outputPath.Utf8());

			std::error_code error;
			if (!overwriteExisting)
				FileSystem::RenameNoReplace(temp.path, outputPath.Native(), error);
			else
			{
#ifdef DS_PLATFORM_WINDOWS
				if (::MoveFileExW(temp.path.c_str(), outputPath.Native().c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
					error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
#else
				std::filesystem::rename(temp.path, outputPath.Native(), error);
#endif
			}
			if (error)
				return WriteError("POSCAR commit failed for " + outputPath.Utf8() + ": " + error.message());
			temp.created = false;
			return Result<void>{};
		}
		catch (const std::exception &exception)
		{
			return WriteError(exception.what());
		}
	}
} // namespace DefectStudio
