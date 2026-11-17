#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include "Core/Utils/Uuid.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"
#include "Presentation/Figures/FigureSpec.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	class PhononPlotDataExperimentalTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::current_path()) / ("ds_experimental_test_" + ToString(GenerateUuid()));
		Ref<JobSystem> jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		PhononPanelSession session{jobs};
		ExperimentalSpectrumSource source;
		void SetUp() override
		{
			std::filesystem::create_directories(root.Native());
			source = {root / "pl.txt", 0, 1, "eV", "measured"};
			Write("energy;PL;other\n1;0;0\n2;2;4\n3;0;0\n");
			(void)session.AddCase("A");
		}
		void TearDown() override
		{
			session.SetPersistedState({}, {});
			jobs->Shutdown();
			std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored);
		}
		void Write(const std::string &text) { std::ofstream out(source.file.Native()); out << text; }
		bool Load(const ExperimentalSpectrumSource &request)
		{
			(void)session.Experimental(request);
			return WaitUntil([&] { return !session.Experimental(request).loading; }, Time::Milliseconds(5000));
		}
		void Computed(double zpl = 2.0)
		{
			PhononAnalysisResult result;
			result.label = "computed"; result.directory = root / "computed";
			result.plPhotonEnergyEv = {1, 2, 3}; result.plComputed = {0, 2, 0}; result.zplEv.computed = zpl;
			session.Settings().resultDirectory = result.directory;
			session.SetResult(result); session.SetPlZpl("computed");
		}
		void WriteComputed(const Path &directory)
		{
			std::filesystem::create_directories(directory.Native());
			std::ofstream summary((directory / "summary.json").Native());
			summary << R"({"schema":"ds.phonon_vibronic.v1","label":"comparison",
				"s_total":1,"debye_waller":0.36,"stable":true,"sigma_meV":6,"gamma_meV":2,
				"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
				"zpl_eV":{"computed":2,"computed_spin_purified":null,"experimental":null}})";
			std::ofstream pl((directory / "pl.tsv").Native());
			pl << "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\n"
				"1\t0\tnan\tnan\n2\t2\tnan\tnan\n3\t0\tnan\tnan\n";
		}
	};

	TEST_F(PhononPlotDataExperimentalTests, EnergyAndLossAxesUseOwnZplThenTheSelectedComputedFallback)
	{
		ASSERT_TRUE(Load(source));
		ASSERT_TRUE(session.AddExperimentalSpectrum(source));
		Computed();
		auto plot = BuildPlPlotData(session, root);
		ASSERT_EQ(plot.series.size(), 2u);
		EXPECT_EQ(plot.series.back().x, (std::vector<double>{1, 2, 3}));
		EXPECT_EQ(plot.series.back().y, (std::vector<double>{0, 2, 0}));
		EXPECT_EQ(plot.series.back().label, "measured (eksp.)");
		EXPECT_EQ(plot.series.back().id, "pl/exp/" + session.ActiveCaseId() + "/0");
		EXPECT_EQ(PhononPlotLegendId(plot.series.back()), plot.series.back().id);
		EXPECT_EQ(PhononPlotStyleKey(plot.series.back()), plot.series.back().id);
		session.Settings().plAxis = "zpl_loss";
		plot = BuildPlPlotData(session, root);
		EXPECT_EQ(plot.series.back().x, (std::vector<double>{-1000, 0, 1000}));
		EXPECT_NE(PhononExperimentalSeriesNote(session, plot.series.back()).find("ZPL krzywej obliczonej"), std::string::npos);
		session.Settings().cases[0].experimentalSpectra[0].zplEv = 2.1;
		plot = BuildPlPlotData(session, root);
		EXPECT_NEAR(plot.series.back().x[1], 100, 1e-9);
		EXPECT_NE(PhononExperimentalSeriesNote(session, plot.series.back()).find("ZPL widma"), std::string::npos);
		EXPECT_TRUE(std::is_sorted(plot.series.back().x.begin(), plot.series.back().x.end()));
	}

	TEST_F(PhononPlotDataExperimentalTests, MaxAndAreaNormalizationMatchComputedCurvesAndDisplayedUnits)
	{
		ASSERT_TRUE(Load(source)); ASSERT_TRUE(session.AddExperimentalSpectrum(source)); Computed();
		for (const auto axis : {"energy", "zpl_loss"})
			for (const auto normalization : {"none", "max", "area"})
			{
				session.Settings().plAxis = axis; session.Settings().plNormalization = normalization;
				const auto plot = BuildPlPlotData(session, root);
				ASSERT_EQ(plot.series.size(), 2u);
				EXPECT_EQ(plot.series[0].x, plot.series[1].x);
				EXPECT_EQ(plot.series[0].y, plot.series[1].y);
				const double peak = std::string_view(normalization) == "none" ? 2 : std::string_view(normalization) == "max" ? 1 :
					std::string_view(axis) == "energy" ? 1 : 0.001;
				EXPECT_NEAR(plot.series.back().y[1], peak, 1e-12);
			}
		const auto plot = BuildPlPlotData(session, root);
		const auto columns = PlotDataColumns(plot, root);
		ASSERT_EQ(columns.size(), 4u);
		EXPECT_EQ(columns.back().unit, "meV^{-1}");
		EXPECT_NE(FormatDataTable(columns, {}).find("measured (eksp.)"), std::string::npos);
		FigureSpec spec; spec.name = "experimental"; spec.plot = plot;
		FigureSpec parsed; std::string error;
		ASSERT_TRUE(ParseFigureSpec(SerializeFigureSpec(spec), parsed, error)) << error;
		ASSERT_EQ(parsed.plot.series.size(), 2u);
		EXPECT_EQ(parsed.plot.series.back().x, plot.series.back().x);
		EXPECT_EQ(parsed.plot.series.back().y, plot.series.back().y);
	}

	TEST_F(PhononPlotDataExperimentalTests, StandaloneMeasuredSpectrumAndMissingZplLegendReason)
	{
		ASSERT_TRUE(Load(source)); ASSERT_TRUE(session.AddExperimentalSpectrum(source));
		auto plot = BuildPlPlotData(session, root);
		ASSERT_EQ(plot.series.size(), 1u); EXPECT_EQ(plot.series[0].x.size(), 3u);
		session.Settings().plAxis = "zpl_loss";
		plot = BuildPlPlotData(session, root);
		ASSERT_EQ(plot.series.size(), 1u);
		EXPECT_TRUE(plot.series[0].x.empty()); EXPECT_TRUE(plot.series[0].y.empty());
		EXPECT_NE(PhononExperimentalSeriesNote(session, plot.series[0]).find("Brak ZPL"), std::string::npos);
		session.Settings().cases[0].experimentalSpectra[0].zplEv = 2;
		EXPECT_EQ(BuildPlPlotData(session, root).series[0].x, (std::vector<double>{-1000, 0, 1000}));
	}

	TEST_F(PhononPlotDataExperimentalTests, NormalizationUsesTheEntireMeasuredRangeBeforeAnyPlotLimits)
	{
		Write("0 20\n1 0\n2 4\n3 0\n4 10\n");
		ASSERT_TRUE(Load(source)); ASSERT_TRUE(session.AddExperimentalSpectrum(source)); Computed();
		session.Settings().plNormalization = "max";
		auto plot = BuildPlPlotData(session, root);
		ASSERT_EQ(plot.series.size(), 2u);
		EXPECT_EQ(plot.series.back().x.front(), 0); EXPECT_EQ(plot.series.back().x.back(), 4);
		EXPECT_NEAR(plot.series.back().y[2], 4.0 / 20.0, 1e-12);
		session.Settings().plNormalization = "area";
		plot = BuildPlPlotData(session, root);
		EXPECT_NEAR(plot.series.back().y[2], 4.0 / 19.0, 1e-12);
		session.Settings().plAxis = "zpl_loss";
		plot = BuildPlPlotData(session, root);
		EXPECT_NEAR(plot.series.back().y[2], 4.0 / 19000.0, 1e-12);
	}

	TEST_F(PhononPlotDataExperimentalTests, ComparedCasesInheritTheirColorAndHiddenCasesOrSpectraContributeNothing)
	{
		ASSERT_TRUE(Load(source)); ASSERT_TRUE(session.AddExperimentalSpectrum(source)); Computed();
		const auto a = session.ActiveCaseId();
		WriteComputed(root / "computed");
		const auto b = session.DuplicateCase(a);
		ASSERT_FALSE(b.empty());
		Computed();
		auto result = *session.Result(); result.directory = root / "computed_b";
		session.Settings().resultDirectory = result.directory; session.SetResult(result);
		session.AddDataset({AnalysisDatasetKind::Phonons, root / "computed", "comparison", "ds.phonon_vibronic.v1"});
		session.AttachDataset(a, root / "computed");
		ASSERT_EQ(session.Cases()[1].experimentalSpectra.size(), 1u);
		EXPECT_EQ(BuildPlPlotData(session, root).series.size(), 2u); // B computed + B measured, A is not compared.
		session.Settings().comparedDirectories = {root / "computed"};
		const std::vector<glm::vec4> colors = {glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 0, 1)};
		auto plot = BuildPlPlotData(session, root, {}, colors);
		ASSERT_EQ(plot.series.size(), 4u);
		EXPECT_EQ(plot.series[0].style.color, plot.series[2].style.color);
		EXPECT_EQ(plot.series[1].style.color, plot.series[3].style.color);
		EXPECT_NE(plot.series[2].style.color, plot.series[3].style.color);
		EXPECT_EQ(plot.series[2].style.lineStyle, 1);
		session.SetCaseHidden(a, true);
		EXPECT_EQ(BuildPlPlotData(session, root).series.size(), 2u);
		session.Settings().cases[1].experimentalSpectra[0].visible = false;
		EXPECT_EQ(BuildPlPlotData(session, root).series.size(), 1u);
		session.SetCaseHidden(b, true);
		EXPECT_TRUE(BuildPlPlotData(session, root).series.empty());
	}

	TEST_F(PhononPlotDataExperimentalTests, FirstVisibleSpectrumZplIsUsedWithoutFallingThroughToLaterEntries)
	{
		session.Settings().cases[0].experimentalSpectra = {{source.file, 0, 1, "eV", "hidden", 1.9, false},
			{source.file, 0, 1, "eV", "first", std::nullopt, true}, {source.file, 0, 1, "eV", "later", 2.1, true}};
		auto rows = BuildPhononComparisonRows(session, root);
		ASSERT_EQ(rows.size(), 1u); EXPECT_FALSE(rows[0].experimentalZplEv);
		session.Settings().cases[0].experimentalSpectra[1].zplEv = 2.0;
		rows = BuildPhononComparisonRows(session, root);
		EXPECT_EQ(rows[0].experimentalZplEv, 2.0);
		const auto columns = PhononComparisonColumns(rows, 0.9);
		EXPECT_EQ(columns.back().name, "ZPL eksp."); EXPECT_EQ(columns.back().values[0], 2.0);
	}

	TEST_F(PhononPlotDataExperimentalTests, CacheSeparatesColumnsAndRefreshesErrorsOnMtimeChange)
	{
		ASSERT_TRUE(Load(source));
		const auto &loaded = session.Experimental(source);
		ASSERT_TRUE(loaded.spectrum); EXPECT_EQ(loaded.table.names[1], "PL");
		EXPECT_EQ(loaded.table.delimiter, ';'); EXPECT_EQ(loaded.table.skippedLines, 1u);
		EXPECT_NE(loaded.preview.find("energy;PL;other"), std::string::npos);
		const auto count = jobs->GetAllJobs().size();
		ASSERT_TRUE(Load(source)); EXPECT_EQ(jobs->GetAllJobs().size(), count);
		auto other = source; other.yColumn = 2;
		ASSERT_TRUE(Load(other)); ASSERT_TRUE(session.Experimental(other).spectrum);
		EXPECT_EQ(session.Experimental(other).spectrum->intensity[1], 4);
		other.yColumn = 9; ASSERT_TRUE(Load(other)); EXPECT_FALSE(session.Experimental(other).error.empty());
		const auto modified = std::filesystem::last_write_time(source.file.Native());
		Write("broken\n"); std::filesystem::last_write_time(source.file.Native(), modified + std::chrono::seconds(2));
		ASSERT_TRUE(Load(source)); EXPECT_FALSE(session.Experimental(source).spectrum); EXPECT_FALSE(session.Experimental(source).error.empty());
		Write("1 1\n2 2\n"); std::filesystem::last_write_time(source.file.Native(), modified + std::chrono::seconds(4));
		ASSERT_TRUE(Load(source)); ASSERT_TRUE(session.Experimental(source).spectrum); EXPECT_TRUE(session.Experimental(source).error.empty());
	}

	TEST_F(PhononPlotDataExperimentalTests, ProjectResetDiscardsPendingLoadsAndPeakUsesTheComputedZplWindow)
	{
		(void)session.Experimental(source);
		session.SetPersistedState({}, root / "next");
		EXPECT_TRUE(session.Cases().empty());
		session.PollExperimentalSpectra(); EXPECT_TRUE(session.Cases().empty());
		Write("1 100\n1.9 2\n2.05 8\n2.2 20\n");
		ASSERT_TRUE(Load(source)); ASSERT_TRUE(session.Experimental(source).spectrum);
		EXPECT_DOUBLE_EQ(SpectrumPeakEnergyEv(*session.Experimental(source).spectrum, 2.0 - 0.15, 2.0 + 0.15), 2.05);
	}

	TEST_F(PhononPlotDataExperimentalTests, WorkerCompletionDoesNotPublishBeforeMainThreadPoll)
	{
		const auto gate = CreateRef<Gate>();
		(void)jobs->Submit(CreateRef<BlockingJob>(gate));
		const auto &pending = session.Experimental(source);
		const bool started = pending.loading;
		gate->Open();
		ASSERT_TRUE(started);
		ASSERT_TRUE(WaitUntil([&] {
			const auto all = jobs->GetAllJobs();
			return std::all_of(all.begin(), all.end(), [](const auto &job) { return job.status == JobStatus::Completed; });
		}, Time::Milliseconds(5000)));
		EXPECT_TRUE(pending.loading); EXPECT_FALSE(pending.spectrum);
		session.PollExperimentalSpectra();
		EXPECT_FALSE(pending.loading); ASSERT_TRUE(pending.spectrum);
		EXPECT_EQ(pending.spectrum->energyEv, (std::vector<double>{1, 2, 3}));
	}
}
