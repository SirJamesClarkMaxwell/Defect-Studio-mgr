#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononModeVectorIO.hpp"

namespace DefectStudio::Tests
{
	class PhononModeVectorIOTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::current_path() / ".task-113g-tests" / ToString(GenerateUuid()));
		void SetUp() override { std::filesystem::create_directories(root.Native()); }
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		void Write(const std::string &name, const std::string &text) { std::ofstream((root / name).Native(), std::ios::binary) << text; }
		nlohmann::json Metadata()
		{
			return {{"schema", "ds.phonon_mode_vector.v1"}, {"mode_index", 3}, {"n_atoms", 2},
				{"frequency_meV", -5.0}, {"convention", "normalised_mass_weighted"}};
		}
		void Npy(const std::vector<float> &values = {.8f, 0, 0, 0, .6f, 0}, const std::string &shape = "(2, 3)",
			const std::string &type = "<f4", bool fortran = false)
		{
			std::string header = "{'descr': '" + type + "', 'fortran_order': " + (fortran ? "True" : "False") + ", 'shape': " + shape + ", }";
			header.append((64 - (10 + header.size() + 1) % 64) % 64, ' '); header += '\n';
			std::ofstream file((root / "eigenvector.npy").Native(), std::ios::binary);
			file.write("\x93NUMPY\x01\x00", 8);
			const char length[2] = {static_cast<char>(header.size() & 255), static_cast<char>(header.size() >> 8)};
			file.write(length, 2); file << header;
			file.write(reinterpret_cast<const char *>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(float)));
		}
	};

	TEST_F(PhononModeVectorIOTests, LoadsSingleModeAndComputesNormalisedParticipation)
	{
		Write("mode.json", Metadata().dump()); Npy();
		PhononModeVector mode;
		std::string error = "old";
		ASSERT_TRUE(PhononModeVectorIO::Load(root, mode, error)) << error;
		EXPECT_TRUE(error.empty()); EXPECT_EQ(mode.modeIndex, 3u); EXPECT_DOUBLE_EQ(mode.frequencyMeV, -5);
		ASSERT_EQ(mode.eigenvector.size(), 6u); ASSERT_EQ(mode.participation.size(), 2u);
		EXPECT_NEAR(mode.participation[0], .64, 1e-6); EXPECT_NEAR(mode.participation[1], .36, 1e-6);
		EXPECT_NEAR(mode.participation[0] + mode.participation[1], 1., 1e-6);
	}

	TEST_F(PhononModeVectorIOTests, RejectsMalformedMetadataAndLeavesOutputUntouched)
	{
		Npy();
		for (const auto &bad : std::vector<nlohmann::json>{
			{{"schema", "wrong"}}, {{"convention", "physical"}}, {{"mode_index", -1}}, {{"mode_index", 6}},
			{{"mode_index", 1.5}}, {{"n_atoms", 0}}, {{"n_atoms", 2.5}}, {{"frequency_meV", nullptr}}})
		{
			auto data = Metadata(); data.update(bad); Write("mode.json", data.dump());
			PhononModeVector mode; mode.modeIndex = 99;
			std::string error;
			EXPECT_FALSE(PhononModeVectorIO::Load(root, mode, error)) << bad;
			EXPECT_FALSE(error.empty()); EXPECT_EQ(mode.modeIndex, 99u);
		}
		Write("mode.json", "not JSON");
		PhononModeVector mode; std::string error;
		EXPECT_FALSE(PhononModeVectorIO::Load(root, mode, error));
		EXPECT_FALSE(PhononModeVectorIO::Load(root / "missing", mode, error));
	}

	TEST_F(PhononModeVectorIOTests, RejectsWrongShapeTypeOrderTruncatedNonfiniteAndUnnormalisedPayloads)
	{
		Write("mode.json", Metadata().dump());
		PhononModeVector mode; mode.modeIndex = 99; std::string error;
		const auto rejects = [&] {
			EXPECT_FALSE(PhononModeVectorIO::Load(root, mode, error));
			EXPECT_FALSE(error.empty()); EXPECT_EQ(mode.modeIndex, 99u);
		};
		Npy({.8f, 0, 0, 0, .6f, 0}, "(3, 2)"); rejects();
		Npy({.8f, 0, 0, 0, .6f, 0}, "(2, 3)", ">f4"); rejects();
		Npy({.8f, 0, 0, 0, .6f, 0}, "(2, 3)", "<f4", true); rejects();
		Npy({.8f}); rejects();
		Npy({std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 1, 0}); rejects();
		Npy({0, 0, 0, 0, 0, 0}); rejects();
		Npy({1, 0, 0, 0, 1, 0}); rejects();
		Write("eigenvector.npy", "truncated"); rejects();
	}

	TEST_F(PhononModeVectorIOTests, ResolvesServerSummaryAndPortableRelativeInput)
	{
		const auto hr = root / "server/spectral-function/12x12x1/displacements_cutoff4A";
		Write("summary.json", nlohmann::json{{"server", {{"hr_dir", hr.Utf8()}}}}.dump());
		Path embedded; std::string error;
		ASSERT_TRUE(PhononModeVectorIO::EmbeddedDirectory(root, embedded, error)) << error;
		EXPECT_EQ(embedded, root / "server/embedded/12x12x1");
		Write("summary.json", nlohmann::json{{"server", {{"hr_dir", "Z:/old/spectral-function/9x9x1/variant"}}},
			{"inputs_relative", {{"hr_dir", "moved/spectral-function/24x24x1/variant"}}}}.dump());
		ASSERT_TRUE(PhononModeVectorIO::EmbeddedDirectory(root, embedded, error)) << error;
		EXPECT_EQ(embedded, root / "moved/embedded/24x24x1");
		Write("summary.json", nlohmann::json{{"server", {{"hr_dir", "bad/layout"}}}}.dump());
		EXPECT_FALSE(PhononModeVectorIO::EmbeddedDirectory(root, embedded, error));
		EXPECT_EQ(embedded, root / "moved/embedded/24x24x1");
	}
}
