#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <nlohmann/json.hpp>

#include "Core/Utils/Uuid.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		PhononAnalysisResult Result(int n, double s = 1.0)
		{
			PhononAnalysisResult result;
			result.directory = Path::FromUtf8("series/" + std::to_string(n));
			result.supercell = std::array{n, n, 1}; result.symbols.resize(n, "B");
			result.hrMethod = "displacements"; result.source = "server"; result.hasEigenvectors = false;
			result.sigmaMeV = 2.0; result.gammaMeV = 0.5;
			result.sTotal = s; result.debyeWaller = std::exp(-s);
			result.modes = {{0, -5, "ignored", 0, 0, 99}, {1, 0, "ignored", 0, 0, 99},
				{2, 50, "A1", 0, 0, 0.25 * s}, {3, 100, "???", 0, 0, 0.75 * s}};
			result.zplEv.computed = 2.0;
			result.plPhotonEnergyEv = {1.8, 1.9, 2.0}; result.plComputed = {0, 1, 2};
			result.spectralEnergyMeV = {0, 50, 100, 150}; result.spectralDensityPerMeV = {0, 0.01, 0.02, 0};
			return result;
		}

		std::vector<PhononConvergenceSeries> Group(const std::vector<PhononAnalysisResult> &results)
		{
			std::vector<std::reference_wrapper<const PhononAnalysisResult>> refs;
			for (const auto &result : results) refs.push_back(std::cref(result));
			return GroupPhononSeries(refs);
		}

		std::vector<PhononAnalysisResult> ThreeMembers()
		{
			std::vector results = {Result(2), Result(3, 2), Result(4, 3)};
			results[1].zplEv.computed = 2.1;
			results[1].plPhotonEnergyEv = {1.8, 2.0, 2.2}; results[1].plComputed = {0, 2, 0};
			results[1].spectralEnergyMeV = {0, 100}; results[1].spectralDensityPerMeV = {0, 0.04};
			results[2].zplEv.computed = 2.2;
			results[2].plPhotonEnergyEv = {2.0, 2.1, 2.2, 2.3}; results[2].plComputed = {0, 3, 6, 3};
			return results;
		}

		struct Workspace
		{
			Path root = Path::FromResolved(std::filesystem::current_path()) / ("tests/.phonon-series-" + ToString(GenerateUuid()));
			~Workspace() { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
			void Write(const Path &directory, const std::string &name, const std::string &text)
			{
				std::filesystem::create_directories(directory.Native());
				std::ofstream stream((directory / name).Native()); stream << text;
			}
			Path Dataset(const std::string &name, int n)
			{
				const auto directory = root / name;
				auto summary = nlohmann::json::parse(R"({"schema":"ds.phonon_vibronic.v1","n_atoms":1,"n_modes":3,
					"source":"server","has_eigenvectors":false,"hr_method":"displacements","hr_cutoff_A":null,
					"imaginary_modes":[],"s_total":1,"debye_waller":0.367879441,"stable":true,"sigma_meV":2,"gamma_meV":0.5,
					"max_displacement_A":null,"delta_Q_amu05_A":null,"approximation":"harmonic",
					"zpl_eV":{"computed":2,"computed_spin_purified":null,"experimental":null}})");
				summary["supercell"] = {n, n, 1};
				Write(directory, "summary.json", summary.dump());
				Write(directory, "structure.json", R"({"symbols":["B"],"masses_amu":[1],"positions_A":[[0,0,0]],"cell_A":[[1,0,0],[0,1,0],[0,0,1]]})");
				Write(directory, "modes.tsv", "index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
					"0\t0\tA1\t1\t1\t0\t0\t1\n1\t50\tA1\t1\t1\t0.25\t0\t0\n2\t100\t???\t1\t1\t0.75\t0\t0\n");
				return directory;
			}
		};
	}

	TEST(PhononSeriesTests, GroupsEveryParameterAndSourceSeparatelyAndDefaultsToLargest)
	{
		std::vector results = {Result(10), Result(9), Result(8)};
		auto fc = Result(99); fc.supercell.reset(); results.push_back(fc);
		for (int field = 0; field < 6; ++field)
		{
			auto changed = Result(12);
			if (field == 0) changed.hrMethod = "approximate_forces";
			if (field == 1) changed.hrCutoffAngstrom = 5;
			if (field == 2) changed.sigmaMeV = 3;
			if (field == 3) changed.sigmaMaxMeV = 4;
			if (field == 4) changed.gammaMeV = 1;
			if (field == 5) changed.source = "local";
			results.push_back(changed);
		}
		const auto series = Group(results);
		ASSERT_EQ(series.size(), 7u); ASSERT_EQ(series[0].members.size(), 4u);
		EXPECT_EQ(series[0].members[0].label, "komórka FC"); EXPECT_EQ(series[0].members[0].n, 0);
		EXPECT_EQ(series[0].members[1].n, 8); EXPECT_EQ(series[0].members[3].n, 10);
		for (std::size_t i = 1; i < series.size(); ++i) EXPECT_EQ(series[i].members.size(), 1u);
		results[0].supercell = std::array{2, 3, 4};
		const auto rectangular = Group(results);
		EXPECT_EQ(rectangular[0].members.back().n, 24);
		EXPECT_EQ(rectangular[0].members.back().label, "2x3x4");
	}

	TEST(PhononSeriesTests, ScalarsUsePositiveModesAndHrWeightedEnergyAndKeepUnknownIrreps)
	{
		const auto results = ThreeMembers(); const auto series = Group(results);
		ASSERT_EQ(series.size(), 1u);
		for (std::size_t i = 0; i < 3; ++i)
		{
			const auto &member = series[0].members[i];
			ASSERT_TRUE(member.franckCondonMeV); ASSERT_TRUE(member.effectiveMeV);
			EXPECT_NEAR(*member.franckCondonMeV, 87.5 * (i + 1), 1e-12);
			EXPECT_NEAR(*member.effectiveMeV, 87.5, 1e-12);
			EXPECT_NEAR(member.irrepS.at("A1"), 0.25 * (i + 1), 1e-12);
			EXPECT_NEAR(member.irrepS.at("???"), 0.75 * (i + 1), 1e-12);
			EXPECT_FALSE(member.irrepS.contains("ignored")); EXPECT_TRUE(member.uncertainIrreps);
		}
	}

	TEST(PhononSeriesTests, MissingModesZeroSAndInvalidValuesAreNotInvented)
	{
		std::vector results = {Result(2, 0), Result(3), Result(4)};
		results[1].modes.clear(); results[2].modes[3].huangRhys = std::numeric_limits<double>::quiet_NaN();
		const auto series = Group(results);
		EXPECT_EQ(series[0].members[0].franckCondonMeV, 0); EXPECT_FALSE(series[0].members[0].effectiveMeV);
		EXPECT_FALSE(series[0].members[1].franckCondonMeV); EXPECT_TRUE(series[0].members[1].irrepS.empty());
		EXPECT_FALSE(series[0].members[2].franckCondonMeV); EXPECT_TRUE(series[0].members[2].irrepS.empty());
		results[0] = Result(2); results[0].modes[3].irrep.clear(); results[0].modes[3].huangRhys = 0.1;
		EXPECT_FALSE(Group(results)[0].members[0].uncertainIrreps);
		results[0].modes[3].huangRhys = 0.1001;
		EXPECT_TRUE(Group(results)[0].members[0].uncertainIrreps);
	}

	TEST(PhononSeriesTests, OriginThreeMemberFixtureHasExactHeadersBlockOrderInterpolationAndRaggedRows)
	{
		const auto results = ThreeMembers(); const auto series = Group(results);
		const auto columns = PhononSeriesColumns(series[0], "computed");
		ASSERT_EQ(columns.size(), 17u);
		const std::vector<std::string> names = {"E-E_ZPL", "E", "L_2x2x1", "L_3x3x1", "L_4x4x1", "hw",
			"S(hw)_2x2x1", "S(hw)_3x3x1", "S(hw)_4x4x1", "n", "N_atoms", "DW", "E_FC", "S", "hw_eff", "S_???", "S_A1"};
		for (std::size_t i = 0; i < names.size(); ++i) EXPECT_EQ(columns[i].name, names[i]);
		EXPECT_NEAR(columns[0].values[0], -200, 1e-10); EXPECT_NEAR(columns[0].values[1], -100, 1e-10);
		EXPECT_EQ(columns[1].values, results[0].plPhotonEnergyEv);
		const std::array second = {1.0, 2.0, 1.0}, third = {0.0, 3.0, 6.0};
		for (std::size_t i = 0; i < 3; ++i)
		{
			EXPECT_NEAR(columns[3].values[i], second[i], 1e-12); EXPECT_NEAR(columns[4].values[i], third[i], 1e-12);
		}
		EXPECT_EQ(columns[7].values, (std::vector<double>{0, 0.02, 0.04, 0.04}));
		EXPECT_EQ(columns[9].values, (std::vector<double>{2, 3, 4}));
		EXPECT_EQ(columns[10].values, (std::vector<double>{2, 3, 4}));
		EXPECT_EQ(columns[12].values, (std::vector<double>{87.5, 175, 262.5}));
		EXPECT_EQ(columns[13].values, (std::vector<double>{1, 2, 3}));
		EXPECT_EQ(columns[14].values, (std::vector<double>{87.5, 87.5, 87.5}));
		EXPECT_NEAR(columns[11].values[0], std::exp(-1), 1e-12);
		DataTableFormat format; format.originHeader = true;
		std::istringstream table(FormatDataTable(columns, format)); std::string line;
		ASSERT_TRUE(static_cast<bool>(std::getline(table, line)));
		EXPECT_EQ(line, "E-E_ZPL\tE\tL_2x2x1\tL_3x3x1\tL_4x4x1\thw\tS(hw)_2x2x1\tS(hw)_3x3x1\tS(hw)_4x4x1\tn\tN_atoms\tDW\tE_FC\tS\thw_eff\tS_???\tS_A1");
		ASSERT_TRUE(static_cast<bool>(std::getline(table, line)));
		EXPECT_EQ(line, "meV\teV\t1/eV\t1/eV\t1/eV\tmeV\t1/meV\t1/meV\t1/meV\t\t\t\tmeV\t\tmeV\t\t");
		ASSERT_TRUE(static_cast<bool>(std::getline(table, line)));
		EXPECT_TRUE(line.starts_with("displacements cutoff=auto"));
		EXPECT_NE(line.find("E-E_ZPL grid: first member (2x2x1)"), std::string::npos);
		EXPECT_NE(line.find("S=1.000"), std::string::npos);
		int rows = 0; std::string last;
		while (std::getline(table, line)) { ++rows; last = line; EXPECT_EQ(std::count(line.begin(), line.end(), '\t'), 16); }
		EXPECT_EQ(rows, 4); EXPECT_TRUE(last.starts_with("\t\t\t\t\t150\t"));
		EXPECT_TRUE(last.ends_with("\t\t\t\t\t\t\t\t"));
	}

	TEST(PhononSeriesTests, UnknownFcSizeIsFirstAndExportsAnEmptyNInsteadOfInventingSize)
	{
		std::vector results = {Result(3), Result(2)}; results[1].supercell.reset();
		const auto series = Group(results); ASSERT_EQ(series[0].members.front().label, "komórka FC");
		const auto columns = PhononSeriesColumns(series[0], "computed");
		ASSERT_EQ(columns[2].name, "L_komórka FC");
		const auto n = std::find_if(columns.begin(), columns.end(), [](const auto &column) { return column.name == "n"; });
		ASSERT_NE(n, columns.end()); EXPECT_TRUE(std::isnan(n->values[0])); EXPECT_EQ(n->values[1], 3);
	}

	TEST(PhononSeriesTests, PlAdapterUsesSharedAxisNormalizationAndSequentialColors)
	{
		const auto results = ThreeMembers(); const auto series = Group(results);
		PhononPanelSettings settings; settings.plZpl = "computed"; settings.plAxis = "zpl_loss"; settings.plNormalization = "max";
		const auto plot = BuildPhononSeriesPlot(series[0], settings, 5);
		ASSERT_EQ(plot.series.size(), 3u); EXPECT_EQ(plot.x.unit, "meV");
		EXPECT_NEAR(plot.series[0].x.front(), 0, 1e-10); EXPECT_NEAR(plot.series[0].x.back(), 200, 1e-10);
		EXPECT_EQ(plot.series[0].y, (std::vector<double>{1, 0.5, 0}));
		EXPECT_EQ(plot.series[0].style.color, PhononSeriesColor(0, 3));
		EXPECT_EQ(plot.series[2].style.color, PhononSeriesColor(2, 3));
		EXPECT_EQ(BuildPhononSeriesPlot(series[0], settings, 2).series[0].y, (std::vector<double>{87.5, 87.5, 87.5}));
		EXPECT_EQ(BuildPhononSeriesPlot(series[0], settings, 4).series.size(), 2u);
	}

	TEST(PhononSeriesTests, ExportNeverOverwritesAndRejectsMissingReferenceGrid)
	{
		Workspace workspace; auto results = ThreeMembers(); auto series = Group(results);
		EXPECT_EQ(PhononSeriesFileName(series[0]), "PL_displacements_sig2_gam0.5.dat");
		Path first, second; std::string error;
		ASSERT_TRUE(SavePhononSeriesOrigin(series[0], "computed", workspace.root, first, error)) << error;
		ASSERT_TRUE(SavePhononSeriesOrigin(series[0], "computed", workspace.root, second, error)) << error;
		EXPECT_EQ(first.filename().Utf8(), "PL_displacements_sig2_gam0.5.dat");
		EXPECT_EQ(second.filename().Utf8(), "PL_displacements_sig2_gam0.5_2.dat");
		std::string saved; ASSERT_TRUE(TextFileIO::Load(first, saved, error));
		DataTableFormat format; format.originHeader = true;
		EXPECT_EQ(saved, FormatDataTable(PhononSeriesColumns(series[0], "computed"), format));
		results[0].hrCutoffAngstrom = 8; results[0].sigmaMaxMeV = 6;
		std::vector single = {results[0]}; series = Group(single);
		EXPECT_EQ(PhononSeriesFileName(series[0]), "PL_displacements_cutoff8A_sig2-6_gam0.5.dat");
		single[0].zplEv = {};
		EXPECT_FALSE(SavePhononSeriesOrigin(series[0], "computed", workspace.root, second, error));
		EXPECT_FALSE(error.empty());
	}

	TEST(PhononSeriesTests, SessionUsesActiveVisibleCaseAllMembersAndExistingComparisonCache)
	{
		Workspace workspace; PhononPanelSession session;
		const auto first = session.AddCase("first");
		const auto a = workspace.Dataset("a", 2), b = workspace.Dataset("b", 3), c = workspace.Dataset("c", 4);
		for (const auto &directory : {a, b})
		{
			session.AddDataset({AnalysisDatasetKind::Phonons, directory, "", "ds.phonon_vibronic.v1"});
			session.AttachDataset(first, directory);
		}
		const auto other = session.AddCase("other");
		session.AddDataset({AnalysisDatasetKind::Phonons, c, "", "ds.phonon_vibronic.v1"}); session.AttachDataset(other, c);
		ASSERT_TRUE(session.ActivateCase(first));
		std::string error; auto series = BuildPhononSeries(session, workspace.root, error);
		ASSERT_EQ(series.size(), 1u) << error; ASSERT_EQ(series[0].members.size(), 2u) << error;
		EXPECT_TRUE(error.empty()) << error;
		EXPECT_EQ(series[0].members[0].result.get().AtomCount(), 1u);
		// The existing cache remains usable after files disappear; no second reader/cache is involved.
		std::filesystem::remove((b / "modes.tsv").Native());
		series = BuildPhononSeries(session, workspace.root, error);
		EXPECT_EQ(series[0].members.size(), 2u); EXPECT_TRUE(error.empty());
		session.Settings().resultDirectory = c;
		EXPECT_EQ(BuildPhononSeries(session, workspace.root, error)[0].members.size(), 2u);
		session.SetCaseHidden(first, true);
		EXPECT_TRUE(BuildPhononSeries(session, workspace.root, error).empty());
	}
}
