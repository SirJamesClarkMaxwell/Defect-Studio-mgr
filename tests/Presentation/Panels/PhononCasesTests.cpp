#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononPanelSettingsIOCases.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio::Tests
{
	class PhononCasesTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_cases_" + ToString(GenerateUuid())));
		void SetUp() override { std::filesystem::create_directories(root.Native()); }
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }

		void Summary(const Path &directory, const Path &gs, const Path &es, const std::string &label,
			AnalysisDatasetKind kind = AnalysisDatasetKind::Phonons)
		{
			std::filesystem::create_directories(directory.Native());
			nlohmann::json summary = {
				{"schema", kind == AnalysisDatasetKind::Ccd ? "ds.ccd_analysis.v1" : "ds.phonon_vibronic.v1"},
				{"label", label}, {"s_total", 1.5}, {"debye_waller", 0.2}, {"stable", true},
				{"sigma_meV", 7.0}, {"gamma_meV", 3.0}, {"max_displacement_A", 0.1},
				{"delta_Q_amu05_A", 0.2}, {"approximation", "harmonic"},
				{"n_atoms", 1}, {"n_modes", 3}, {"imaginary_modes", nlohmann::json::array()},
				{"zpl_eV", {{"computed", 2.0}, {"computed_spin_purified", nullptr}, {"experimental", nullptr}}},
				{"zpl_formula", {{"text", "E_es - E_gs"}}}
			};
			summary["inputs_relative"] = {
				{"gs_dir", gs.Native().lexically_relative(directory.Native()).generic_string()},
				{"es_dir", es.Native().lexically_relative(directory.Native()).generic_string()},
				{"fc_dir", "../../fc"}, {"triplet_dir", "../../triplet"}, {"bulk_fc_dir", "../../bulk"}
			};
			std::ofstream out((directory / "summary.json").Native());
			out << summary.dump();
		}

		void ResultFiles(const Path &directory)
		{
			const auto write = [&](const std::string &file, const std::string &text) { std::ofstream out((directory / file).Native()); out << text; };
			write("structure.json", R"({"symbols":["B"],"masses_amu":[1],"positions_A":[[0,0,0]],"cell_A":[[1,0,0],[0,1,0],[0,0,1]]})");
			write("modes.tsv", "index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
				"0\t50\tA1\t1\t1\t0.2\t0.1\t0\n1\t60\tE\t1\t1\t0.3\t0.1\t0\n2\t70\tE\t1\t1\t1\t0.1\t0\n");
			const auto npy = [&](const std::string &file, const std::string &shape, const std::vector<float> &values) {
				std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': " + shape + ", }";
				header.append((64 - (10 + header.size() + 1) % 64) % 64, ' ');
				header.push_back('\n');
				std::ofstream out((directory / file).Native(), std::ios::binary);
				out.write("\x93NUMPY\x01\x00", 8);
				out.put(static_cast<char>(header.size() & 255)); out.put(static_cast<char>(header.size() >> 8));
				out << header;
				for (float value : values)
					for (int byte = 0; byte < 4; ++byte)
						out.put(static_cast<char>((std::bit_cast<std::uint32_t>(value) >> (8 * byte)) & 255));
			};
			npy("eigenvectors.npy", "(3, 1, 3)", {1, 0, 0, 0, 1, 0, 0, 0, 1});
			npy("participation.npy", "(3, 1)", {1, 1, 1});
		}

		PhononCaseSettings Settings(const std::string &tag)
		{
			PhononCaseSettings s;
			s.forceConstantDirectory = root / tag / "fc";
			s.groundStateDirectory = root / tag / "gs";
			s.excitedStateDirectory = root / tag / "es";
			s.tripletDirectory = root / tag / "triplet";
			s.zplFormula = tag;
			s.groundAtExcitedDirectory = root / tag / "gs-at-es";
			s.excitedAtGroundDirectory = root / tag / "es-at-gs";
			s.outputDirectory = root / tag / "output";
			s.sigmaMeV = 7.0;
			s.gammaMeV = 3.0;
			s.bulkForceConstantDirectory = root / tag / "bulk";
			s.embeddingMult = "24x24x1";
			s.dcutoffAngstrom = 6.0;
			s.bcutoffAngstrom = 8.0;
			s.ccdOutputDirectory = root / tag / "ccd-inputs";
			s.ccdStep = 0.125;
			s.ccdDirectory = root / tag / "ccd-path";
			s.ccdAnalysisOutputDirectory = root / tag / "ccd-output";
			s.ccdTemperatureK = 100.0;
			s.ccdPhononCurvature = true;
			s.selectedCcdDirectory = root / tag / "ccd-result";
			s.resultDirectory = root / tag / "result";
			s.selectedMode = 4;
			s.plZpl = "formula";
			s.irrepFilter = tag;
			s.minHuangRhys = 0.02;
			return s;
		}

		void ExpectSettings(const PhononCaseSettings &a, const PhononCaseSettings &b)
		{
			EXPECT_EQ(a.forceConstantDirectory, b.forceConstantDirectory);
			EXPECT_EQ(a.groundStateDirectory, b.groundStateDirectory);
			EXPECT_EQ(a.excitedStateDirectory, b.excitedStateDirectory);
			EXPECT_EQ(a.tripletDirectory, b.tripletDirectory);
			EXPECT_EQ(a.zplFormula, b.zplFormula);
			EXPECT_EQ(a.groundAtExcitedDirectory, b.groundAtExcitedDirectory);
			EXPECT_EQ(a.excitedAtGroundDirectory, b.excitedAtGroundDirectory);
			EXPECT_EQ(a.outputDirectory, b.outputDirectory);
			EXPECT_DOUBLE_EQ(a.sigmaMeV, b.sigmaMeV);
			EXPECT_DOUBLE_EQ(a.gammaMeV, b.gammaMeV);
			EXPECT_EQ(a.bulkForceConstantDirectory, b.bulkForceConstantDirectory);
			EXPECT_EQ(a.embeddingMult, b.embeddingMult);
			EXPECT_EQ(a.dcutoffAngstrom, b.dcutoffAngstrom);
			EXPECT_EQ(a.bcutoffAngstrom, b.bcutoffAngstrom);
			EXPECT_EQ(a.ccdOutputDirectory, b.ccdOutputDirectory);
			EXPECT_DOUBLE_EQ(a.ccdStep, b.ccdStep);
			EXPECT_EQ(a.ccdDirectory, b.ccdDirectory);
			EXPECT_EQ(a.ccdAnalysisOutputDirectory, b.ccdAnalysisOutputDirectory);
			EXPECT_DOUBLE_EQ(a.ccdTemperatureK, b.ccdTemperatureK);
			EXPECT_EQ(a.ccdPhononCurvature, b.ccdPhononCurvature);
			EXPECT_EQ(a.selectedCcdDirectory, b.selectedCcdDirectory);
			EXPECT_EQ(a.resultDirectory, b.resultDirectory);
			EXPECT_EQ(a.selectedMode, b.selectedMode);
			EXPECT_EQ(a.plZpl, b.plZpl);
			EXPECT_EQ(a.irrepFilter, b.irrepFilter);
			EXPECT_DOUBLE_EQ(a.minHuangRhys, b.minHuangRhys);
		}

		PhononAnalysisResult Result()
		{
			PhononAnalysisResult result;
			result.symbols = {"B"};
			result.positionsAngstrom = {glm::dvec3(0.0)};
			result.massesAmu = {1.0};
			result.eigenvectors = {1, 0, 0};
			result.participation = {1};
			PhononMode mode;
			mode.huangRhys = 1.0;
			mode.frequencyMeV = 50.0;
			result.modes = {mode};
			return result;
		}
	};

	TEST_F(PhononCasesTests, ActivationSwapsEveryCaseFieldAndRetainsGlobalSettings)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto a = session.AddCase("A");
		auto first = Settings("a");
		static_cast<PhononCaseSettings &>(session.Settings()) = first;
		session.Settings().seriesStyles["S/A"].lineWeight = 3.0f;
		session.Settings().comparedDirectories = {root / "compared"};
		session.Settings().arrowLowColor = glm::vec3(0.1f, 0.2f, 0.3f);
		session.Settings().ccdLevels = 7;
		session.Settings().dispersionDirectory = root / "bulk-dispersion";
		const auto b = session.AddCase("B");
		ExpectSettings(session.Settings(), {});
		auto second = Settings("b");
		second.sigmaMeV = 9.0;
		second.gammaMeV = 4.0;
		second.ccdStep = 0.5;
		second.ccdPhononCurvature = false;
		second.ccdTemperatureK = 50.0;
		second.selectedMode = 9;
		second.plZpl = "experimental";
		second.minHuangRhys = 0.1;
		second.dcutoffAngstrom.reset();
		static_cast<PhononCaseSettings &>(session.Settings()) = second;
		ASSERT_TRUE(session.ActivateCase(a));
		ExpectSettings(session.Settings(), first);
		ASSERT_TRUE(session.ActivateCase(b));
		ExpectSettings(session.Settings(), second);
		EXPECT_FLOAT_EQ(session.Settings().seriesStyles.at("S/A").lineWeight, 3.0f);
		EXPECT_EQ(session.Settings().comparedDirectories, (std::vector<Path>{root / "compared"}));
		EXPECT_EQ(session.Settings().arrowLowColor, glm::vec3(0.1f, 0.2f, 0.3f));
		EXPECT_EQ(session.Settings().ccdLevels, 7);
		EXPECT_EQ(session.Settings().dispersionDirectory, root / "bulk-dispersion");
		EXPECT_EQ(session.Result(), nullptr); // Missing case results are nonfatal and never show the old case.
		EXPECT_FALSE(session.ActivateCase("missing"));
		PhononPanelSettings restored;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(PhononPanelSettingsIO::Serialize(session.Settings(), root), root, restored, warnings, error));
		ASSERT_EQ(restored.cases.size(), 2u);
		ExpectSettings(restored.cases[0].settings, first);
		ExpectSettings(restored.cases[1].settings, second);
	}

	TEST_F(PhononCasesTests, RemovedIdsAreNotReusedAfterSavingAndReopening)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		EXPECT_EQ(session.AddCase("A"), "case-1");
		const auto removed = session.AddCase("B");
		EXPECT_EQ(removed, "case-2");
		session.RemoveCase(removed);
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(PhononPanelSettingsIO::Serialize(session.Settings(), root), root, loaded, warnings, error));
		session.SetPersistedState(loaded, root);
		EXPECT_EQ(session.AddCase("C"), "case-3");
		session.RemoveCase("case-1");
		session.RemoveCase("case-3");
		EXPECT_TRUE(session.Cases().empty());
		const auto text = PhononPanelSettingsIO::Serialize(session.Settings(), root);
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, root, loaded, warnings, error));
		session.SetPersistedState(loaded, root);
		EXPECT_EQ(session.AddCase("D"), "case-4");
	}

	TEST_F(PhononCasesTests, ActivationLoadsEachResultAndRestoresItsSelectedMode)
	{
		const auto p = root / "a/defectstudio/phonons", q = root / "b/defectstudio/phonons";
		Summary(p, root / "a/gs", root / "a/es", "A"); ResultFiles(p);
		Summary(q, root / "b/gs", root / "b/es", "B"); ResultFiles(q);
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto a = session.AddCase("A");
		session.SetDirectory("result", p);
		ASSERT_NE(session.Result(), nullptr);
		session.SelectMode(1);
		const auto b = session.AddCase("B");
		session.SetDirectory("result", q);
		ASSERT_NE(session.Result(), nullptr);
		session.SelectMode(0);
		ASSERT_TRUE(session.ActivateCase(a));
		ASSERT_NE(session.Result(), nullptr);
		EXPECT_EQ(session.Result()->label, "A");
		EXPECT_EQ(session.SelectedMode(), 1u);
		ASSERT_TRUE(session.ActivateCase(b));
		ASSERT_NE(session.Result(), nullptr);
		EXPECT_EQ(session.Result()->label, "B");
		EXPECT_EQ(session.SelectedMode(), 0u);
		EXPECT_EQ(session.CurrentDataset(), q);
	}

	TEST_F(PhononCasesTests, DuplicateCopiesInputsAndViewsButClearsOutputsAndMembership)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto original = session.AddCase("A");
		auto expected = Settings("a");
		static_cast<PhononCaseSettings &>(session.Settings()) = expected;
		session.AttachDataset(original, root / "result");
		const auto copy = session.DuplicateCase(original);
		expected.outputDirectory = {};
		expected.ccdOutputDirectory = {};
		expected.ccdAnalysisOutputDirectory = {};
		ExpectSettings(session.Settings(), expected);
		ASSERT_EQ(session.Cases().size(), 2u);
		EXPECT_EQ(session.Cases()[1].name, "A (kopia)");
		EXPECT_EQ(session.ActiveCaseId(), copy);
		EXPECT_TRUE(session.Cases()[1].datasets.empty());
		EXPECT_EQ(session.Cases()[0].datasets.size(), 1u);
	}

	TEST_F(PhononCasesTests, OutputsAvoidOtherAttachmentsUseSlugsAndRetainOwnPlainDirectory)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		std::filesystem::create_directories((root / "setup").Native());
		const auto a = session.AddCase("A");
		session.Settings().groundStateDirectory = root / "setup/gs-a";
		session.Settings().excitedStateDirectory = root / "setup/es-a";
		const auto plain = session.CaseOutputDirectory("phonons");
		EXPECT_EQ(plain, root / "setup/defectstudio/phonons");
		session.AttachDataset(a, plain);
		const auto b = session.AddCase("B /?");
		session.Settings().groundStateDirectory = root / "setup/gs-b";
		session.Settings().excitedStateDirectory = root / "setup/es-b";
		const auto slugged = root / "setup/defectstudio/phonons_B___";
		EXPECT_EQ(session.CaseOutputDirectory("phonons"), slugged);
		session.AttachDataset(a, slugged);
		EXPECT_EQ(session.CaseOutputDirectory("phonons"), root / "setup/defectstudio/phonons_B____2");
		session.RenameCase(b, std::string(50, 'Z'));
		EXPECT_EQ(session.CaseOutputDirectory("phonons").filename().Utf8(), "phonons_" + std::string(40, 'Z'));
		session.RenameCase(b, "C-ż /β");
		EXPECT_EQ(session.CaseOutputDirectory("phonons").filename().Utf8(), "phonons_C-____");
		session.Settings().outputDirectory = root / "override";
		EXPECT_EQ(session.CaseOutputDirectory("phonons_emb_24x24x1"), root / "override");
		ASSERT_TRUE(session.ActivateCase(a));
		EXPECT_EQ(session.CaseOutputDirectory("phonons"), plain);
		for (const auto &kind : {"ccd", "ccd_inputs", "phonons_emb_24x24x1"})
		{
			const auto output = session.CaseOutputDirectory(kind);
			session.AttachDataset(a, output);
			ASSERT_TRUE(session.ActivateCase(b));
			session.Settings().outputDirectory = {};
			session.Settings().embeddingMult.clear();
			EXPECT_NE(session.CaseOutputDirectory(kind), output);
			ASSERT_TRUE(session.ActivateCase(a));
		}
	}

	TEST_F(PhononCasesTests, ExistingSummaryProvenancePreventsOverwriteAndOwnProvenanceAllowsIt)
	{
		const auto plain = root / "setup/defectstudio/phonons";
		Summary(plain, root / "setup/gs-a", root / "setup/es-a", "A");
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		(void)session.AddCase("B");
		session.Settings().groundStateDirectory = root / "setup/gs-b";
		session.Settings().excitedStateDirectory = root / "setup/es-b";
		EXPECT_EQ(session.CaseOutputDirectory("phonons"), root / "setup/defectstudio/phonons_B");
		session.Settings().groundStateDirectory = root / "setup/gs-a";
		session.Settings().excitedStateDirectory = root / "setup/es-a";
		EXPECT_EQ(session.CaseOutputDirectory("phonons"), plain);
	}

	TEST_F(PhononCasesTests, ProposalsMatchRelativeProvenanceAndIgnoreEveryAttachedDataset)
	{
		const auto p = root / "defectstudio/phonons", c = root / "defectstudio/ccd", other = root / "defectstudio/other";
		Summary(p, root / "gs", root / "es", "A");
		Summary(c, root / "gs", root / "es", "A CCD", AnalysisDatasetKind::Ccd);
		Summary(other, root / "gs", root / "other-es", "B");
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		session.SetDatasets({*ReadDatasetRef(p), *ReadDatasetRef(c), *ReadDatasetRef(other)});
		const auto a = session.AddCase("A");
		session.Settings().groundStateDirectory = root / "gs";
		session.Settings().excitedStateDirectory = root / "es";
		EXPECT_EQ(session.ProposedDatasets(a).size(), 2u);
		const auto b = session.AddCase("B");
		session.AttachDataset(b, c);
		EXPECT_EQ(session.ProposedDatasets(a), (std::vector<Path>{p}));
		session.AttachDataset(a, c);
		EXPECT_TRUE(session.Cases()[1].datasets.empty());
		EXPECT_EQ(session.DatasetCaseId(c), a);
		session.DetachDataset(a, c);
		EXPECT_EQ(session.ProposedDatasets(a).size(), 2u);
	}

	TEST_F(PhononCasesTests, DatasetVariantsCreateOneCasePerTransitionOnlyOnce)
	{
		const auto p = root / "defectstudio/phonons", emb = root / "defectstudio/phonons_emb", other = root / "defectstudio/other";
		Summary(p, root / "gs", root / "es", "Dimer emb 12x12x1");
		Summary(emb, root / "gs", root / "es", "Dimer emb 24x24x1");
		Summary(other, root / "other-gs", root / "other-es", "Other");
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		session.SetDatasets({*ReadDatasetRef(p), *ReadDatasetRef(emb), *ReadDatasetRef(other)});
		const auto ids = session.CreateCasesFromDatasets();
		ASSERT_EQ(ids.size(), 2u);
		EXPECT_TRUE(session.CreateCasesFromDatasets().empty());
		const auto &cases = session.Cases();
		ASSERT_EQ(cases.size(), 2u);
		for (const auto &item : cases)
		{
			EXPECT_EQ(item.settings.forceConstantDirectory, root / "fc");
			EXPECT_EQ(item.settings.tripletDirectory, root / "triplet");
			EXPECT_EQ(item.settings.bulkForceConstantDirectory, root / "bulk");
			EXPECT_EQ(item.settings.zplFormula, "E_es - E_gs");
			EXPECT_DOUBLE_EQ(item.settings.sigmaMeV, 7.0);
			EXPECT_DOUBLE_EQ(item.settings.gammaMeV, 3.0);
			if (item.name == "Dimer")
			{
				EXPECT_EQ(item.settings.groundStateDirectory, root / "gs");
				EXPECT_EQ(item.settings.excitedStateDirectory, root / "es");
				EXPECT_EQ(item.datasets.size(), 2u);
			}
			else
			{
				EXPECT_EQ(item.name, "Other");
				EXPECT_EQ(item.settings.groundStateDirectory, root / "other-gs");
				EXPECT_EQ(item.datasets, (std::vector<Path>{other}));
			}
		}
	}

	TEST_F(PhononCasesTests, WindowOwnershipSurvivesSwitchAndAnimationStopsOnTheOldWindow)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto a = session.AddCase("A");
		session.Settings().forceConstantDirectory = root / "fc";
		session.Settings().resultDirectory = root / "result";
		session.SetResult(Result());
		std::vector<RendererWindowState> windows(1);
		auto &window = windows.front();
		window.windowId = "window-a";
		window.title = session.CaseWindowTitle();
		window.structure.sourcePath = root / "fc/POSCAR";
		RendererAtomData atom;
		atom.element = "B";
		atom.cartesianPosition = glm::vec3(2.0f, 0.0f, 0.0f);
		window.structure.atoms = {atom};
		session.Update(windows, 0.0, true);
		ASSERT_EQ(session.WindowId(), window.windowId);
		session.StartAnimation(windows.front());
		session.AdvanceAnimation(windows.front(), 0.25);
		const auto b = session.AddCase("B");
		EXPECT_FALSE(session.IsAnimating());
		EXPECT_EQ(windows.front().structure.atoms.front().cartesianPosition, atom.cartesianPosition);
		EXPECT_FALSE(windows.front().modeArrows);
		EXPECT_TRUE(session.WindowId().empty());
		EXPECT_TRUE(session.CachedCaseResult(a));
		session.SetResult(Result());
		session.Settings().forceConstantDirectory = root / "fc";
		EXPECT_FALSE(session.CanBindWindow(windows.front(), root / "fc/POSCAR", root));
		RendererWindowState legacy;
		legacy.structure.atoms = {atom};
		legacy.windowId = "legacy";
		legacy.title = std::string(PhononPanelSession::SupercellWindowTitle);
		legacy.structure.sourcePath = {};
		EXPECT_TRUE(session.CanBindWindow(legacy, {}, root));
		legacy.structure.atoms.front().element = "N";
		EXPECT_FALSE(session.CanBindWindow(legacy, {}, root));
		EXPECT_EQ(session.ActiveCaseId(), b);
		session.SetPersistedState({}, root);
		EXPECT_FALSE(session.CachedCaseResult(a));
	}

	TEST_F(PhononCasesTests, FirstDirectoryCreatesACaseAndMutationsCoalesceIntoOneSave)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		int saves = 0;
		session.SetSaveHandler([&](const auto &) { ++saves; });
		session.Settings().forceConstantDirectory = root / "fc";
		session.SetDirectory("fc", session.Settings().forceConstantDirectory);
		ASSERT_EQ(session.Cases().size(), 1u);
		EXPECT_EQ(session.Cases().front().name, "fc");
		EXPECT_EQ(session.Settings().forceConstantDirectory, root / "fc");
		const auto id = session.ActiveCaseId();
		session.RenameCase(id, "Named");
		session.SetCaseChecked(id, true);
		session.AttachDataset(id, root / "dataset");
		session.FlushChanges();
		EXPECT_EQ(saves, 1);
		EXPECT_TRUE(session.CaseChecked(id));
		EXPECT_EQ(session.Cases().front().name, "Named");
		session.SetPersistedState({}, root);
		EXPECT_FALSE(session.CaseChecked(id));
		session.FlushChanges();
		EXPECT_EQ(saves, 1);
	}

	TEST_F(PhononCasesTests, PreviousFlatSerializerOutputMigratesEveryCaseField)
	{
		PhononPanelSettings legacy;
		const auto values = Settings("legacy");
		static_cast<PhononCaseSettings &>(legacy) = values;
		legacy.seriesStyles["S/A"].lineWeight = 4.0f;
		legacy.comparedDirectories = {root / "compared"};
		auto text = PhononSettingsDetail::SerializeFlat(legacy, root);
		const std::string version = "formatVersion: 2";
		const auto position = text.find(version);
		ASSERT_NE(position, std::string::npos);
		text.replace(position, version.size(), "formatVersion: 1");
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, root, loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.cases.size(), 1u);
		ExpectSettings(loaded, values);
		ExpectSettings(loaded.cases.front().settings, values);
		EXPECT_EQ(loaded.cases.front().datasets, (std::vector<Path>{values.resultDirectory, values.selectedCcdDirectory}));
		EXPECT_EQ(loaded.comparedDirectories, legacy.comparedDirectories);
		EXPECT_FLOAT_EQ(loaded.seriesStyles.at("S/A").lineWeight, 4.0f);
		EXPECT_TRUE(warnings.empty());
	}

	TEST_F(PhononCasesTests, MigrationUsesExplicitDatasetLabelThenGroundFolderThenDefaultName)
	{
		const auto result = root / "defectstudio/phonons";
		Summary(result, root / "gs", root / "es", "Readable defect label");
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		const std::string text = "formatVersion: 1\ninputs: {gsDir: gs}\nresultDir: defectstudio/phonons\n";
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, root, loaded, warnings, error));
		EXPECT_EQ(loaded.cases.front().name, "Readable defect label");
		{
			std::ofstream out((result / "summary.json").Native());
			out << R"({"schema":"ds.phonon_vibronic.v1"})";
		}
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, root, loaded, warnings, error));
		EXPECT_EQ(loaded.cases.front().name, root.filename().Utf8() + "/gs");
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("formatVersion: 1\ninputs: {outputDir: output}\n", root, loaded, warnings, error));
		EXPECT_EQ(loaded.cases.front().name, "Przypadek 1");
	}
}
