#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <tuple>
#include <nlohmann/json.hpp>

#include "Core/Utils/Uuid.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		struct Workspace
		{
			Path root = Path::FromResolved(std::filesystem::temp_directory_path()) / ("ds_phonon_comparison_" + ToString(GenerateUuid()));
			~Workspace() { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
			void Write(const Path &directory, const std::string &filename, const std::string &text)
			{
				std::filesystem::create_directories(directory.Native());
				std::ofstream file((directory / filename).Native());
				file << text;
			}
			void Npy(const Path &file, const std::string &shape, const std::vector<float> &values)
			{
				std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': " + shape + ", }";
				header.append((64 - (10 + header.size() + 1) % 64) % 64, ' '); header.push_back('\n');
				std::ofstream out(file.Native(), std::ios::binary);
				out.write("\x93NUMPY\x01\x00", 8);
				out.put(static_cast<char>(header.size() & 255)); out.put(static_cast<char>(header.size() >> 8));
				out << header;
				for (float value : values)
					for (int byte = 0; byte < 4; ++byte)
						out.put(static_cast<char>((std::bit_cast<std::uint32_t>(value) >> (8 * byte)) & 255));
			}
			Path Dataset(const std::string &name)
			{
				const auto directory = root / name;
				Write(directory, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","n_atoms":1,"n_modes":3,
					"imaginary_modes":[],"s_total":0.9,"debye_waller":0.4,"stable":true,"sigma_meV":6,"gamma_meV":2,
					"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
					"zpl_eV":{"computed":1.9,"computed_spin_purified":null,"experimental":2.5}})");
				Write(directory, "structure.json", R"({"symbols":["B"],"masses_amu":[1],"positions_A":[[0,0,0]],
					"cell_A":[[1,0,0],[0,1,0],[0,0,1]]})");
				Write(directory, "modes.tsv", "index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
					"0\t0\tA1\t1\t1\t99\t99\t1\n1\t50\tA1\t1\t1\t0.2\t1\t0\n2\t100\tE\t1\t1\t0.7\t2\t0\n");
				Npy(directory / "eigenvectors.npy", "(3, 1, 3)", {1, 0, 0, 0, 1, 0, 0, 0, 1});
				Npy(directory / "participation.npy", "(3, 1)", {1, 1, 1});
				return directory;
			}
			Path Ccd(const std::string &name, int axis = 0, const std::string &symbol = "B")
			{
				const auto directory = root / name;
				Write(directory, "summary.json", R"({"schema":"ds.ccd_analysis.v1","delta_Q_amu05_A":3.1622776601683795,
					"zpl_eV":{"used":2},"displacement_file":"ccd_displacement.json"})");
				auto data = nlohmann::json::parse(R"({"schema":"ds.ccd_displacement.v1","symbols":["B","N","N"],
					"masses_amu":[2,1,1],"cell_angstrom":[[10,0,0],[0,10,0],[0,0,10]],
					"gs_positions_angstrom":[[0.1,0,0],[9.9,0,0],[4,0,0]],
					"displacement_angstrom":[[2,0,0],[1,0,0],[1,0,0]],"delta_Q_amu05_A":3.1622776601683795,
					"gs_source":"gs/POSCAR","es_source":"es/POSCAR"})");
				data["symbols"][0] = symbol;
				if (axis == 1) data["displacement_angstrom"] = {{0, 2, 0}, {0, 1, 0}, {0, 1, 0}};
				Write(directory, "ccd_displacement.json", data.dump());
				return directory;
			}
		};

		PhononAnalysisResult Synthetic()
		{
			PhononAnalysisResult result;
			result.sTotal = 0.9; result.debyeWaller = 0.4; result.deltaQAmuHalfAngstrom = 0.2;
			result.zplEv.computed = 1.9; result.zplEv.experimental = 2.5;
			result.modes = {{0, 0, "A1", 1, 1, 99, 99, true}, {1, 50, "A1", 3, 1, 0.2, 1}, {2, 100, "E", 4, 1, 0.7, 2}};
			return result;
		}

		void SetSynthetic(PhononPanelSession &session, const Path &directory, PhononAnalysisResult result)
		{
			if (session.Cases().empty()) (void)session.AddCase("case");
			session.AddDataset({AnalysisDatasetKind::Phonons, directory, "synthetic", "ds.phonon_vibronic.v1"});
			session.AttachDataset(session.ActiveCaseId(), directory);
			session.Settings().resultDirectory = directory;
			result.directory = directory; session.SetResult(std::move(result));
		}
	}

	TEST(PhononComparisonTests, RowsGroupCasesWithActiveFirstAndOnlyComparedUnattachedPhononsLast)
	{
		Workspace workspace;
		const auto a = workspace.Dataset("a"), b = workspace.Dataset("b"), variant = workspace.Dataset("variant"), orphan = workspace.Dataset("orphan");
		PhononPanelSession session;
		const auto first = session.AddCase("first"); session.SetDirectory("result", a);
		session.AddDataset({AnalysisDatasetKind::Phonons, variant, "variant", "ds.phonon_vibronic.v1"}); session.AttachDataset(first, variant);
		const auto second = session.AddCase("second"); session.SetDirectory("result", b);
		session.AddDataset({AnalysisDatasetKind::Ccd, workspace.root / "ccd", "ccd", "ds.ccd_analysis.v1"});
		session.AttachDataset(second, workspace.root / "ccd");
		session.AddDataset({AnalysisDatasetKind::Phonons, orphan, "orphan", "ds.phonon_vibronic.v1"}); session.SetCompared(orphan, true);
		session.AddDataset({AnalysisDatasetKind::Phonons, workspace.root / "ignored", "ignored", {}});
		const auto rows = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_EQ(rows.size(), 4u);
		EXPECT_EQ(rows[0].caseId, second); EXPECT_EQ(rows[0].directory, b); EXPECT_TRUE(rows[0].active);
		EXPECT_EQ(rows[1].caseId, first); EXPECT_EQ(rows[1].directory, a);
		EXPECT_EQ(rows[2].caseId, first); EXPECT_EQ(rows[2].directory, variant);
		EXPECT_TRUE(rows[3].caseId.empty()); EXPECT_EQ(rows[3].directory, orphan); EXPECT_TRUE(rows[3].compared);
		EXPECT_EQ(std::count_if(rows.begin(), rows.end(), [](const auto &row) { return row.active; }), 1);
		session.SetCompared(a, true);
		EXPECT_TRUE(BuildPhononComparisonRows(session, workspace.root)[1].compared);
	}

	TEST(PhononComparisonTests, StrongestModeExcludesTranslationsAndEffectiveFrequencyMatchesCcdDefinition)
	{
		Workspace workspace;
		PhononPanelSession session;
		SetSynthetic(session, workspace.root / "synthetic", Synthetic());
		const auto rows = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_EQ(rows.size(), 1u);
		EXPECT_EQ(rows[0].values[0], 2.5); EXPECT_EQ(rows[0].zplSource, "eksperymentalne");
		EXPECT_EQ(rows[0].values[1], 0.2); EXPECT_EQ(rows[0].values[2], 0.9); EXPECT_EQ(rows[0].values[3], 40);
		ASSERT_TRUE(rows[0].values[4]); EXPECT_NEAR(*rows[0].values[4], std::sqrt(8500.0), 1e-12);
		EXPECT_EQ(rows[0].values[5], 100); EXPECT_EQ(rows[0].values[6], 0.7); EXPECT_EQ(rows[0].values[7], 4);
		EXPECT_EQ(rows[0].values[8], 0); EXPECT_TRUE(rows[0].active);
	}

	TEST(PhononComparisonTests, InvalidEffectiveFrequencyIsMissingButImaginaryCountAndStrongestModeRemain)
	{
		Workspace workspace;
		PhononPanelSession session;
		auto result = Synthetic(); result.modes[1].frequencyMeV = -50; result.imaginaryModes = {1};
		SetSynthetic(session, workspace.root / "synthetic", result);
		auto rows = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_FALSE(rows[0].values[4]); EXPECT_FALSE(rows[0].reasons[4].empty()); EXPECT_EQ(rows[0].values[8], 1);
		EXPECT_EQ(rows[0].values[5], 100);
		result = Synthetic(); result.modes[1].qAmuHalfAngstrom = result.modes[2].qAmuHalfAngstrom = 0;
		session.SetResult(result); EXPECT_FALSE(BuildPhononComparisonRows(session, workspace.root)[0].values[4]);
		result = Synthetic(); result.modes[1].qAmuHalfAngstrom = std::numeric_limits<double>::quiet_NaN();
		session.SetResult(result); EXPECT_FALSE(BuildPhononComparisonRows(session, workspace.root)[0].values[4]);
	}

	TEST(PhononComparisonTests, MissingDatasetHasReasonsAndEmptyNumericExportCellsInsteadOfZeros)
	{
		Workspace workspace;
		PhononPanelSession session;
		const auto id = session.AddCase("missing");
		const auto directory = workspace.root / "missing";
		session.Settings().resultDirectory = directory; session.AttachDataset(id, directory);
		const auto rows = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_EQ(rows.size(), 1u); EXPECT_FALSE(rows[0].active);
		for (std::size_t i = 0; i < rows[0].values.size(); ++i)
		{
			EXPECT_FALSE(rows[0].values[i]); EXPECT_FALSE(rows[0].reasons[i].empty());
		}
		const auto columns = PhononComparisonColumns(rows);
		ASSERT_EQ(columns.size(), 12u);
		for (std::size_t i = 3; i < columns.size(); ++i) EXPECT_TRUE(std::isnan(columns[i].values[0]));
		const auto text = FormatDataTable(columns, {});
		EXPECT_NE(text.find("0\tmissing\tmissing\t\t\t\t\t\t\t\t\t\n"), std::string::npos);
	}

	TEST(PhononComparisonTests, SummarySurvivesMissingModesAndUnavailableZplHasAReason)
	{
		Workspace workspace;
		const auto directory = workspace.Dataset("summary-only");
		workspace.Write(directory, "modes.tsv", "invalid");
		PhononPanelSession session;
		session.AddDataset({AnalysisDatasetKind::Phonons, directory, "summary-only", "ds.phonon_vibronic.v1"}); session.SetCompared(directory, true);
		const auto rows = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_EQ(rows.size(), 1u); EXPECT_EQ(rows[0].values[2], 0.9);
		for (std::size_t i = 4; i < 9; ++i) { EXPECT_FALSE(rows[0].values[i]); EXPECT_FALSE(rows[0].reasons[i].empty()); }
		auto result = Synthetic(); result.zplEv = {}; result.modes.clear();
		SetSynthetic(session, workspace.root / "synthetic", result);
		const auto missing = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_FALSE(missing[0].values[0]); EXPECT_FALSE(missing[0].reasons[0].empty()); EXPECT_FALSE(missing[0].values[5]);
		result = Synthetic();
		for (auto &mode : result.modes) mode.isTranslation = true;
		session.SetResult(result);
		const auto translations = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_FALSE(translations[0].values[4]); EXPECT_FALSE(translations[0].values[5]); EXPECT_FALSE(translations[0].values[7]);
	}

	TEST(PhononComparisonTests, ModeAndSummaryReadsAreCachedUntilDatasetRefresh)
	{
		Workspace workspace;
		const auto directory = workspace.Dataset("cached");
		PhononPanelSession session;
		session.AddDataset({AnalysisDatasetKind::Phonons, directory, "cached", "ds.phonon_vibronic.v1"}); session.SetCompared(directory, true);
		const auto first = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_TRUE(first[0].values[4]);
		workspace.Write(directory, "summary.json", "invalid"); workspace.Write(directory, "modes.tsv", "invalid");
		EXPECT_EQ(BuildPhononComparisonRows(session, workspace.root)[0].values, first[0].values);
		session.AddDataset({AnalysisDatasetKind::Phonons, directory, "cached", "ds.phonon_vibronic.v1"});
		EXPECT_FALSE(BuildPhononComparisonRows(session, workspace.root)[0].values[2]);
	}

	TEST(PhononComparisonTests, SummaryZplFallbackKeepsItsSourceAndExportHasOneColumnPerTableColumnWithUnits)
	{
		Workspace workspace;
		PhononPanelSession session;
		SetSynthetic(session, workspace.root / "synthetic", Synthetic()); session.SetPlZpl("spin_purified");
		const auto rows = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_EQ(rows[0].values[0], 1.9); EXPECT_EQ(rows[0].zplSource, "obliczone");
		const auto columns = PhononComparisonColumns(rows);
		ASSERT_EQ(columns.size(), 12u);
		EXPECT_EQ(columns[3].unit, "eV"); EXPECT_EQ(columns[4].unit, "amu^½ Å"); EXPECT_EQ(columns[6].unit, "%");
		EXPECT_EQ(columns[7].unit, "meV"); EXPECT_EQ(columns[8].unit, "meV");
		DataTableFormat format; format.originHeader = true;
		const auto text = FormatDataTable(columns, format);
		EXPECT_NE(text.find("\n\t\t\teV\tamu^½ Å\t\t%\tmeV\tmeV\t\t\t\n"), std::string::npos);
		EXPECT_TRUE(PhononComparisonColumns({}).empty());
	}

	TEST(PhononComparisonTests, DisplacementColumnsUseSelectedCcdAndFullExportRetainsTableOrderAndFraction)
	{
		Workspace workspace;
		PhononPanelSession session;
		auto phonons = Synthetic(); phonons.deltaQAmuHalfAngstrom = 5;
		SetSynthetic(session, workspace.root / "phonons", phonons);
		auto rows = BuildPhononComparisonRows(session, workspace.root);
		for (std::size_t i = 9; i < 12; ++i)
		{
			EXPECT_FALSE(rows[0].values[i]); EXPECT_EQ(rows[0].reasons[i], "Brak zestawu CCD z ΔR w przypadku.");
		}
		const auto legacy = workspace.Ccd("legacy");
		std::filesystem::remove((legacy / "ccd_displacement.json").Native());
		session.Settings().selectedCcdDirectory = legacy;
		rows = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_EQ(rows[0].reasons[9], "Zestaw CCD bez ΔR - przelicz CCD.");
		session.Settings().selectedCcdDirectory = workspace.Ccd("ccd");
		session.Settings().carriedFraction = 0.8;
		rows = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_EQ(rows.size(), 1u);
		for (std::size_t i = 9; i < 14; ++i) { ASSERT_TRUE(rows[0].values[i]); EXPECT_TRUE(std::isfinite(*rows[0].values[i])); }
		EXPECT_NEAR(*rows[0].values[9], std::sqrt(10.0), 1e-12);
		EXPECT_EQ(rows[0].values[10], 2); EXPECT_EQ(rows[0].values[11], 1);
		EXPECT_EQ(rows[0].values[12], 20); EXPECT_EQ(rows[0].values[13], 16);
		const auto columns = PhononComparisonColumns(rows, 0.8);
		ASSERT_EQ(columns.size(), 17u);
		EXPECT_EQ(columns[12].name, "ΔQ (CCD)"); EXPECT_EQ(columns[12].unit, "amu^½ Å");
		EXPECT_EQ(columns[13].name, "max |ΔR|"); EXPECT_EQ(columns[13].unit, "Å");
		EXPECT_EQ(columns[14].name, "atomy 80 % ΔQ²");
		EXPECT_EQ(columns[15].name, "pokrycie ΔQ² modami"); EXPECT_EQ(columns[15].unit, "%");
		EXPECT_EQ(columns[16].name, "największy udział modu"); EXPECT_EQ(columns[16].unit, "%");
		for (std::size_t i = 0; i < rows[0].values.size(); ++i) EXPECT_EQ(columns[i + 3].values[0], *rows[0].values[i]);
		EXPECT_EQ(PhononComparisonColumns(rows).size(), 12u);
		EXPECT_TRUE(PhononComparisonColumns({}, 0.9).empty());
		DataTableFormat format; format.originHeader = true;
		EXPECT_NE(FormatDataTable(columns, format).find("atomy 80 % ΔQ²"), std::string::npos);
		session.Settings().carriedFraction = 0.9;
		EXPECT_EQ(BuildPhononComparisonRows(session, workspace.root)[0].values[11], 2);
	}

	TEST(PhononComparisonTests, CoverageExcludesTranslationsAndNeedsFiniteProjectionsAndPositivePhononDeltaQ)
	{
		Workspace workspace;
		PhononPanelSession session;
		auto phonons = Synthetic(); phonons.deltaQAmuHalfAngstrom = 5; phonons.modes[1].frequencyMeV = -50;
		SetSynthetic(session, workspace.root / "phonons", phonons);
		auto rows = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_EQ(rows[0].values[12], 20); EXPECT_EQ(rows[0].values[13], 16);
		phonons.modes[1].qAmuHalfAngstrom = std::numeric_limits<double>::quiet_NaN(); session.SetResult(phonons);
		rows = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_FALSE(rows[0].values[12]); EXPECT_FALSE(rows[0].values[13]); EXPECT_FALSE(rows[0].reasons[12].empty());
		phonons = Synthetic(); phonons.deltaQAmuHalfAngstrom = 0; session.SetResult(phonons);
		rows = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_FALSE(rows[0].values[12]); EXPECT_FALSE(rows[0].values[13]);
		phonons = Synthetic(); phonons.modes.clear(); session.SetResult(phonons);
		rows = BuildPhononComparisonRows(session, workspace.root);
		EXPECT_FALSE(rows[0].values[12]); EXPECT_EQ(rows[0].reasons[12], rows[0].reasons[5]);
		const auto columns = PhononComparisonColumns(rows, 0.9);
		for (std::size_t i = 12; i < 17; ++i) EXPECT_TRUE(std::isnan(columns[i].values[0]));
	}

	TEST(PhononComparisonTests, LocalisationUsesOneSeriesPerActiveOrComparedCaseWithSelectedDisplacement)
	{
		Workspace workspace;
		PhononPanelSession session;
		SetSynthetic(session, workspace.root / "phonons", Synthetic());
		const auto a = session.ActiveCaseId();
		const auto ccdA = workspace.Ccd("ccd-a"), ccdB = workspace.Ccd("ccd-b", 1);
		session.Settings().selectedCcdDirectory = ccdA;
		const auto variant = workspace.Dataset("variant");
		session.AddDataset({AnalysisDatasetKind::Phonons, variant, "variant", {}}); session.AttachDataset(a, variant);
		session.SetCompared(variant, true);
		const auto b = session.AddCase("second");
		session.AddDataset({AnalysisDatasetKind::Ccd, ccdB, "ccd-b", {}}); session.AttachDataset(b, ccdB);
		session.Settings().selectedCcdDirectory = ccdB;
		(void)session.AddCase("ignored"); session.Settings().selectedCcdDirectory = workspace.Ccd("ignored");
		ASSERT_TRUE(session.ActivateCase(b));
		session.Settings().seriesStyles["localisation/" + a].visible = false;
		const std::vector<glm::vec4> colors = {{1, 0, 0, 1}, {0, 1, 0, 1}};
		const auto plot = BuildLocalisationPlotData(session, workspace.root, {}, colors);
		EXPECT_EQ(plot.id, "localisation"); EXPECT_EQ(plot.x.unit, "Å"); EXPECT_EQ(plot.y.maximum, 1);
		ASSERT_EQ(plot.series.size(), 2u);
		EXPECT_EQ(plot.series[0].label, "second"); EXPECT_EQ(plot.series[0].source, ccdB);
		EXPECT_EQ(plot.series[1].label, "case"); EXPECT_EQ(plot.series[1].source, ccdA); EXPECT_FALSE(plot.series[1].style.visible);
		EXPECT_NE(PhononPlotLegendId(plot.series[0]), PhononPlotLegendId(plot.series[1]));
		for (const auto &series : plot.series)
		{
			EXPECT_TRUE(std::is_sorted(series.x.begin(), series.x.end()));
			EXPECT_TRUE(std::is_sorted(series.y.begin(), series.y.end())); EXPECT_DOUBLE_EQ(series.y.back(), 1);
			EXPECT_EQ(PhononPlotStyleKey(series), series.id);
		}
		const auto exported = PlotDataColumns(plot, workspace.root, false);
		ASSERT_EQ(exported.size(), 4u); EXPECT_EQ(exported[0].unit, "Å");
		EXPECT_EQ(exported[1].values, plot.series[0].y); EXPECT_NE(exported[0].comment.find("ccd-b"), std::string::npos);
		EXPECT_EQ(PlotDataColumns(plot, workspace.root, true).size(), 2u);
		EXPECT_EQ(session.ActiveCaseId(), b); EXPECT_EQ(session.Settings().selectedCcdDirectory, ccdB);
		const auto rows = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_FALSE(rows.empty()); EXPECT_EQ(rows[0].caseId, b); EXPECT_TRUE(rows[0].values[9]);
	}

	TEST(PhononComparisonTests, OverlapTableIncludesComparableValuesAndEmptyCellsForIncompatibleCases)
	{
		Workspace workspace;
		PhononPanelSession session;
		for (const auto &[name, axis, symbol] : {std::tuple{"first", 0, "B"}, std::tuple{"second", 1, "B"}, std::tuple{"third", 0, "C"}})
		{
			const auto id = session.AddCase(name);
			const auto directory = workspace.Ccd(name, axis, symbol);
			session.AddDataset({AnalysisDatasetKind::Ccd, directory, name, {}}); session.AttachDataset(id, directory);
			session.Settings().selectedCcdDirectory = directory; session.SetCompared(directory, true);
		}
		const auto columns = BuildDisplacementOverlapColumns(session, workspace.root);
		ASSERT_EQ(columns.size(), 4u);
		EXPECT_EQ(columns[0].text, (std::vector<std::string>{"third", "first", "second"}));
		for (std::size_t i = 1; i < columns.size(); ++i)
		{
			EXPECT_EQ(columns[i].name, columns[0].text[i - 1]); EXPECT_FALSE(columns[i].comment.empty());
			EXPECT_NEAR(columns[i].values[i - 1], 1, 1e-12);
		}
		EXPECT_TRUE(std::isnan(columns[1].values[1])); EXPECT_TRUE(std::isnan(columns[2].values[0]));
		EXPECT_DOUBLE_EQ(columns[2].values[2], 0); EXPECT_DOUBLE_EQ(columns[3].values[1], 0);
		EXPECT_NE(FormatDataTable(columns, {}).find("first\t\t1\t0\n"), std::string::npos);
		PhononPanelSession empty;
		EXPECT_TRUE(BuildDisplacementOverlapColumns(empty, workspace.root).empty());
		EXPECT_TRUE(BuildLocalisationPlotData(empty, workspace.root).series.empty());
	}

	TEST(PhononComparisonTests, CcdCacheInvalidatesOnDisplacementAndSummaryTimestampsAndRecoversFailedReads)
	{
		Workspace workspace;
		PhononPanelSession session;
		const auto directory = workspace.Ccd("cached-ccd");
		const auto id = session.AddCase("ccd-only");
		session.AddDataset({AnalysisDatasetKind::Ccd, directory, "ccd", {}}); session.AttachDataset(id, directory);
		session.Settings().selectedCcdDirectory = directory;
		const auto initial = BuildPhononComparisonRows(session, workspace.root);
		ASSERT_EQ(initial.size(), 1u); ASSERT_TRUE(initial[0].values[9]);
		const auto displacementFile = (directory / "ccd_displacement.json").Native();
		const auto timestamp = std::filesystem::last_write_time(displacementFile);
		workspace.Write(directory, "ccd_displacement.json", "invalid");
		std::filesystem::last_write_time(displacementFile, timestamp);
		EXPECT_EQ(BuildPhononComparisonRows(session, workspace.root)[0].values[9], initial[0].values[9]);
		std::filesystem::last_write_time(displacementFile, timestamp + std::chrono::seconds(2));
		EXPECT_FALSE(BuildPhononComparisonRows(session, workspace.root)[0].values[9]);
		EXPECT_TRUE(BuildLocalisationPlotData(session, workspace.root).series.empty());
		(void)workspace.Ccd("cached-ccd");
		std::filesystem::last_write_time(displacementFile, timestamp + std::chrono::seconds(4));
		EXPECT_TRUE(BuildPhononComparisonRows(session, workspace.root)[0].values[9]);
		const auto summaryFile = (directory / "summary.json").Native();
		const auto summaryTimestamp = std::filesystem::last_write_time(summaryFile);
		workspace.Write(directory, "summary.json", R"({"schema":"ds.ccd_analysis.v1","delta_Q_amu05_A":1,"zpl_eV":{"used":2},"displacement_file":null})");
		std::filesystem::last_write_time(summaryFile, summaryTimestamp + std::chrono::seconds(2));
		EXPECT_FALSE(BuildPhononComparisonRows(session, workspace.root)[0].values[9]);
		EXPECT_TRUE(BuildDisplacementOverlapColumns(session, workspace.root).empty());
	}
}
