#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include "Core/Utils/Uuid.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		PhononAnalysisResult TwoModes(const Path &directory = Path{})
		{
			PhononAnalysisResult result;
			result.directory = directory; result.label = "active";
			result.symbols = {"B", "N"}; result.participation = {0.9f, 0.1f, 0.5f, 0.5f};
			result.modes = {{4, 50, "A1", 1.2, 5, 0.3}, {7, 100, "E", 2, 2.5, 0.1}};
			result.spectralEnergyMeV = {-10, 0, 50, 100, 120, 200};
			result.spectralDensityPerMeV = {0, 0, 0.03, 0.01, 0, 0};
			result.zplEv.computed = 2; result.zplEv.experimental = 2.1; result.zplEv.formula = 2.2;
			result.plPhotonEnergyEv = {0, 1, 2, 3, 4};
			result.plComputed = {0, 0.2, 1, 0, 0};
			result.plExperimental = {0, 0.4, 0.8, 0, 0};
			result.plFormula = {0, 0.6, 0.7, 0, 0};
			return result;
		}

		CcdAnalysisResult CcdData()
		{
			CcdAnalysisResult result;
			result.label = "CCD"; result.deltaQ = 1; result.zplEv = 2;
			result.curves.q = {-2, -1, 0, 1, 2, 3};
			result.curves.vGsEv = {4, 1, 0, 1, 4, 9};
			result.curves.vEsEv = {11, 6, 3, 2, 3, 6};
			result.points = {{"gs", 0, 0, 0, "ok"}, {"gs", 0.5, 0.5, std::nullopt, "pending"},
				{"es", 1, 1, 2, "ok"}, {"triplet", 0, 0, 2.5, "ok"}, {"triplet", 1, 1, std::nullopt, "missing"}};
			result.levels.gsMeV = {100, 300}; result.levels.esMeV = {200};
			result.wavefunctions.q = {-2, -1, 0, 1, 2, 3};
			result.wavefunctions.gs = {{0, 0.1, 0.5, -0.1, 0, 0}, {0, 0.2, 0.3, -0.2, 0, 0}};
			result.wavefunctions.es = {{0, 0, 0.1, 0.5, 0.1, 0}};
			result.pl1d.energyEv = {0, 1, 2, 3, 4};
			result.pl1d.numerov = {0, 0.2, 1, 0, 0};
			result.pl1d.harmonic = {0, 0.3, 0.8, 0, 0};
			result.pl1d.phonon1d = {0, 0.4, 0.6, 0, 0};
			return result;
		}

		struct Workspace
		{
			Path root = Path::FromResolved(std::filesystem::temp_directory_path()) / ("ds_plot_data_" + ToString(GenerateUuid()));
			~Workspace() { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
			void Write(const Path &directory, const std::string &filename, const std::string &text)
			{
				std::filesystem::create_directories(directory.Native());
				std::ofstream file((directory / filename).Native());
				file << text;
			}
			Path Comparison()
			{
				const auto directory = root / "compared";
				Write(directory, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"comparison",
					"s_total":1,"debye_waller":0.36,"stable":true,"sigma_meV":6,"gamma_meV":2,
					"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
					"zpl_eV":{"computed":2,"computed_spin_purified":null,"experimental":2.1,"formula":2.2}})");
				Write(directory, "spectral_function.tsv", "energy_meV\tS_E_per_meV\n0\t0\n50\t0.1\n100\t0.05\n200\t0\n");
				Write(directory, "pl.tsv", "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula\n1\t0.2\tnan\t0.4\t0.6\n2\t1\tnan\t0.8\t0.7\n3\t0\tnan\t0\t0\n");
				return directory;
			}
		};
	}

	TEST(PhononPlotDataTests, BetaGroupsFilteredModesAndPreservesSelectedLineAndHiddenStyles)
	{
		PhononPanelSession session;
		const auto source = Path::FromUtf8("C:/project/phonons");
		session.Settings().resultDirectory = source;
		session.SetResult(TwoModes(source));
		session.Settings().seriesStyles["beta/other"].visible = false;
		const std::vector<glm::vec4> colors = {glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 0, 1)};
		const auto plot = BuildBetaPlotData(session, source.parent_path(), {}, colors);
		ASSERT_EQ(plot.series.size(), 3u);
		EXPECT_EQ(plot.series[0].label, "inne irreps");
		EXPECT_EQ(plot.series[0].x, std::vector<double>{100}); EXPECT_EQ(plot.series[0].y, std::vector<double>{2.5});
		EXPECT_FALSE(plot.series[0].style.visible);
		EXPECT_EQ(plot.series[1].label, "A1"); EXPECT_EQ(plot.series[1].kind, PlotSeriesKind::Stems);
		EXPECT_EQ(plot.series[1].x, std::vector<double>{50}); EXPECT_EQ(plot.series[1].y, std::vector<double>{5});
		EXPECT_EQ(plot.series[1].source, source); EXPECT_EQ(plot.series[1].style.color, colors[1]);
		EXPECT_EQ(plot.series[2].kind, PlotSeriesKind::VerticalLine); EXPECT_EQ(plot.series[2].x, std::vector<double>{50});
		session.Settings().minHuangRhys = 0.2;
		EXPECT_TRUE(BuildBetaPlotData(session, {}).series[0].x.empty());
		session.Settings().irrepFilter = "E";
		EXPECT_TRUE(BuildBetaPlotData(session, {}).series[1].x.empty());
	}

	TEST(PhononPlotDataTests, SpectralLineUsesRightAxisAndTheSameGridTrimmingAsThePlot)
	{
		PhononPanelSession session;
		session.SetResult(TwoModes());
		const auto plot = BuildSPlotData(session, {});
		ASSERT_EQ(plot.series.size(), 4u);
		EXPECT_TRUE(plot.hasY2); EXPECT_EQ(plot.y2.unit, "meV^{-1}");
		EXPECT_EQ(plot.series[3].yAxis, 1);
		EXPECT_EQ(plot.series[3].x, (std::vector<double>{-10, 0, 50, 100, 120}));
		EXPECT_EQ(plot.series[3].y, (std::vector<double>{0, 0, 0.03, 0.01, 0}));
	}

	TEST(PhononPlotDataTests, PlUsesChosenCurveAndExistingFallback)
	{
		PhononPanelSession session;
		session.SetResult(TwoModes());
		session.SetPlZpl("formula");
		const auto formula = BuildPlPlotData(session, {});
		ASSERT_EQ(formula.series.size(), 1u);
		EXPECT_EQ(formula.series[0].x, (std::vector<double>{0, 1, 2, 3}));
		EXPECT_EQ(formula.series[0].y, (std::vector<double>{0, 0.6, 0.7, 0}));
		session.SetPlZpl("spin_purified");
		const auto fallback = BuildPlPlotData(session, {});
		EXPECT_EQ(fallback.series[0].y, (std::vector<double>{0, 0.2, 1, 0}));
	}

	TEST(PhononPlotDataTests, ComparedSpectraAddOneSourcedSeriesAndWorkWithoutAnActiveResult)
	{
		Workspace workspace;
		const auto source = workspace.Comparison();
		PhononPanelSession session;
		session.Settings().resultDirectory = workspace.root / "active";
		session.SetResult(TwoModes());
		session.AddDataset({AnalysisDatasetKind::Phonons, source, "comparison", "ds.phonon_vibronic.v1"});
		session.SetCompared(source, true);
		session.SetPlZpl("computed");
		const auto plot = BuildPlPlotData(session, workspace.root);
		ASSERT_EQ(plot.series.size(), 2u);
		EXPECT_EQ(plot.series.back().source, source); EXPECT_EQ(plot.series.back().id, "PL/compared");
		EXPECT_EQ(plot.series.back().label, "comparison"); EXPECT_EQ(PhononPlotLegendId(plot.series.back()), source.Utf8());
		EXPECT_EQ(PlotDataColumns(plot, workspace.root).back().comment, "compared");
		EXPECT_EQ(BuildSPlotData(session, workspace.root).series.size(), 5u);
		session.ClearResult();
		const auto comparedOnly = BuildPlPlotData(session, workspace.root);
		ASSERT_EQ(comparedOnly.series.size(), 1u);
		EXPECT_EQ(comparedOnly.series.front().source, source);
		EXPECT_EQ(comparedOnly.series.front().y, (std::vector<double>{0.2, 1, 0}));
	}

	TEST(PhononPlotDataTests, DispersionHasOneSeriesPerBranchAndNanAtEveryPathBreak)
	{
		PhononDispersion result;
		result.bandCount = 2; result.distance = {0, 1, 1, 2}; result.segment = {0, 0, 1, 1};
		result.bandsMeV = {{1, 2, 3, 4}, {10, 20, 30, 40}};
		const auto plot = BuildDispersionPlotData(result, {}, Path::FromUtf8("bands"));
		ASSERT_EQ(plot.series.size(), 2u);
		for (const auto &series : plot.series)
		{
			EXPECT_EQ(series.kind, PlotSeriesKind::Segments); ASSERT_EQ(series.x.size(), 5u);
			EXPECT_TRUE(std::isnan(series.x[2])); EXPECT_TRUE(std::isnan(series.y[2]));
			EXPECT_EQ(series.source, Path::FromUtf8("bands"));
		}
		EXPECT_EQ(plot.series[0].y.back(), 4); EXPECT_EQ(plot.series[1].y.back(), 40);
		const auto columns = PlotDataColumns(plot, {});
		ASSERT_EQ(columns.size(), 4u); EXPECT_TRUE(std::isnan(columns[2].values[2]));
		EXPECT_NE(FormatDataTable(columns, {}).find("\n\t\t\t\n"), std::string::npos);
	}

	TEST(PhononPlotDataTests, DispersionPickUsesPixelScalesAndReturnsTheWholeSelection)
	{
		PhononDispersion result;
		result.bandCount = 2; result.distance = {0, 1}; result.segment = {0, 1};
		result.bandsMeV = {{8, 0}, {30, 40}};
		result.qpoints = {{0, 0, 0}, {0.5, 0.25, 0}};
		const auto horizontal = PickDispersion(result, 0, 0, 1, 1);
		ASSERT_TRUE(horizontal); EXPECT_EQ(horizontal->row, 1u); EXPECT_EQ(horizontal->band, 0);
		EXPECT_EQ(horizontal->segment, 1); EXPECT_DOUBLE_EQ(horizontal->distance, 1);
		EXPECT_DOUBLE_EQ(horizontal->frequencyMeV, 0); EXPECT_EQ(horizontal->q, result.qpoints[1]);
		const auto vertical = PickDispersion(result, 0, 0, 0.01, 10);
		ASSERT_TRUE(vertical); EXPECT_EQ(vertical->row, 0u);
		const auto branch = PickDispersion(result, 1, 39, 0.01, 1);
		ASSERT_TRUE(branch); EXPECT_EQ(branch->row, 1u); EXPECT_EQ(branch->band, 1);
	}

	TEST(PhononPlotDataTests, DispersionPickRequiresQAndRejectsDistantOrInvalidCoordinates)
	{
		PhononDispersion result;
		result.bandCount = 1; result.distance = {0}; result.segment = {0}; result.bandsMeV = {{0}};
		EXPECT_FALSE(PickDispersion(result, 0, 0, 1, 1));
		result.qpoints = {{0, 0, 0}};
		EXPECT_TRUE(PickDispersion(result, 12, 0, 1, 1));
		EXPECT_FALSE(PickDispersion(result, 12.01, 0, 1, 1));
		EXPECT_FALSE(PickDispersion(result, 9, 9, 1, 1));
		EXPECT_FALSE(PickDispersion(result, 0, 0, 0, 1));
		EXPECT_FALSE(PickDispersion(result, 0, 0, 1, -1));
		EXPECT_FALSE(PickDispersion(result, std::numeric_limits<double>::quiet_NaN(), 0, 1, 1));
		EXPECT_FALSE(PickDispersion(result, 0, 0, 1, std::numeric_limits<double>::infinity()));
	}

	TEST(PhononPlotDataTests, DispersionSnapClampsToItsSegmentAtDuplicatedBreakDistances)
	{
		PhononDispersion result;
		result.distance = {0, 0.5, 1, 1, 1.5, 2}; result.segment = {0, 0, 0, 1, 1, 1};
		EXPECT_EQ(SnapToSegment(result, 0, -10), 0u); EXPECT_EQ(SnapToSegment(result, 0, 10), 2u);
		EXPECT_EQ(SnapToSegment(result, 1, -10), 3u); EXPECT_EQ(SnapToSegment(result, 1, 10), 5u);
		EXPECT_EQ(SnapToSegment(result, 0, 1), 2u); EXPECT_EQ(SnapToSegment(result, 1, 1), 3u);
		EXPECT_EQ(SnapToSegment(result, 0, 0.6), 1u); EXPECT_EQ(SnapToSegment(result, 1, 1.6), 4u);
		EXPECT_EQ(SnapToSegment(result, 2, 1), result.distance.size());
		EXPECT_EQ(SnapToSegment({}, 0, 0), 0u);
	}

	TEST(PhononPlotDataTests, ColumnConversionUsesLabelsUnitsVisibilityAndOneXForVerticalLines)
	{
		PlotData plot;
		plot.x.unit = "meV"; plot.y.unit = ""; plot.y2.unit = "meV^{-1}"; plot.hasY2 = true;
		plot.series = {{"s", "S###internal", PlotSeriesKind::Line, {1, 2}, {3, 4}, 1, Path::FromUtf8("C:/project/data")},
			{"stems", "A1", PlotSeriesKind::Stems, {10}, {0.2}}, {"selected", "Wybrany mod", PlotSeriesKind::VerticalLine, {10}, {}}};
		plot.series[1].style.visible = false;
		const auto columns = PlotDataColumns(plot, Path::FromUtf8("C:/project"));
		ASSERT_EQ(columns.size(), 3u);
		EXPECT_EQ(columns[0].name, "S x"); EXPECT_EQ(columns[0].unit, "meV"); EXPECT_EQ(columns[0].comment, "data");
		EXPECT_EQ(columns[1].name, "S y"); EXPECT_EQ(columns[1].unit, "meV^{-1}");
		EXPECT_EQ(columns[2].name, "Wybrany mod x"); EXPECT_EQ(columns[2].values, std::vector<double>{10});
		EXPECT_EQ(PlotDataColumns(plot, {}, false).size(), 5u);
	}

	TEST(PhononPlotDataTests, CcdSeparatesMarkersByBranchAndFillAndProjectsMissingEnergies)
	{
		PhononPanelSettings settings;
		settings.ccdLevels = 0;
		const std::vector<glm::vec4> colors = {glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 0, 1)};
		const auto result = CcdData();
		const auto plot = BuildCcdPlotData(result, settings, Path::FromUtf8("ccd"), colors);
		ASSERT_EQ(plot.series.size(), 7u);
		EXPECT_EQ(plot.series[2].marker.shape, PlotMarker::Shape::Circle); EXPECT_TRUE(plot.series[2].marker.filled);
		EXPECT_FALSE(plot.series[3].marker.filled); EXPECT_EQ(plot.series[3].y, std::vector<double>{0.5});
		EXPECT_EQ(plot.series[4].style.color, colors[1]);
		EXPECT_EQ(plot.series[5].marker.shape, PlotMarker::Shape::Diamond); EXPECT_TRUE(plot.series[5].marker.filled);
		EXPECT_FALSE(plot.series[6].marker.filled); EXPECT_EQ(plot.series[6].y, std::vector<double>{0});
		for (std::size_t i = 2; i < plot.series.size(); ++i)
		{
			EXPECT_EQ(plot.series[i].kind, PlotSeriesKind::Scatter); EXPECT_EQ(plot.series[i].marker.size, 5);
			EXPECT_EQ(PhononPlotStyleKey(plot.series[i]), "CCD/points");
		}
		EXPECT_EQ(CcdPlotPointEnergy(result, result.points[1]), 0.5);
	}

	TEST(PhononPlotDataTests, CcdLevelsAndWavefunctionsUseEnergyOffsetsProbabilityAndSharedVisibility)
	{
		PhononPanelSettings settings;
		settings.ccdWavefunctions = true; settings.ccdProbability = true; settings.ccdWavefunctionScale = 0.1;
		settings.seriesStyles["CCD/chi_gs"].visible = false;
		const auto result = CcdData();
		const auto plot = BuildCcdPlotData(result, settings, {});
		const auto wave = std::find_if(plot.series.begin(), plot.series.end(), [](const auto &series) { return series.id == "CCD/chi_gs/0"; });
		ASSERT_NE(wave, plot.series.end()); EXPECT_FALSE(wave->style.visible);
		const auto zero = std::find(wave->x.begin(), wave->x.end(), 0.0);
		ASSERT_NE(zero, wave->x.end()); EXPECT_NEAR(wave->y[zero - wave->x.begin()], 0.125, 1e-12);
		const auto level = std::find_if(plot.series.begin(), plot.series.end(), [](const auto &series) { return series.id == "CCD/levels/es/0"; });
		ASSERT_NE(level, plot.series.end()); EXPECT_EQ(level->y, (std::vector<double>{2.2, 2.2}));
		const auto range = PhononPanelSession::CcdLevelRange(result, true, 0);
		ASSERT_TRUE(range); EXPECT_EQ(level->x, (std::vector<double>{range->first, range->second}));
		EXPECT_EQ(PlotDataColumns(plot, {}, false).size() - PlotDataColumns(plot, {}).size(), 4u);
	}

	TEST(PhononPlotDataTests, Pl1dIncludesAllAvailableCurvesAndOptionalPhononOverlay)
	{
		PhononPanelSession session;
		session.SetResult(TwoModes()); session.SetPlZpl("formula");
		session.Settings().ccdComparePhononPl = true;
		session.Settings().selectedCcdDirectory = Path::FromUtf8("ccd");
		session.Settings().resultDirectory = Path::FromUtf8("phonons");
		const auto plot = BuildCcdPlPlotData(session, CcdData(), {});
		ASSERT_EQ(plot.series.size(), 4u);
		EXPECT_EQ(plot.series[0].label, "Numerov"); EXPECT_EQ(plot.series[1].label, "harmoniczne");
		EXPECT_EQ(plot.series[2].label, "fonon 1D"); EXPECT_EQ(plot.series[3].source, Path::FromUtf8("phonons"));
		EXPECT_EQ(plot.series[3].y, (std::vector<double>{0, 0.6, 0.7, 0}));
		EXPECT_EQ(plot.series[0].source, Path::FromUtf8("ccd"));
	}

	TEST(PhononPlotDataTests, Pl1dComparisonAddsOnlyNumerovWithItsSourceAndSharedRange)
	{
		Workspace workspace;
		const auto directory = workspace.root / "comparison-ccd";
		workspace.Write(directory, "summary.json", R"({"schema":"ds.ccd_analysis.v1","label":"comparison",
			"delta_Q_amu05_A":1,"zpl_eV":{"used":3}})");
		workspace.Write(directory, "pl_1d.tsv", "photon_energy_eV\tintensity_numerov\tintensity_harmonic\tintensity_phonon_1d\n1\t0.2\t0.3\t0.4\n2\t1\t0.8\t0.6\n3\t0.1\t0.2\t0.3\n4\t0\t0\t0\n");
		PhononPanelSession session;
		session.Settings().selectedCcdDirectory = workspace.root / "active-ccd";
		session.AddDataset({AnalysisDatasetKind::Ccd, directory, "comparison", "ds.ccd_analysis.v1"});
		session.SetCompared(directory, true);
		const auto plot = BuildCcdPlPlotData(session, CcdData(), workspace.root);
		ASSERT_EQ(plot.series.size(), 4u);
		EXPECT_EQ(plot.series.back().source, directory); EXPECT_EQ(plot.series.back().id, "PL1D/comparison-ccd");
		EXPECT_EQ(plot.series.back().y, (std::vector<double>{0.2, 1, 0.1, 0}));
		EXPECT_DOUBLE_EQ(plot.x.maximum, 3.15);
		EXPECT_EQ(plot.series.front().x, (std::vector<double>{0, 1, 2, 3, 4}));
	}

	TEST(PhononPlotDataTests, ModesExportContainsEveryTableColumnInTheFilteredSortedOrder)
	{
		PhononPanelSession session;
		session.SetResult(TwoModes());
		auto modes = session.VisibleModes();
		SortPhononModes(*session.Result(), modes, 1, false);
		const auto columns = PhononModesColumns(*session.Result(), modes, 0.9, {});
		ASSERT_EQ(columns.size(), 7u);
		EXPECT_EQ(columns[0].values, (std::vector<double>{7, 4})); EXPECT_EQ(columns[1].unit, "meV");
		EXPECT_EQ(columns[2].text, (std::vector<std::string>{"E", "A1"}));
		EXPECT_EQ(columns[3].values, (std::vector<double>{2.5, 5})); EXPECT_EQ(columns[6].name, "N(90%)");
		EXPECT_EQ(columns[6].values, (std::vector<double>{2, 1}));
		session.Settings().irrepFilter = "A1";
		const auto filtered = PhononModesColumns(*session.Result(), session.VisibleModes(), 0.9, {});
		EXPECT_EQ(filtered[0].values, std::vector<double>{4});
		EXPECT_NE(FormatDataTable(filtered, {}).find("\tA1\t"), std::string::npos);
	}

	TEST(PhononPlotDataTests, PlLossAxisUsesEachCurvesOwnZplAndBothPeaksAreAtZero)
	{
		Workspace workspace;
		const auto compared = workspace.Comparison();
		workspace.Write(compared, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"comparison",
			"s_total":1,"debye_waller":0.36,"stable":true,"sigma_meV":6,"gamma_meV":2,
			"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
			"zpl_eV":{"computed":2.5,"computed_spin_purified":null,"experimental":null}})");
		workspace.Write(compared, "pl.tsv", "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\n"
			"2.4\t0\tnan\tnan\n2.5\t3\tnan\tnan\n2.6\t0\tnan\tnan\n");
		PhononPanelSession session;
		const auto active = workspace.root / "active";
		session.Settings().resultDirectory = active;
		auto result = TwoModes(active);
		result.zplEv.computed = 1.9; result.plPhotonEnergyEv = {1.8, 1.9, 2.0}; result.plComputed = {0, 2, 0};
		session.SetResult(result); session.SetPlZpl("computed"); session.Settings().plAxis = "zpl_loss"; session.Settings().plNormalization = "max";
		session.AddDataset({AnalysisDatasetKind::Phonons, compared, "comparison", "ds.phonon_vibronic.v1"});
		session.SetCompared(compared, true);
		const auto plot = BuildPlPlotData(session, workspace.root);
		ASSERT_EQ(plot.series.size(), 2u); EXPECT_EQ(plot.x.unit, "meV"); EXPECT_EQ(plot.x.labelTex, "E_{ZPL} - E");
		for (const auto &series : plot.series)
		{
			const auto peak = std::max_element(series.y.begin(), series.y.end()) - series.y.begin();
			EXPECT_NEAR(series.x[peak], 0.0, 1e-10); EXPECT_DOUBLE_EQ(series.y[peak], 1.0);
			EXPECT_TRUE(std::is_sorted(series.x.begin(), series.x.end()));
		}
		session.ClearResult();
		EXPECT_EQ(BuildPlPlotData(session, workspace.root).series.size(), 1u);
	}

	TEST(PhononPlotDataTests, PlNormalizationUsesTheFullGridBeforeTrimmingAndTheDisplayedAxisUnits)
	{
		PhononPanelSession session;
		auto result = TwoModes();
		result.plComputed = {0, 2, 4, 6, 20};
		session.SetResult(result); session.SetPlZpl("computed"); session.Settings().plNormalization = "max";
		const auto maximum = BuildPlPlotData(session, {});
		ASSERT_EQ(maximum.series.size(), 1u); ASSERT_EQ(maximum.series[0].y.size(), 4u);
		EXPECT_DOUBLE_EQ(maximum.series[0].y[2], 4.0 / 20.0);
		EXPECT_EQ(maximum.series[0].x.back(), 3.0);
		session.Settings().plNormalization = "area";
		const auto area = BuildPlPlotData(session, {});
		EXPECT_NEAR(area.series[0].y[2], 4.0 / 22.0, 1e-12); EXPECT_EQ(area.y.unit, "eV^{-1}");
		session.Settings().plAxis = "zpl_loss";
		const auto loss = BuildPlPlotData(session, {});
		const auto zero = std::find(loss.series[0].x.begin(), loss.series[0].x.end(), 0.0);
		ASSERT_NE(zero, loss.series[0].x.end());
		EXPECT_NEAR(loss.series[0].y[zero - loss.series[0].x.begin()], 4.0 / 22000.0, 1e-12);
		EXPECT_EQ(PlotDataColumns(loss, {})[1].unit, "meV^{-1}");
		EXPECT_NE(FormatDataTable(PlotDataColumns(loss, {}), {}).find("0.000181818182"), std::string::npos);
	}

	TEST(PhononPlotDataTests, PlFallbackRecordsSourceAndLabelsItWithoutChangingIdentityOrStyle)
	{
		PhononPanelSession session;
		auto result = TwoModes(Path::FromUtf8("C:/project/data"));
		session.Settings().resultDirectory = result.directory; session.SetResult(result);
		session.SetPlZpl("computed");
		const auto requested = BuildPlPlotData(session, Path::FromUtf8("C:/project"));
		session.Settings().seriesStyles[requested.series[0].id].visible = false;
		session.SetPlZpl("spin_purified"); session.Settings().plAxis = "zpl_loss";
		const auto fallback = BuildPlPlotData(session, Path::FromUtf8("C:/project"));
		const auto selected = SelectPhononPlCurve(result, "spin_purified");
		EXPECT_EQ(selected.choice, "computed"); EXPECT_EQ(selected.zplEv, 2.0);
		EXPECT_EQ(fallback.series[0].label, "active (ZPL: obliczone)");
		EXPECT_EQ(fallback.series[0].id, requested.series[0].id); EXPECT_FALSE(fallback.series[0].style.visible);
		EXPECT_EQ(PhononPlotLegendId(fallback.series[0]), PhononPlotLegendId(requested.series[0]));
		result.plComputed.clear(); result.zplEv.spinPurified = 2.3; result.plSpinPurified = result.plExperimental;
		session.SetResult(result); session.SetPlZpl("computed");
		EXPECT_EQ(BuildPlPlotData(session, {}).series[0].label, "active (ZPL: po korekcji spinowej)");
		result.zplEv.spinPurified.reset(); session.SetResult(result);
		EXPECT_TRUE(BuildPlPlotData(session, {}).series.empty());
		result.plSpinPurified.clear(); result.plExperimental.clear(); session.SetResult(result);
		EXPECT_EQ(BuildPlPlotData(session, {}).series[0].label, "active (ZPL: wzór)");
	}

	TEST(PhononPlotDataTests, PlLossRangeIncludesOnlyCurvesWhoseOwnZplIsAvailable)
	{
		Workspace workspace;
		const auto directory = workspace.Comparison();
		workspace.Write(directory, "pl.tsv", "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\n"
			"1\t0\tnan\tnan\n2\t1\tnan\tnan\n3\t0\tnan\tnan\n");
		PhononPanelSession session;
		auto result = TwoModes(); result.zplEv.computed.reset();
		session.SetResult(result); session.SetPlZpl("computed"); session.Settings().plAxis = "zpl_loss";
		session.AddDataset({AnalysisDatasetKind::Phonons, directory, "comparison", "ds.phonon_vibronic.v1"});
		session.SetCompared(directory, true);
		const auto plot = BuildPlPlotData(session, workspace.root);
		ASSERT_EQ(plot.series.size(), 1u); EXPECT_EQ(plot.series[0].source, directory);
		EXPECT_NEAR(plot.x.minimum, -150.0, 1e-10); EXPECT_NEAR(plot.x.maximum, 0.0, 1e-10);
	}
}
