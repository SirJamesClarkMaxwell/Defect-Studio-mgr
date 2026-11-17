#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "Core/Utils/Uuid.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	class PhononDefectFolderSessionTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_folder_session_" + ToString(GenerateUuid())));
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		void Write(const Path &directory, const std::string &name, const std::string &text = "x")
		{
			std::filesystem::create_directories(directory.Native());
			std::ofstream((directory / name).Native()) << text;
		}
		PhononFolderSelection Selection(const std::string &name)
		{
			const auto system = root / "server" / name;
			for (const auto &directory : {system / "relax", system / "excited/singlet", system / "excited/triplet", system / "gs_at_es"})
			{
				Write(directory, "OUTCAR"); Write(directory, "CONTCAR");
			}
			Write(system / "dispersion", "FORCE_SETS");
			PhononFolderSelection selection;
			selection.proposal = ScanPhononDefectFolder(system, {}).at(0);
			return selection;
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

	TEST_F(PhononDefectFolderSessionTests, CreatesPickedCasesAttachesOnlyCheckedDatasetsAndPreservesActiveCase)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root / "project");
		const auto original = session.AddCase("original");
		session.Settings().bulkForceConstantDirectory = root / "bulk";
		auto selected = Selection("a"), skipped = Selection("b");
		selected.proposal.name = "edited";
		selected.proposal.tripletDirectory = {};
		const auto attached = selected.proposal.systemDirectory / "defectstudio/phonons";
		const auto ignored = selected.proposal.systemDirectory / "defectstudio/ignored";
		Write(attached, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"chosen"})");
		Write(ignored, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"ignored"})");
		selected.proposal.datasets = {*ReadDatasetRef(attached), *ReadDatasetRef(ignored)};
		selected.datasetChecked = {true, false};
		skipped.selected = false;
		const auto created = session.CreateCasesFromFolders({selected, skipped});
		ASSERT_EQ(created.size(), 1u); ASSERT_EQ(session.Cases().size(), 2u);
		EXPECT_EQ(session.ActiveCaseId(), original);
		const auto &item = session.Cases()[1];
		EXPECT_EQ(item.id, created[0]); EXPECT_EQ(item.name, "edited"); EXPECT_EQ(item.sourceDirectory, selected.proposal.systemDirectory);
		EXPECT_EQ(item.settings.groundStateDirectory, selected.proposal.groundStateDirectory);
		EXPECT_EQ(item.settings.excitedStateDirectory, selected.proposal.excitedStateDirectory);
		EXPECT_EQ(item.settings.forceConstantDirectory, selected.proposal.forceConstantDirectory);
		EXPECT_EQ(item.settings.groundAtExcitedDirectory, selected.proposal.groundAtExcitedDirectory);
		EXPECT_TRUE(item.settings.tripletDirectory.Empty()); EXPECT_EQ(item.settings.bulkForceConstantDirectory, root / "bulk");
		EXPECT_EQ(item.datasets, (std::vector<Path>{attached})); EXPECT_EQ(item.settings.resultDirectory, attached);
		ASSERT_EQ(session.Datasets().size(), 1u); EXPECT_EQ(session.Datasets()[0].directory, attached);
	}

	TEST_F(PhononDefectFolderSessionTests, EmptySessionActivatesOnlyFirstCreatedCaseAndAmbiguousBulkIsNotGuessed)
	{
		PhononPanelSession session;
		const auto selections = std::vector{Selection("a"), Selection("b")};
		const auto created = session.CreateCasesFromFolders(selections);
		ASSERT_EQ(created.size(), 2u); EXPECT_EQ(session.ActiveCaseId(), created[0]);
		session.Settings().bulkForceConstantDirectory = root / "bulk-a";
		ASSERT_TRUE(session.ActivateCase(created[1])); session.Settings().bulkForceConstantDirectory = root / "bulk-b";
		const auto more = session.CreateCasesFromFolders({Selection("c")});
		ASSERT_EQ(more.size(), 1u); EXPECT_EQ(session.ActiveCaseId(), created[1]);
		EXPECT_TRUE(session.Cases().back().settings.bulkForceConstantDirectory.Empty());
		auto missing = Selection("missing"); missing.proposal.excitedStateDirectory = {};
		EXPECT_TRUE(session.CreateCasesFromFolders({missing}).empty());
	}

	TEST_F(PhononDefectFolderSessionTests, RememberedRootsUseBackgroundScanAndImportedMtimeDetectsUpdates)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		PhononPanelSession session(jobs);
		session.SetPersistedState({}, root / "project");
		auto selection = Selection("dimer");
		const auto id = session.CreateCasesFromFolders({selection}).at(0);
		const auto system = selection.proposal.systemDirectory;
		const auto oldHr = system / "spectral-function/12x12x1/displacements";
		const auto newHr = system / "spectral-function/24x24x1/displacements";
		Write(oldHr, "hr_factors.dat"); Write(newHr, "hr_factors.dat");
		const auto timestamp = std::filesystem::last_write_time((oldHr / "hr_factors.dat").Native());
		const double seconds = std::chrono::duration<double>(std::chrono::clock_cast<std::chrono::system_clock>(timestamp).time_since_epoch()).count();
		const auto dataset = root / "project/defectstudio/phonons/imported";
		const nlohmann::json summary = {{"schema", "ds.phonon_vibronic.v1"},
			{"server", {{"hr_dir", oldHr.Utf8()}, {"hr_factors_mtime", seconds}}}};
		Write(dataset, "summary.json", summary.dump());
		session.AddDataset(*ReadDatasetRef(dataset)); session.AttachDataset(id, dataset);
		session.Settings().defectRoots = {root / "server"};
		(void)session.Cases();
		const auto saved = session.Settings();
		session.SetPersistedState(saved, root / "project"); session.SetProjectRoots({});
		ASSERT_TRUE(Finished(jobs)); EXPECT_TRUE(session.NewServerResults(id).empty());
		ASSERT_TRUE(session.PollDatasetScan());
		ASSERT_EQ(session.NewServerResults(id).size(), 1u);
		EXPECT_EQ(session.NewServerResults(id)[0].result.hrDirectory, newHr); EXPECT_FALSE(session.NewServerResults(id)[0].updated);
		std::filesystem::last_write_time((oldHr / "hr_factors.dat").Native(), timestamp + std::chrono::seconds(2));
		session.RescanDefectFolders(); ASSERT_TRUE(Finished(jobs)); ASSERT_TRUE(session.PollDatasetScan());
		const auto &results = session.NewServerResults(id);
		ASSERT_EQ(results.size(), 2u);
		EXPECT_TRUE(std::any_of(results.begin(), results.end(), [&](const auto &hit) { return hit.result.hrDirectory == oldHr && hit.updated; }));
		session.RemoveDefectRoot(root / "server");
		EXPECT_TRUE(session.Settings().defectRoots.empty()); EXPECT_EQ(session.Cases().size(), 1u);
	}

	TEST_F(PhononDefectFolderSessionTests, HiddenCasesAreSkippedByBatchAndRetainTheirCcdResultWhenShown)
	{
		PhononPanelSession session;
		const auto id = session.AddCase("hidden");
		session.SetCcdResult({}); session.SetCaseChecked(id, true);
		session.SetCaseHidden(id, true);
		EXPECT_FALSE(session.CcdResult()); EXPECT_FALSE(session.CaseChecked(id));
		session.StartBatch({id}, {PhononBatchStep::Phonons});
		EXPECT_FALSE(session.IsBatchRunning()); EXPECT_EQ(session.CaseRun(id).status, PhononCaseStatus::Idle);
		session.SetCaseHidden(id, false);
		EXPECT_TRUE(session.CcdResult()); EXPECT_TRUE(session.CaseChecked(id));
	}

	TEST_F(PhononDefectFolderSessionTests, CancelledFolderScanCannotCommitAfterProjectChange)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		PhononPanelSession session(jobs);
		(void)Selection("dimer");
		session.AddDefectFolders({root / "server", root / "server"});
		EXPECT_EQ(session.Settings().defectRoots.size(), 1u);
		session.SetPersistedState({}, root / "other-project");
		ASSERT_TRUE(Finished(jobs)); EXPECT_FALSE(session.PollDatasetScan());
		EXPECT_TRUE(session.Cases().empty()); EXPECT_TRUE(session.Settings().defectRoots.empty());
	}
}
