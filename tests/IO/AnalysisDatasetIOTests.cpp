#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "Core/Utils/Uuid.hpp"
#include "IO/AnalysisDatasetIO.hpp"

namespace DefectStudio::Tests
{
	class AnalysisDatasetIOTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_datasets_" + ToString(GenerateUuid())));
		void SetUp() override { std::filesystem::create_directories(root.Native()); }
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		void Summary(const Path &directory, const std::string &text)
		{
			std::filesystem::create_directories(directory.Native());
			std::ofstream out((directory / "summary.json").Native());
			out << text;
		}
	};

	TEST_F(AnalysisDatasetIOTests, FindsKnownChildrenSortedAndDoesNotDescendIntoDatasets)
	{
		const Path phonons = root / "a/defectstudio/phonons", dispersion = root / "b/c/defectstudio/dispersion";
		Summary(phonons, R"({"schema":"ds.phonon_vibronic.v1","label":"Dimer"})");
		Summary(dispersion, R"({"schema":"ds.phonon_dispersion.v1"})");
		Summary(root / "defectstudio/ccd", R"({"schema":"ds.ccd_analysis.v1"})");
		Summary(root / "defectstudio/unknown", R"({"schema":"future.v1"})");
		Summary(root / "defectstudio/broken", "{");
		std::filesystem::create_directories((root / "defectstudio/x").Native());
		Summary(phonons / "nested/defectstudio/ccd", R"({"schema":"ds.ccd_analysis.v1"})");
		Summary(root / "outside", R"({"schema":"ds.phonon_vibronic.v1"})");
		const auto found = FindDatasets(root, {});
		ASSERT_EQ(found.size(), 3u);
		EXPECT_EQ(found[0].directory, phonons);
		EXPECT_EQ(found[0].label, "Dimer");
		EXPECT_EQ(found[1].directory, dispersion);
		EXPECT_EQ(found[1].kind, AnalysisDatasetKind::Dispersion);
		EXPECT_EQ(found[1].label, "dispersion");
		EXPECT_EQ(found[2].kind, AnalysisDatasetKind::Ccd);
		EXPECT_EQ(FindDatasets(root / "defectstudio", {}).size(), 1u);
		EXPECT_TRUE(FindDatasets(root, [] { return true; }).empty());
		EXPECT_FALSE(ReadDatasetRef(root / "defectstudio/x"));
	}

	TEST_F(AnalysisDatasetIOTests, SkipsDirectorySymlinksIncludingTheRoot)
	{
		Summary(root / "real/defectstudio/phonons", R"({"schema":"ds.phonon_vibronic.v1"})");
		std::error_code error;
		std::filesystem::create_directory_symlink((root / "real").Native(), (root / "link").Native(), error);
		if (error) GTEST_SKIP() << "Directory symlinks unavailable: " << error.message();
		EXPECT_TRUE(FindDatasets(root / "link", {}).empty());
		EXPECT_EQ(FindDatasets(root, {}).size(), 1u);
	}

	TEST_F(AnalysisDatasetIOTests, ProvenancePrefersRelativePathsAndFallsBackPerKey)
	{
		const auto dataset = root / "defectstudio/phonons";
		Summary(dataset, R"({
  "schema": "ds.phonon_vibronic.v1",
  "input_paths": {"gs_dir": "old-gs", "es_dir": "../../es", "triplet_dir": null, "bulk_fc_dir": "../../bulk"},
  "inputs_relative": {"gs_dir": "../../gs", "fc_dir": "../../fc", "es_dir": null, "absent": null}
})");
		const auto ref = ReadDatasetRef(dataset);
		ASSERT_TRUE(ref);
		EXPECT_EQ(ref->inputs.at("gs_dir"), root / "gs");
		EXPECT_EQ(ref->inputs.at("es_dir"), root / "es");
		EXPECT_EQ(ref->inputs.at("fc_dir"), root / "fc");
		EXPECT_EQ(ref->inputs.at("bulk_fc_dir"), root / "bulk");
		EXPECT_FALSE(ref->inputs.contains("triplet_dir"));
		EXPECT_FALSE(ref->inputs.contains("absent"));
	}

	TEST_F(AnalysisDatasetIOTests, AbsoluteProvenanceAndMissingInputsKeepExistingFields)
	{
		const auto dataset = root / "defectstudio/ccd";
		std::string absolute = (root / "gs").Utf8();
		std::replace(absolute.begin(), absolute.end(), '\\', '/');
		Summary(dataset, "{\"schema\":\"ds.ccd_analysis.v1\",\"label\":\"CCD\",\"input_paths\":{\"gs_dir\":\"" + absolute + "\"}}");
		const auto ref = ReadDatasetRef(dataset);
		ASSERT_TRUE(ref);
		EXPECT_EQ(ref->inputs.at("gs_dir"), root / "gs");
		EXPECT_EQ(ref->label, "CCD");
		EXPECT_EQ(ref->kind, AnalysisDatasetKind::Ccd);
		Summary(dataset, R"({"schema":"ds.ccd_analysis.v1","label":"CCD"})");
		ASSERT_TRUE(ReadDatasetRef(dataset));
		EXPECT_TRUE(ReadDatasetRef(dataset)->inputs.empty());
	}

	TEST_F(AnalysisDatasetIOTests, DefaultUsesCommonInputDirectoryAndChecksItsExistingAncestor)
	{
		std::filesystem::create_directories((root / "x/excited/s").Native());
		std::filesystem::create_directories((root / "x/relax").Native());
		Path checked;
		const auto writable = [&](const Path &path) { checked = path; return true; };
		EXPECT_EQ(DefaultDatasetDirectory({root / "x/relax", root / "x/excited/s"}, "phonons", root / "project", writable),
			root / "x/defectstudio/phonons");
		EXPECT_EQ(checked, root / "x");
		EXPECT_EQ(DefaultDatasetDirectory({root / "bulk/fc"}, "dispersion", root / "project", writable),
			root / "bulk/fc/defectstudio/dispersion");
		EXPECT_EQ(DefaultDatasetDirectory({root / "x/relax"}, "phonons_emb_12x12x1", root / "project", [](const Path &) { return false; }),
			root / "project/defectstudio/phonons_emb_12x12x1");
		EXPECT_EQ(DefaultDatasetDirectory({}, "ccd", root / "project", writable), root / "project/defectstudio/ccd");
		EXPECT_TRUE(IsDirectoryWritable(root));
		for (const auto &entry : std::filesystem::directory_iterator(root.Native()))
			EXPECT_FALSE(entry.path().filename().string().starts_with(".ds-write-probe-"));
	}
}
