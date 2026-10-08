#include <filesystem>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "IO/PoscarWriter.hpp"
#include "ScientificRuntime/Python/PuntukasBridge.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// A directory with a space in a native Windows temp path: the writer used to hand that path
		// to a Python subprocess, and a path full of backslashes broke it more than once.
		class PoscarWriterTests : public ::testing::Test
		{
		protected:
			void SetUp() override
			{
				m_Directory = FileSystem::TempDirectoryPath() /
					("poscar writer " + std::to_string(Time::NowSteady().time_since_epoch().count()));
				std::filesystem::create_directories(m_Directory);
			}

			void TearDown() override
			{
				std::error_code error;
				std::filesystem::remove_all(m_Directory, error);
			}

			[[nodiscard]] Path Output() const { return Path::FromResolved(m_Directory / "POSCAR"); }

			[[nodiscard]] std::vector<std::string> Lines() const
			{
				std::ifstream file(m_Directory / "POSCAR");
				std::vector<std::string> lines;
				for (std::string line; std::getline(file, line);)
					lines.push_back(line);
				return lines;
			}

			[[nodiscard]] std::size_t FilesInDirectory() const
			{
				std::size_t count = 0;
				for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator(m_Directory))
					++count;
				return count;
			}

			std::filesystem::path m_Directory;
		};

		[[nodiscard]] CrystalStructure SiGe()
		{
			CrystalStructure structure;
			structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 4.0f});
			structure.atoms = {
				AtomSite{"Si", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), 0},
				AtomSite{"Ge", glm::vec3(0.25f, 0.5f, 0.125f), glm::vec3(1.0f, 2.0f, 0.5f), 0},
			};
			structure.atoms[1].position = glm::vec3(1.0f, 2.0f, 0.5f);
			structure.atoms[1].fractional = glm::vec3(0.25f, 0.5f, 0.125f);
			return structure;
		}
	} // namespace

	TEST_F(PoscarWriterTests, WritesDirectCoordinatesThatReadBackExactly)
	{
		ASSERT_TRUE(PoscarWriter::Write(SiGe(), Output()));
		const std::vector<std::string> lines = Lines();
		ASSERT_EQ(lines.size(), 10u);
		EXPECT_EQ(lines[5], " Ge Si"); // species sorted
		EXPECT_EQ(lines[6], " 1 1");
		EXPECT_EQ(lines[7], "Direct");

		std::istringstream lattice(lines[2]);
		double ax = 0.0, ay = 0.0, az = 0.0;
		lattice >> ax >> ay >> az;
		EXPECT_DOUBLE_EQ(ax, 4.0);
		std::istringstream ge(lines[8]);
		double x = 0.0, y = 0.0, z = 0.0;
		ge >> x >> y >> z;
		EXPECT_DOUBLE_EQ(x, 0.25);
		EXPECT_DOUBLE_EQ(y, 0.5);
		EXPECT_DOUBLE_EQ(z, 0.125);
		EXPECT_EQ(FilesInDirectory(), 1u); // no temporary file left behind
	}

	TEST_F(PoscarWriterTests, ANewStructureNeverReplacesAnExistingFile)
	{
		{
			std::ofstream existing(m_Directory / "POSCAR");
			existing << "keep me\n";
		}
		EXPECT_FALSE(PoscarWriter::Write(SiGe(), Output(), false));
		ASSERT_FALSE(Lines().empty());
		EXPECT_EQ(Lines().front(), "keep me");
		EXPECT_EQ(FilesInDirectory(), 1u);
	}

	TEST_F(PoscarWriterTests, SavingAnEditedStructureReplacesIt)
	{
		{
			std::ofstream existing(m_Directory / "POSCAR");
			existing << "old\n";
		}
		ASSERT_TRUE(PoscarWriter::Write(SiGe(), Output(), true));
		ASSERT_EQ(Lines().size(), 10u);
		EXPECT_EQ(Lines()[7], "Direct");
		EXPECT_EQ(FilesInDirectory(), 1u);
	}

	TEST_F(PoscarWriterTests, RoundTripsThroughTheProjectsPoscarReader)
	{
		CrystalStructure structure = SiGe();
		structure.cell.vectors = {
			glm::vec3(4.123456f, 0.125f, 0.25f),
			glm::vec3(-0.375f, 5.234567f, 0.5f),
			glm::vec3(0.625f, -0.75f, 6.345678f)};
		structure.atoms.clear();
		for (const auto &[species, fractional] : std::array{
			std::pair{std::string("Si"), glm::vec3(0.1f, 0.7f, 0.3f)},
			std::pair{std::string("Ge"), glm::vec3(0.6f, 0.2f, 0.4f)},
			std::pair{std::string("Si"), glm::vec3(0.1f, 0.7f, 0.2f)},
			std::pair{std::string("Ge"), glm::vec3(0.2f, 0.3f, 0.5f)},
			std::pair{std::string("Si"), glm::vec3(0.1f, 0.6f, 0.8f)}})
		{
			AtomSite atom;
			atom.species = species;
			atom.fractional = fractional;
			atom.position = structure.FractionalToCartesian(fractional);
			structure.atoms.push_back(atom);
		}
		const auto written = PoscarWriter::Write(structure, Output());
		ASSERT_TRUE(written) << written.Error().technicalDetails;
		PuntukasBridge reader;
		const auto loaded = reader.LoadStructure(Output());
		ASSERT_TRUE(loaded) << loaded.Error().technicalDetails;
		ASSERT_EQ(loaded->sites.size(), structure.atoms.size());
		for (int row = 0; row < 3; ++row)
			for (int component = 0; component < 3; ++component)
				EXPECT_NEAR(loaded->lattice[row][component], structure.cell.vectors[row][component], 1e-10);
		const std::array<std::size_t, 5> sortedIndices{3, 1, 4, 2, 0};
		for (std::size_t i = 0; i < sortedIndices.size(); ++i)
		{
			const AtomSite &expected = structure.atoms[sortedIndices[i]];
			EXPECT_EQ(loaded->sites[i].element, expected.species);
			for (int component = 0; component < 3; ++component)
				EXPECT_NEAR(loaded->sites[i].fractionalPosition[component], expected.fractional[component], 1e-10);
		}
		EXPECT_EQ(Lines().front(), "Ge Si");
		EXPECT_EQ(FilesInDirectory(), 1u);
	}

	TEST_F(PoscarWriterTests, PreservesTinyAndUnwrappedFractionalCoordinates)
	{
		CrystalStructure structure = SiGe();
		structure.atoms.resize(1);
		structure.atoms.front().fractional = glm::vec3(1e-30f, -1.234567f, 12.345678f);
		ASSERT_TRUE(PoscarWriter::Write(structure, Output()));
		const auto lines = Lines();
		ASSERT_EQ(lines.size(), 9u);
		std::istringstream coordinates(lines[8]);
		coordinates.imbue(std::locale::classic());
		for (int component = 0; component < 3; ++component)
		{
			double value = 0;
			ASSERT_TRUE(static_cast<bool>(coordinates >> value));
			EXPECT_DOUBLE_EQ(value, static_cast<double>(structure.atoms.front().fractional[component]));
		}
		EXPECT_EQ(FilesInDirectory(), 1u);
	}

	TEST_F(PoscarWriterTests, FailedReplacementLeavesDestinationAndNoTemporaryFile)
	{
		std::filesystem::create_directory(Output().Native());
		const auto marker = Output().Native() / "keep.txt";
		std::ofstream(marker) << "keep me";
		EXPECT_FALSE(PoscarWriter::Write(SiGe(), Output(), true));
		std::ifstream file(marker);
		std::string contents;
		std::getline(file, contents);
		EXPECT_EQ(contents, "keep me");
		EXPECT_EQ(FilesInDirectory(), 1u);
	}

	TEST_F(PoscarWriterTests, InvalidInputAndMissingParentLeaveNoFiles)
	{
		CrystalStructure structure = SiGe();
		structure.atoms.front().species = "Si\nGe";
		EXPECT_FALSE(PoscarWriter::Write(structure, Output()));
		structure = SiGe();
		structure.atoms.front().fractional.x = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(PoscarWriter::Write(structure, Output()));
		structure = SiGe();
		structure.cell.vectors[2] = structure.cell.vectors[1];
		EXPECT_FALSE(PoscarWriter::Write(structure, Output()));
		EXPECT_FALSE(PoscarWriter::Write(SiGe(), Path::FromResolved(m_Directory / "missing" / "POSCAR")));
		EXPECT_EQ(FilesInDirectory(), 0u);
	}

	TEST_F(PoscarWriterTests, RefusingOverwritePreservesEveryByte)
	{
		const std::string original("first\r\n\0last\n", 13);
		{
			std::ofstream file(Output().Native(), std::ios::binary);
			file.write(original.data(), static_cast<std::streamsize>(original.size()));
		}
		EXPECT_FALSE(PoscarWriter::Write(SiGe(), Output(), false));
		std::ifstream file(Output().Native(), std::ios::binary);
		const std::string actual{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		EXPECT_EQ(actual, original);
		EXPECT_EQ(FilesInDirectory(), 1u);
	}
} // namespace DefectStudio::Tests
