#include <gtest/gtest.h>
#include <algorithm>

#include "Presentation/Panels/CalculationSummaryPanel.hpp"
#include "Presentation/Panels/DefectEnergeticsPanel.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
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

		const auto summary = CreateRef<CalculationSummarySession>(WeakRef<JobSystem>{});
		CalculationSummaryPanel ks(summary, CalculationSummaryPanel::View::KsLevels, "Poziomy KS");
		const auto ksClone = std::dynamic_pointer_cast<CalculationSummaryPanel>(ks.Clone());
		ASSERT_NE(ksClone, nullptr);
		EXPECT_EQ(ksClone->Session(), summary);
		int shown = 0;
		summary->SetShowViewsHandler([&](const bool visible) { shown += visible ? 1 : 0; });
		summary->OpenDirectory(Path::FromUtf8("task101-missing-fixture"));
		EXPECT_EQ(shown, 1);
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
