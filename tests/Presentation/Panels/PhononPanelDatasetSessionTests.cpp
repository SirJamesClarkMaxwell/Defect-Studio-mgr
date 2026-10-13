#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "Core/Utils/Uuid.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	class PhononPanelDatasetSessionTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_dataset_session_" + ToString(GenerateUuid())));
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		void Write(const Path &directory, const char *filename, const std::string &text)
		{
			std::filesystem::create_directories(directory.Native());
			std::ofstream out(directory.Join(filename).Native()); out << text;
		}
		bool Finished(const Ref<JobSystem> &jobs)
		{
			return WaitUntil([&] {
				const auto all = jobs->GetAllJobs();
				return !all.empty() && std::all_of(all.begin(), all.end(), [](const auto &job) {
					return job.status == JobStatus::Completed || job.status == JobStatus::Cancelled || job.status == JobStatus::Failed;
				});
			}, Time::Milliseconds(5000));
		}
	};

	TEST_F(PhononPanelDatasetSessionTests, WorkerScanCommitsOnlyOnPollAndDeduplicatesOverlappingRoots)
	{
		const Path dataset = root / "a/defectstudio/phonons";
		Write(dataset, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"A"})");
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		PhononPanelSession session;
		session.StartDatasetScan(jobs, {root, root / "a"});
		ASSERT_TRUE(Finished(jobs));
		EXPECT_TRUE(session.Datasets().empty());
		EXPECT_TRUE(session.PollDatasetScan());
		ASSERT_EQ(session.Datasets().size(), 1u);
		EXPECT_EQ(session.Datasets()[0].directory, dataset);
		EXPECT_FALSE(session.IsScanningDatasets());
	}

	TEST_F(PhononPanelDatasetSessionTests, CancelledScanCannotCommitIntoTheNextProject)
	{
		Write(root / "defectstudio/phonons", "summary.json", R"({"schema":"ds.phonon_vibronic.v1"})");
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		PhononPanelSession session;
		session.StartDatasetScan(jobs, {root});
		session.CancelDatasetScan();
		session.SetDatasets({});
		ASSERT_TRUE(Finished(jobs));
		EXPECT_FALSE(session.PollDatasetScan());
		EXPECT_TRUE(session.Datasets().empty());
	}

	TEST_F(PhononPanelDatasetSessionTests, SpectraCacheAndCachedErrorsRefreshWhenDatasetIsReplaced)
	{
		const Path dataset = root / "defectstudio/phonons";
		Write(dataset, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"A",
			"s_total":1,"debye_waller":0.36,"stable":true,"sigma_meV":6,"gamma_meV":2,
			"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
			"zpl_eV":{"computed":null,"computed_spin_purified":null,"experimental":null,"formula":1.945},
			"zpl_formula":{"text":"1.945"}})");
		Write(dataset, "pl.tsv", "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula\n1.945\tnan\tnan\tnan\t1\n");
		PhononPanelSession session;
		session.AddDataset(*ReadDatasetRef(dataset));
		std::string error;
		const auto loaded = session.Spectra(dataset, error);
		ASSERT_TRUE(loaded) << error;
		EXPECT_TRUE(loaded->get().modes.empty());
		EXPECT_EQ(loaded->get().zplFormulaText, "1.945");
		Write(dataset, "pl.tsv", "invalid");
		EXPECT_TRUE(session.Spectra(dataset, error)); // no disk access until invalidation
		session.AddDataset(*ReadDatasetRef(dataset));
		EXPECT_FALSE(session.Spectra(dataset, error));
		EXPECT_FALSE(error.empty());
		std::filesystem::remove((dataset / "pl.tsv").Native());
		EXPECT_FALSE(session.Spectra(dataset, error)); // failed reads are cached too
		session.SetDatasets(session.Datasets());
		EXPECT_TRUE(session.Spectra(dataset, error));
		EXPECT_TRUE(error.empty());
	}
}
