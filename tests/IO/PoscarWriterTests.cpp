#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "IO/PoscarWriter.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// A name with a space, written into a native Windows temp path. Both matter: the whole
		// point of this test is that the writer hands Python a path full of backslashes, which an
		// earlier hand-rolled JSON string turned into invalid escapes ("\U", "\N") and made every
		// save fail before the structure ever reached ase.
		[[nodiscard]] Path MakeTempPoscarPath()
		{
			return Path::FromResolved(FileSystem::TempDirectoryPath()) /
				("New Structure " + std::to_string(Time::NowSteady().time_since_epoch().count()) + ".vasp");
		}
	} // namespace

	TEST(PoscarWriterTests, WritesFractionalCoordinatesToNativePath)
	{
		CrystalStructure structure;
		structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 4.0f});
		structure.atoms = {
			AtomSite{"Si", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), 0},
			AtomSite{"Ge", glm::vec3(0.25f, 0.25f, 0.25f), glm::vec3(0.0f, 0.0f, 0.0f), 0},
		};

		const Path outputPath = MakeTempPoscarPath();
		const Path inputJsonPath = Path::FromResolved(FileSystem::TempDirectoryPath()) /
			("poscar_input_" + std::to_string(Time::NowSteady().time_since_epoch().count()) + ".json");
		const Result<void> written = PoscarWriter::Write(structure, outputPath, inputJsonPath);
		if (!written)
			GTEST_SKIP() << "ase unavailable in current environment: " << written.Error().technicalDetails;

		std::ifstream file(outputPath.Native());
		ASSERT_TRUE(file.is_open()) << "POSCAR not written to " << outputPath.String();
		std::stringstream contents;
		contents << file.rdbuf();
		file.close();
		std::error_code removeError;
		FileSystem::Remove(outputPath.Native(), removeError);

		const std::string poscar = contents.str();
		EXPECT_NE(poscar.find("Ge"), std::string::npos);
		EXPECT_NE(poscar.find("Si"), std::string::npos);
		// 0.25 fractional in a 4 A cubic cell is 1.0 A - the writer used to pass fractional
		// coordinates to ase as cartesian, which would put this atom at 0.25 A instead.
		EXPECT_NE(poscar.find("1.0000000000000000"), std::string::npos)
			<< "fractional coordinates were not scaled by the cell:\n" << poscar;
	}
} // namespace DefectStudio::Tests
