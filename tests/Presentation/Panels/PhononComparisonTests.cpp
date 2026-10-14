#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
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
}
