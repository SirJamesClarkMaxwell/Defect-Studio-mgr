#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Core/Utils/Uuid.hpp"
#include "IO/PhononDispersionIO.hpp"

namespace DefectStudio::Tests
{
	class PhononDispersionIOTests : public testing::Test
	{
	protected:
		Path directory;
		void Write(const char *name, const std::string &text)
		{
			std::ofstream stream(directory.Join(name).Native(), std::ios::binary);
			stream << text;
			ASSERT_TRUE(stream.good());
		}
		void SetUp() override
		{
			directory = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_dispersion_" + ToString(GenerateUuid())));
			std::filesystem::create_directories(directory.Native());
			Write("summary.json", R"({"schema":"ds.phonon_dispersion.v1","n_bands":2,
"ticks":[{"distance":0,"label":"G"},{"distance":1,"label":"M/K"},{"distance":2,"label":"G"}],
"breaks":[1],"min_freq_meV":-2,"max_freq_meV":8,"in_plane":true})");
			Write("dispersion.tsv", "segment\tdistance\tband_0\tband_1\n0\t0\t-2\t4\n0\t1\t0\t5\n1\t1\t1\t6\n1\t2\t3\t8\n");
		}
		void TearDown() override
		{
			std::error_code ignored;
			std::filesystem::remove_all(directory.Native(), ignored);
		}
	};

	TEST_F(PhononDispersionIOTests, LoadsBandsAndDisconnectedSegments)
	{
		PhononDispersion result;
		std::string error = "previous error";
		ASSERT_TRUE(PhononDispersionIO::Load(directory, result, error)) << error;
		EXPECT_TRUE(error.empty());
		EXPECT_EQ(result.bandCount, 2);
		EXPECT_EQ(result.segment, (std::vector<int>{0, 0, 1, 1}));
		EXPECT_EQ(result.distance, (std::vector<double>{0, 1, 1, 2}));
		ASSERT_EQ(result.bandsMeV.size(), 2u);
		EXPECT_EQ(result.bandsMeV[0], (std::vector<double>{-2, 0, 1, 3}));
		EXPECT_EQ(result.bandsMeV[1], (std::vector<double>{4, 5, 6, 8}));
		ASSERT_EQ(result.ticks.size(), 3u);
		EXPECT_EQ(result.ticks[1].label, "M/K");
		EXPECT_DOUBLE_EQ(result.ticks[1].distance, 1.0);
		EXPECT_EQ(result.breaks, (std::vector<double>{1}));
		EXPECT_DOUBLE_EQ(result.minMeV, -2.0);
		EXPECT_DOUBLE_EQ(result.maxMeV, 8.0);
		EXPECT_TRUE(result.inPlane);
		EXPECT_TRUE(result.qpoints.empty());
	}

	TEST_F(PhononDispersionIOTests, OptionalQpointsMustMatchRowsAndContainFiniteTriples)
	{
		PhononDispersion result;
		std::string error;
		Write("qpoints.tsv", "qx\tqy\tqz\n0\t0\t0\n0.5\t0\t0\n0.333333333333\t0.333333333333\t0\n0\t0\t0\n");
		ASSERT_TRUE(PhononDispersionIO::Load(directory, result, error)) << error;
		ASSERT_EQ(result.qpoints.size(), 4u);
		EXPECT_EQ(result.qpoints[1], glm::dvec3(0.5, 0, 0));
		for (const char *text : {"qx\tqy\tqz\n0\t0\t0\n", "qx\tqy\tqz\n0\tnan\t0\n", "qx\tqy\tqz\n0\t0\n", "x\ty\tz\n"})
		{
			Write("qpoints.tsv", text);
			EXPECT_FALSE(PhononDispersionIO::Load(directory, result, error));
			EXPECT_FALSE(error.empty());
			EXPECT_EQ(result.qpoints.size(), 4u);
		}
	}

	TEST_F(PhononDispersionIOTests, WrongSchemaFailsWithoutReplacingTheResult)
	{
		Write("summary.json", R"({"schema":"ds.phonon_dispersion.v2"})");
		PhononDispersion result;
		result.bandCount = 7;
		std::string error;
		EXPECT_FALSE(PhononDispersionIO::Load(directory, result, error));
		EXPECT_FALSE(error.empty());
		EXPECT_EQ(result.bandCount, 7);
	}

	TEST_F(PhononDispersionIOTests, MissingFilesFail)
	{
		PhononDispersion result;
		std::string error;
		std::filesystem::remove(directory.Join("dispersion.tsv").Native());
		EXPECT_FALSE(PhononDispersionIO::Load(directory, result, error));
		EXPECT_FALSE(error.empty());
		std::filesystem::remove(directory.Join("summary.json").Native());
		EXPECT_FALSE(PhononDispersionIO::Load(directory, result, error));
		EXPECT_FALSE(error.empty());
	}

	TEST_F(PhononDispersionIOTests, MalformedRowsFail)
	{
		for (const char *row : {"0\t0\t1\n", "0\t0\t1\t2\t3\n", "0\t0\tnan\t2\n",
			"0\tinf\t1\t2\n", "0\t0\t1x\t2\n", "0.5\t0\t1\t2\n", "-1\t0\t1\t2\n",
			"0\t1\t1\t2\n0\t0\t1\t2\n"})
		{
			Write("dispersion.tsv", std::string("segment\tdistance\tband_0\tband_1\n") + row);
			PhononDispersion result;
			std::string error;
			EXPECT_FALSE(PhononDispersionIO::Load(directory, result, error)) << row;
			EXPECT_FALSE(error.empty());
		}
	}

	TEST_F(PhononDispersionIOTests, EmptyDataAndWrongHeaderFail)
	{
		for (const char *text : {"segment\tdistance\tband_0\tband_1\n", "segment\tdistance\tband_0\n0\t0\t1\t2\n"})
		{
			Write("dispersion.tsv", text);
			PhononDispersion result;
			std::string error;
			EXPECT_FALSE(PhononDispersionIO::Load(directory, result, error));
			EXPECT_FALSE(error.empty());
		}
	}

	TEST_F(PhononDispersionIOTests, InvalidMetadataFails)
	{
		for (const char *text : {
			R"({"schema":"ds.phonon_dispersion.v1","n_bands":0})",
			R"({"schema":"ds.phonon_dispersion.v1","n_bands":2.5})",
			R"({"schema":"ds.phonon_dispersion.v1","n_bands":2,"min_freq_meV":null,"max_freq_meV":8})",
			R"({"schema":"ds.phonon_dispersion.v1","n_bands":2,"min_freq_meV":-2,"max_freq_meV":8,"in_plane":true,
"ticks":[{"distance":null,"label":"G"}],"breaks":[]})"})
		{
			Write("summary.json", text);
			PhononDispersion result;
			std::string error;
			EXPECT_FALSE(PhononDispersionIO::Load(directory, result, error));
			EXPECT_FALSE(error.empty());
		}
	}
}
