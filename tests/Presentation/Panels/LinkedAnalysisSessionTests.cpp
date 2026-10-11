#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <fstream>

#include "Presentation/Panels/CalculationSummaryPanel.hpp"
#include "Presentation/Panels/PanelRegistry.hpp"
#include "Presentation/Panels/DefectEnergeticsPanel.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		struct ScanWorkspace
		{
			Path root = Path::FromResolved(FileSystem::TempDirectoryPath()) /
				("DefectStudioDefectSetTests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			~ScanWorkspace()
			{
				std::error_code ignored;
				FileSystem::RemoveAll(root.Native(), ignored);
			}
		};

		EnergeticsFile VacancyInputs()
		{
			EnergeticsFile file;
			file.root = Path::FromUtf8("task101-missing-fixture");
			file.gap = 5.0;
			VaspRunRecord bulk;
			bulk.directory = file.root / Path::FromUtf8("bulk");
			bulk.species = {"C"};
			bulk.ions = {8};
			bulk.valence = {4.0};
			bulk.electrons = 32.0;
			bulk.totalEnergy = -80.0;
			bulk.finished = true;
			VaspRunRecord vacancy = bulk;
			vacancy.directory = file.root / Path::FromUtf8("vacancy/q0");
			vacancy.ions = {7};
			vacancy.electrons = 28.0;
			vacancy.totalEnergy = -65.0;
			file.bulkRun = bulk.directory;
			file.runs = {bulk, vacancy};
			file.concentrations.sites["vacancy"] = 1e22;
			file.concentrations.temperatureSteps = 1;
			return file;
		}

		bool WaitForConcentrations(const Ref<JobSystem> &jobs)
		{
			return WaitUntil([&] {
				const auto all = jobs->GetAllJobs();
				return !all.empty() && std::ranges::all_of(all, [](const JobSnapshot &job) {
					return job.status == JobStatus::Completed || job.status == JobStatus::Cancelled || job.status == JobStatus::Failed;
				});
			}, Time::Milliseconds(5000));
		}
	}

	TEST(LinkedAnalysisSessionTests, ClonesShareSessionsAndKeepTheirViewVisibility)
	{
		const auto energetics = CreateRef<DefectEnergeticsSession>(WeakRef<JobSystem>{});
		DefectEnergeticsPanel formation(energetics, DefectEnergeticsPanel::View::Formation, "Energie tworzenia");
		const auto formationClone = std::dynamic_pointer_cast<DefectEnergeticsPanel>(formation.Clone());
		ASSERT_NE(formationClone, nullptr);
		EXPECT_EQ(formationClone->Session(), energetics);
		formationClone->SetVisible(true);
		EXPECT_FALSE(formation.IsVisible());
	}

	TEST(LinkedAnalysisSessionTests, CalculationSummaryClonesOwnSessionsAndKeepIndependentSelections)
	{
		const auto summary = CreateRef<CalculationSummarySession>(WeakRef<JobSystem>{}, false);
		summary->OpenDirectory(Path::FromUtf8("task103-missing-fixture"));
		summary->SetAvailableBands({1022, 1023});
		summary->SetBandSelected(1022, false);
		summary->SetSpinSelected(2, false);
		int updates = 0;
		summary->SetUpdateInstancesHandler([&] { ++updates; });
		CalculationSummaryPanel panel(summary, "Calculation Summary");
		const auto clone = std::dynamic_pointer_cast<CalculationSummaryPanel>(panel.Clone());
		const auto another = std::dynamic_pointer_cast<CalculationSummaryPanel>(panel.Clone());
		ASSERT_NE(clone, nullptr);
		ASSERT_NE(another, nullptr);
		EXPECT_NE(clone->Session(), summary);
		EXPECT_EQ(clone->Session()->Directory(), summary->Directory());
		EXPECT_NE(clone->GetTitle(), panel.GetTitle());
		EXPECT_NE(clone->GetTitle(), another->GetTitle());
		EXPECT_TRUE(clone->IsVisible());
		EXPECT_FALSE(panel.IsVisible());
		EXPECT_FALSE(clone->Session()->IsKsLevelSelected(1, 1022));
		EXPECT_TRUE(clone->Session()->IsKsLevelSelected(1, 1023));
		EXPECT_FALSE(clone->Session()->IsKsLevelSelected(2, 1023));
		clone->Session()->SetBandSelected(1022, true);
		clone->Session()->SetSpinSelected(2, true);
		EXPECT_FALSE(summary->IsKsLevelSelected(1, 1022));
		EXPECT_FALSE(summary->IsKsLevelSelected(2, 1023));
		clone->Session()->Update();
		EXPECT_EQ(updates, 0); // A clone must not copy the editor's registry-maintenance callback.
		summary->Update();
		EXPECT_EQ(updates, 1);
		clone->Session()->OpenDirectory(Path::FromUtf8("task103-other-fixture"));
		EXPECT_NE(clone->Session()->Directory(), summary->Directory());
	}

	TEST(LinkedAnalysisSessionTests, CalculationSummaryBandSelectionSurvivesReloadedBandWindows)
	{
		CalculationSummarySession session(WeakRef<JobSystem>{}, false);
		session.SetAvailableBands({1022, 1023, 1024});
		EXPECT_TRUE(session.IsKsLevelSelected(1, 1022));
		EXPECT_TRUE(session.IsKsLevelSelected(2, 1022));
		session.SetBandSelected(1023, false);
		session.SetSpinSelected(2, false);
		session.SetAvailableBands({1022, 1023, 1024});
		EXPECT_FALSE(session.IsKsLevelSelected(1, 1023));
		session.SetAvailableBands({1023, 1024, 1025});
		EXPECT_FALSE(session.BandSelection().contains(1022));
		EXPECT_FALSE(session.IsKsLevelSelected(1, 1023));
		EXPECT_TRUE(session.IsKsLevelSelected(1, 1025));
		EXPECT_FALSE(session.IsKsLevelSelected(2, 1025));
		session.SetBandSelected(9999, true);
		EXPECT_FALSE(session.BandSelection().contains(9999));
		session.SetSpinSelected(0, false);
		session.SetSpinSelected(3, false);
		EXPECT_FALSE(session.IsKsLevelSelected(0, 1025));
		EXPECT_FALSE(session.IsKsLevelSelected(3, 1025));
		session.SetAvailableBands({});
		EXPECT_TRUE(session.BandSelection().empty());
	}

	TEST(LinkedAnalysisSessionTests, CalculationSummaryFocusAndCloseAffectOnlyTheirInstance)
	{
		PanelRegistry registry;
		const auto first = CreateRef<CalculationSummarySession>(WeakRef<JobSystem>{}, false);
		const auto second = CreateRef<CalculationSummarySession>(WeakRef<JobSystem>{}, false);
		first->OpenDirectory(Path::FromUtf8("task103-first-fixture"));
		second->OpenDirectory(Path::FromUtf8("task103-second-fixture"));
		const auto firstId = registry.Add<CalculationSummaryPanel>(first, "Calculation Summary");
		const auto secondId = registry.Add<CalculationSummaryPanel>(second, "Calculation Summary: second##2", true);
		const auto panel = std::dynamic_pointer_cast<CalculationSummaryPanel>(registry.Get(firstId).lock());
		const auto other = std::dynamic_pointer_cast<CalculationSummaryPanel>(registry.Get(secondId).lock());
		ASSERT_NE(panel, nullptr);
		ASSERT_NE(other, nullptr);
		EXPECT_LT(panel->FocusOrder(), other->FocusOrder());
		panel->RequestFocus();
		EXPECT_GT(panel->FocusOrder(), other->FocusOrder());
		EXPECT_TRUE(panel->IsVisible());
		panel->Close();
		EXPECT_TRUE(panel->IsClosed());
		EXPECT_FALSE(panel->IsVisible());
		EXPECT_EQ(panel->Clone(), nullptr);
		EXPECT_TRUE(other->IsVisible());
		EXPECT_FALSE(other->IsClosed());
		EXPECT_TRUE(registry.Contains(firstId)); // Removal is deferred until the main-thread update.
		EXPECT_TRUE(registry.Remove(firstId));
		EXPECT_FALSE(registry.Contains(firstId));
		EXPECT_TRUE(registry.Contains(secondId));
	}

	TEST(LinkedAnalysisSessionTests, FlushSavesOnceAndProjectReplacementDropsPendingEdits)
	{
		DefectEnergeticsSession session(WeakRef<JobSystem>{});
		int saves = 0;
		session.SetSaveHandler([&](const EnergeticsFile &) { ++saves; });
		session.SetPersistedState({});
		session.Changed();
		session.Changed();
		session.Update();
		EXPECT_EQ(saves, 0);
		session.FlushChanges();
		session.FlushChanges();
		EXPECT_EQ(saves, 1);
		session.Changed();
		session.SetPersistedState({});
		session.FlushChanges();
		EXPECT_EQ(saves, 1);
	}

	TEST(LinkedAnalysisSessionTests, CompletedJobCommitsOnceAndInputChangesInvalidateIt)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		DefectEnergeticsSession session(jobs);
		session.SetPersistedState(VacancyInputs());
		session.StartConcentrations();
		session.StartConcentrations();
		ASSERT_EQ(jobs->GetAllJobs().size(), 1u);
		ASSERT_TRUE(WaitForConcentrations(jobs));
		EXPECT_TRUE(session.Concentrations().solutions.empty()); // Only the main thread accepts results.
		session.Update();
		ASSERT_TRUE(session.ConcentrationsCurrent()) << session.Concentrations().error;
		ASSERT_EQ(session.Concentrations().solutions.size(), 1u);
		session.Update();
		EXPECT_EQ(session.Concentrations().solutions.size(), 1u);
		session.InvalidateInputs();
		session.FlushChanges();
		EXPECT_FALSE(session.ConcentrationsCurrent());
	}

	TEST(LinkedAnalysisSessionTests, DefectSetEditsSaveOnceAtFlushAndDoNotDuplicateRoots)
	{
		DefectEnergeticsSession session(WeakRef<JobSystem>{});
		EnergeticsFile file;
		file.root = Path::FromUtf8("O:/data");
		session.SetPersistedState(file);
		int saves = 0;
		session.SetSaveHandler([&](const EnergeticsFile &saved) { file = saved; ++saves; });
		session.HideDefect("GeV");
		session.HideDefect("GeV");
		session.HideDefect("");
		session.AddDefectFolder(Path::FromUtf8("O:/data/GeV"));
		session.AddDefectFolder(Path::FromUtf8("O:/data"));
		session.AddDefectFolder({});
		session.AddDefectFolder(Path::FromUtf8("X:/other/GeV"));
		session.AddDefectFolder(Path::FromUtf8("X:/other/./GeV/"));
		EXPECT_EQ(saves, 0);
		session.FlushChanges();
		EXPECT_EQ(saves, 1);
		EXPECT_EQ(file.hiddenDefects, (std::vector<std::string>{"GeV"}));
		EXPECT_EQ(file.extraRoots, (std::vector<Path>{Path::FromUtf8("X:/other/GeV")}));
		session.RestoreDefect("GeV");
		session.RemoveExtraRoot(Path::FromUtf8("X:/other/GeV/"));
		session.FlushChanges();
		EXPECT_EQ(saves, 2);
		EXPECT_TRUE(file.hiddenDefects.empty());
		EXPECT_TRUE(file.extraRoots.empty());
		session.RestoreDefect("GeV");
		session.RemoveExtraRoot(Path::FromUtf8("X:/other/GeV"));
		session.FlushChanges();
		EXPECT_EQ(saves, 2);
	}

	TEST(LinkedAnalysisSessionTests, HidingADefectDropsItsConcentrationSnapshotAtFlush)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		DefectEnergeticsSession session(jobs);
		session.SetPersistedState(VacancyInputs());
		session.StartConcentrations();
		ASSERT_TRUE(WaitForConcentrations(jobs));
		session.Update();
		ASSERT_TRUE(session.ConcentrationsCurrent());
		ASSERT_EQ(session.Concentrations().defects, (std::vector<std::string>{"vacancy"}));
		session.HideDefect("vacancy");
		EXPECT_FALSE(session.ConcentrationsCurrent());
		EXPECT_FALSE(session.Concentrations().solutions.empty()); // Deferred while views hold references.
		session.FlushChanges();
		EXPECT_TRUE(session.Concentrations().defects.empty());
		EXPECT_TRUE(session.Concentrations().solutions.empty());
		session.StartConcentrations();
		EXPECT_EQ(jobs->GetAllJobs().size(), 1u); // Hidden states cannot start a new concentration job.
	}

	TEST(LinkedAnalysisSessionTests, ScanDeduplicatesOverlappingRootsReusesCacheAndKeepsOutsideReferences)
	{
		ScanWorkspace workspace;
		EnergeticsFile file;
		file.root = workspace.root / "primary";
		const Path extra = workspace.root / "extra";
		file.extraRoots = {extra, extra / "Vacancy"};
		for (const Path &directory : {file.root / "Vacancy" / "q0", extra / "Vacancy" / "q0"})
		{
			ASSERT_TRUE(FileSystem::CreateDirectories(directory.Native()));
			{
				std::ofstream outcar((directory / "OUTCAR").Native(), std::ios::binary);
				outcar << " TITEL = PAW_PBE C\n ions per type = 1\n ZVAL = 4\n NELECT = 4\n"
					" free  energy   TOTEN = -10\n General timing and accounting\n";
			}
			file.runs.push_back(VaspRunRecordIO::Read(directory));
			ASSERT_TRUE(file.runs.back().error.empty());
			file.runs.back().method.encut = 1234; // Only the cache has this marker.
		}
		VaspRunRecord missing;
		missing.directory = extra / "deleted" / "q0";
		file.runs.push_back(missing);
		VaspRunRecord reference;
		reference.directory = workspace.root / "reference";
		reference.error = "hand-added reference";
		file.runs.push_back(reference);
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		DefectEnergeticsSession session(jobs);
		session.SetPersistedState(file);
		session.SetSaveHandler([&](const EnergeticsFile &saved) { file = saved; });
		session.AddDefectFolder(file.root); // An already covered folder still rescans all roots.
		ASSERT_EQ(jobs->GetAllJobs().size(), 1u);
		ASSERT_TRUE(WaitForConcentrations(jobs));
		EXPECT_EQ(file.runs.size(), 4u); // A job cannot commit state.
		session.Update();
		session.FlushChanges();
		ASSERT_EQ(file.runs.size(), 3u);
		for (const auto &run : file.runs)
			if (run.directory == reference.directory)
				EXPECT_EQ(run.error, reference.error);
			else
				EXPECT_EQ(run.method.encut, 1234);
		EXPECT_EQ(std::count_if(file.runs.begin(), file.runs.end(), [&](const auto &run) {
			return run.directory == extra / "Vacancy" / "q0";
		}), 1);
		EXPECT_EQ(std::count_if(file.runs.begin(), file.runs.end(), [&](const auto &run) {
			return run.directory == reference.directory;
		}), 1);
		EXPECT_EQ(file.extraRoots.size(), 2u);
	}

	TEST(LinkedAnalysisSessionTests, StaleAndPreviousProjectJobsCannotReplaceResults)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		DefectEnergeticsSession session(jobs);
		session.SetPersistedState(VacancyInputs());
		session.StartConcentrations();
		ASSERT_TRUE(WaitForConcentrations(jobs));
		session.InvalidateInputs();
		session.Update();
		EXPECT_FALSE(session.ConcentrationsCurrent());
		EXPECT_TRUE(session.Concentrations().solutions.empty());

		session.StartConcentrations();
		ASSERT_TRUE(WaitForConcentrations(jobs));
		session.SetPersistedState({});
		session.Update();
		session.FlushChanges();
		EXPECT_TRUE(session.Concentrations().solutions.empty());
		EXPECT_FALSE(session.ConcentrationsCurrent());
		EXPECT_FALSE(session.BulkBandEdges().has_value());
	}
} // namespace DefectStudio::Tests
