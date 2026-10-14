#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <numbers>

#include "Core/Utils/Uuid.hpp"
#include "Presentation/PanelDockRegions.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		PhononAnalysisResult OneAtom()
		{
			PhononAnalysisResult result;
			result.symbols = {"B"};
			result.positionsAngstrom = {glm::dvec3(0)};
			result.massesAmu = {1};
			for (int i = 0; i < 2; ++i)
			{
				PhononMode mode;
				mode.index = i;
				mode.frequencyMeV = 50 + 50 * i;
				mode.huangRhys = 0.5;
				mode.irrep = "A1";
				result.modes.push_back(mode);
			}
			result.eigenvectors = {1, 0, 0, 0, 1, 0};
			result.participation = {1, 1};
			return result;
		}

		std::vector<RendererWindowState> Windows()
		{
			// RendererWindowState is move-only: build it in place.
			std::vector<RendererWindowState> windows(1);
			RendererWindowState &window = windows.front();
			window.windowId = "phonon-test-window";
			window.title = std::string(PhononPanelSession::SupercellWindowTitle);
			RendererAtomData atom;
			atom.element = "B";
			atom.cartesianPosition = glm::vec3(10, 0, 0);
			window.structure.atoms.push_back(atom);
			return windows;
		}

		struct Workspace
		{
			Path root = Path::FromResolved(std::filesystem::temp_directory_path()) / ("ds_phonon_views_" + ToString(GenerateUuid()));
			~Workspace() { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
			void Write(const Path &directory, const char *filename, const std::string &text)
			{
				std::filesystem::create_directories(directory.Native());
				std::ofstream file((directory / filename).Native());
				file << text;
			}
		};

		struct OpenGateOnExit
		{
			Ref<Gate> gate;
			~OpenGateOnExit() { gate->Open(); }
		};

		void WriteNpy(const Path &file, const std::string &shape, const std::vector<float> &values)
		{
			std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': " + shape + ", }";
			header.append((64 - (10 + header.size() + 1) % 64) % 64, ' ');
			header.push_back('\n');
			std::ofstream out(file.Native(), std::ios::binary);
			out.write("\x93NUMPY\x01\x00", 8);
			out.put(static_cast<char>(header.size() & 255));
			out.put(static_cast<char>(header.size() >> 8));
			out << header;
			for (float value : values)
				for (int byte = 0; byte < 4; ++byte)
					out.put(static_cast<char>((std::bit_cast<std::uint32_t>(value) >> (8 * byte)) & 255));
		}
	}

	TEST(PhononViewsTests, ViewsAndCloneShareModeSelectionAndIndependentVisibility)
	{
		const auto session = CreateRef<PhononPanelSession>();
		session->SetResult(OneAtom());
		PhononModesPanel modes(session, PhononModesPanel::View::Modes, "Fonony: mody", true);
		PhononModesPanel plots(session, PhononModesPanel::View::Plots, "Fonony: wykresy");
		const auto clone = std::dynamic_pointer_cast<PhononModesPanel>(modes.Clone());
		ASSERT_NE(clone, nullptr);
		EXPECT_EQ(clone->Session(), session);
		EXPECT_EQ(clone->GetView(), PhononModesPanel::View::Modes);
		modes.Session()->SelectMode(1);
		EXPECT_EQ(plots.Session()->SelectedMode(), 1u);
		EXPECT_EQ(clone->Session()->SelectedMode(), 1u);
		clone->SetVisible(false);
		EXPECT_TRUE(modes.IsVisible());
		EXPECT_FALSE(plots.IsVisible());
		EXPECT_STREQ(modes.GetSubcategory(), "Fonony");
	}

	TEST(PhononViewsTests, SeveralChangesSaveOnceAtFlushAndProjectReplacementDropsEdits)
	{
		PhononPanelSession session;
		int saves = 0;
		session.SetSaveHandler([&](const PhononPanelSettings &) { ++saves; });
		session.FlushChanges();
		EXPECT_EQ(saves, 0);
		session.Changed(); session.Changed(); session.Changed();
		EXPECT_EQ(saves, 0);
		session.FlushChanges(); session.FlushChanges();
		EXPECT_EQ(saves, 1);
		session.Changed();
		session.SetPersistedState({}, {});
		session.FlushChanges();
		EXPECT_EQ(saves, 1);
	}

	TEST(PhononViewsTests, HiddenViewsRestorePositionsAndClearArrowsButClosingPlotsKeepsAnimation)
	{
		const auto session = CreateRef<PhononPanelSession>();
		session->SetResult(OneAtom());
		session->Settings().animationAmplitudeAngstrom = 0.2;
		session->Settings().animationPeriodSeconds = 2;
		auto windows = Windows();
		session->Update(windows, 0, true);
		ASSERT_EQ(session->WindowId(), windows[0].windowId);
		session->StartAnimation(windows[0]);
		PhononModesPanel modes(session, PhononModesPanel::View::Modes, "Fonony: mody", true);
		PhononModesPanel plots(session, PhononModesPanel::View::Plots, "Fonony: wykresy", true);
		plots.SetVisible(false);
		session->Update(windows, 0.5, modes.IsVisible() || plots.IsVisible());
		EXPECT_TRUE(session->IsAnimating());
		EXPECT_NEAR(windows[0].structure.atoms[0].cartesianPosition.x, 10.2f, 1e-6f);
		ASSERT_TRUE(windows[0].modeArrows);
		modes.SetVisible(false);
		session->Update(windows, 0.5, false);
		EXPECT_FALSE(session->IsAnimating());
		EXPECT_FALSE(session->RestPositionsCaptured());
		EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, glm::vec3(10, 0, 0));
		EXPECT_FALSE(windows[0].modeArrows);
		session->Update(windows, 0.5, false);
		EXPECT_FALSE(windows[0].modeArrows);
	}

	TEST(PhononViewsTests, ResultComparedSetCcdAndZplRevisePlotsButModeSelectionDoesNot)
	{
		PhononPanelSession session;
		auto revision = session.PlotRevision();
		session.SetResult(OneAtom());
		EXPECT_GT(session.PlotRevision(), revision);
		revision = session.PlotRevision();
		session.SelectMode(1);
		EXPECT_EQ(session.PlotRevision(), revision);
		const auto directory = Path::FromUtf8("phonon-view-comparison");
		session.AddDataset({AnalysisDatasetKind::Phonons, directory, "compared", "ds.phonon_vibronic.v1"});
		revision = session.PlotRevision();
		session.SetCompared(directory, true);
		EXPECT_GT(session.PlotRevision(), revision);
		revision = session.PlotRevision();
		session.SetCompared(directory, true);
		EXPECT_EQ(session.PlotRevision(), revision);
		session.SetCompared(directory, false);
		EXPECT_GT(session.PlotRevision(), revision);
		revision = session.PlotRevision();
		session.SetCcdResult({});
		EXPECT_GT(session.PlotRevision(), revision);
		revision = session.PlotRevision();
		session.SetPlZpl("formula");
		EXPECT_GT(session.PlotRevision(), revision);
		revision = session.PlotRevision();
		session.Settings().plZpl = "computed";
		session.Changed();
		EXPECT_GT(session.PlotRevision(), revision);
	}

	TEST(PhononViewsTests, PersistedStateCancelsEachJobAndScanAndClearsBinding)
	{
		Workspace workspace;
		for (const auto &name : {"gs", "es", "cross", "fc", "bulk"})
			std::filesystem::create_directories((workspace.root / name).Native());
		for (int kind = 0; kind < 4; ++kind)
		{
			SCOPED_TRACE(kind);
			const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
			const auto gate = CreateRef<Gate>();
			OpenGateOnExit release{gate};
			const auto blocking = jobs->Submit(CreateRef<BlockingJob>(gate));
			ASSERT_TRUE(WaitUntil([&] { return jobs->GetJob(blocking)->status == JobStatus::Running; }, Time::Milliseconds(5000)));
			PhononPanelSession session(jobs);
			PhononPanelSettings settings;
			settings.forceConstantDirectory = workspace.root / "fc";
			settings.groundStateDirectory = workspace.root / "gs";
			settings.excitedStateDirectory = workspace.root / "es";
			settings.groundAtExcitedDirectory = workspace.root / "cross";
			settings.bulkForceConstantDirectory = workspace.root / "bulk";
			settings.zplFormula = "1.945";
			settings.outputDirectory = settings.ccdOutputDirectory = settings.ccdAnalysisOutputDirectory = settings.dispersionDirectory = workspace.root / "output";
			session.SetPersistedState(settings, workspace.root);
			session.SetProjectRoots({});
			session.SetResult(OneAtom());
			auto windows = Windows();
			session.Update(windows, 0, true);
			session.StartAnimation(windows[0]);
			session.SetPhase(windows[0], std::numbers::pi / 2);
			if (kind == 0) session.SubmitComputation();
			else if (kind == 1) session.SubmitCcdInputs();
			else if (kind == 2) session.SubmitCcdAnalysis();
			else session.SubmitDispersion();
			ASSERT_TRUE(session.IsBusy());
			ASSERT_TRUE(session.IsScanningDatasets());
			ASSERT_EQ(jobs->GetAllJobs().size(), 3u);
			session.SetPersistedState({}, {});
			EXPECT_FALSE(session.IsBusy());
			EXPECT_FALSE(session.IsScanningDatasets());
			EXPECT_TRUE(session.WindowId().empty());
			EXPECT_FALSE(session.IsAnimating());
			EXPECT_FALSE(windows[0].modeArrows);
			EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, glm::vec3(10, 0, 0));
			gate->Open();
			ASSERT_TRUE(WaitUntil([&] {
				return std::ranges::all_of(jobs->GetAllJobs(), [](const auto &job) {
					return job.status == JobStatus::Completed || job.status == JobStatus::Cancelled || job.status == JobStatus::Failed;
				});
			}, Time::Milliseconds(5000)));
			for (const auto &job : jobs->GetAllJobs())
				if (job.id != blocking) EXPECT_EQ(job.status, JobStatus::Cancelled);
			session.Update(windows, 0.5, false);
			EXPECT_EQ(session.Result(), nullptr);
			EXPECT_TRUE(session.Datasets().empty());
			EXPECT_TRUE(session.WindowId().empty());
		}
	}

	TEST(PhononViewsTests, ComparedSpectraRemainAvailableWithoutActivePhonons)
	{
		Workspace workspace;
		const Path dataset = workspace.root / "phonons";
		workspace.Write(dataset, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"A",
			"s_total":1,"debye_waller":0.36,"stable":true,"sigma_meV":6,"gamma_meV":2,
			"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
			"zpl_eV":{"computed":null,"computed_spin_purified":null,"experimental":null,"formula":1.945},
			"zpl_formula":{"text":"1.945"}})");
		workspace.Write(dataset, "pl.tsv", "photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula\n1.945\tnan\tnan\tnan\t1\n");
		PhononPanelSession session;
		session.AddDataset(*ReadDatasetRef(dataset));
		session.SetCompared(dataset, true);
		session.ClearResult();
		std::string error;
		const auto compared = session.Spectra(dataset, error);
		ASSERT_TRUE(compared) << error;
		EXPECT_TRUE(session.IsCompared(dataset));
		EXPECT_EQ(session.Result(), nullptr);
		EXPECT_EQ(PhononPanelSession::PlCurve(compared->get(), "formula"), std::vector<double>{1});
		EXPECT_FALSE(session.PlotModes(dataset, error));
		EXPECT_FALSE(error.empty());
		EXPECT_TRUE(session.Spectra(dataset, error)); // A failed mode upgrade preserves the spectra cache.
		EXPECT_TRUE(error.empty());
	}

	TEST(PhononViewsTests, ComparedModesUpgradeTheExistingCacheAndRefreshWhenDatasetChanges)
	{
		Workspace workspace;
		const Path dataset = workspace.root / "phonons";
		workspace.Write(dataset, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","n_atoms":1,"n_modes":3,
			"imaginary_modes":[],"s_total":1,"debye_waller":0.36,"stable":true,"sigma_meV":6,"gamma_meV":2,
			"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
			"zpl_eV":{"computed":null,"computed_spin_purified":null,"experimental":null}})");
		PhononPanelSession session;
		session.AddDataset(*ReadDatasetRef(dataset));
		session.SetCompared(dataset, true);
		std::string error;
		ASSERT_TRUE(session.Spectra(dataset, error));
		EXPECT_FALSE(session.PlotModes(dataset, error));
		workspace.Write(dataset, "structure.json", R"({"symbols":["B"],"masses_amu":[1],"positions_A":[[0,0,0]],
			"cell_A":[[1,0,0],[0,1,0],[0,0,1]]})");
		workspace.Write(dataset, "modes.tsv", "index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
			"0\t50\tA1\t1\t1\t0.5\t0.1\t0\n1\t60\tE\t1\t1\t0.3\t0.1\t0\n2\t70\tE\t1\t1\t0.2\t0.1\t0\n");
		WriteNpy(dataset / "eigenvectors.npy", "(3, 1, 3)", {1, 0, 0, 0, 1, 0, 0, 0, 1});
		WriteNpy(dataset / "participation.npy", "(3, 1)", {1, 1, 1});
		EXPECT_FALSE(session.PlotModes(dataset, error)); // Failed mode reads are cached until invalidated.
		session.AddDataset(*ReadDatasetRef(dataset));
		const auto modes = session.PlotModes(dataset, error);
		ASSERT_TRUE(modes) << error;
		EXPECT_EQ(modes->get().modes.size(), 3u);
		EXPECT_DOUBLE_EQ(modes->get().modes[0].beta, 1);
		EXPECT_EQ(session.Result(), nullptr);
		ASSERT_TRUE(session.Spectra(dataset, error));
		EXPECT_EQ(session.Spectra(dataset, error)->get().modes.size(), 3u);
		const auto revision = session.PlotRevision();
		session.SetDirectory("result", dataset);
		EXPECT_GT(session.PlotRevision(), revision);
		ASSERT_NE(session.Result(), nullptr);
		EXPECT_EQ(session.CurrentDataset(), dataset);
	}

	TEST(PhononViewsTests, HiddenUpdateCommitsCompletedScanOnlyOnceOnTheMainThread)
	{
		Workspace workspace;
		workspace.Write(workspace.root / "defectstudio" / "phonons", "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"A"})");
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		PhononPanelSession session(jobs);
		session.SetPersistedState({}, workspace.root);
		session.SetProjectRoots({});
		ASSERT_TRUE(WaitUntil([&] {
			return std::ranges::all_of(jobs->GetAllJobs(), [](const auto &job) { return job.status == JobStatus::Completed; });
		}, Time::Milliseconds(5000)));
		EXPECT_TRUE(session.Datasets().empty());
		auto windows = Windows();
		session.Update(windows, 0.5, false);
		ASSERT_EQ(session.Datasets().size(), 1u);
		const auto revision = session.PlotRevision();
		session.Update(windows, 0.5, false);
		EXPECT_EQ(session.Datasets().size(), 1u);
		EXPECT_EQ(session.PlotRevision(), revision);
	}

	TEST(PhononViewsTests, MixAngleChangesTheNextFrameWithoutStoppingAndSelectionResetsIt)
	{
		PhononPanelSession session;
		auto result = OneAtom();
		for (auto &mode : result.modes) mode.irrep = "E";
		result.modes[1].frequencyMeV = result.modes[0].frequencyMeV + 0.2;
		session.SetResult(result);
		session.Settings().animationAmplitudeAngstrom = 0.2;
		session.Settings().arrowLengthAngstrom = 2.0;
		auto windows = Windows();
		session.Update(windows, 0, true);
		const auto rest = windows[0].structure.atoms[0].cartesianPosition;
		session.StartAnimation(windows[0]);
		session.SetPhase(windows[0], std::numbers::pi / 2.0);
		const auto firstFrame = windows[0].structure.atoms[0].cartesianPosition;
		const auto revision = session.PlotRevision();
		ASSERT_EQ(session.DegeneratePartnerOfSelected(), 1u);
		session.SetMixAngle(std::numbers::pi / 4.0);
		EXPECT_TRUE(session.IsAnimating());
		EXPECT_EQ(session.PlotRevision(), revision);
		EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, firstFrame);
		session.SetPhase(windows[0], std::numbers::pi / 2.0);
		const auto mixed = windows[0].structure.atoms[0].cartesianPosition;
		EXPECT_NEAR(mixed.x, rest.x + 0.2 / std::sqrt(2.0), 1e-6);
		EXPECT_NEAR(mixed.y, rest.y + 0.2 / std::sqrt(2.0), 1e-6);
		ASSERT_TRUE(windows[0].modeArrows);
		ASSERT_EQ(windows[0].modeArrows->arrows.size(), 1u);
		EXPECT_NEAR(windows[0].modeArrows->arrows[0].vectorAngstrom.y, 2.0 / std::sqrt(2.0), 1e-6);
		session.SetMixAngle(std::numbers::pi / 2.0);
		session.SetPhase(windows[0], std::numbers::pi / 2.0);
		EXPECT_NEAR(windows[0].structure.atoms[0].cartesianPosition.x, rest.x, 1e-6);
		EXPECT_NEAR(windows[0].structure.atoms[0].cartesianPosition.y, rest.y + 0.2, 1e-6);
		session.SelectMode(1);
		EXPECT_DOUBLE_EQ(session.MixAngle(), 0.0);
		EXPECT_TRUE(session.IsAnimating());
		session.StopAnimation(windows[0]);
		EXPECT_FALSE(session.RestPositionsCaptured());
		EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, rest);
	}

	TEST(PhononViewsTests, FieldTracksLegacySettingsSelectionAndReplacementAtTheSameIndex)
	{
		PhononPanelSession session;
		auto result = OneAtom();
		session.SetResult(result);
		session.SetMixAngle(std::numbers::pi / 4.0);
		session.Settings().selectedMode = 1;
		EXPECT_DOUBLE_EQ(session.MixAngle(), 0.0);
		ASSERT_EQ(session.Field().real.size(), 1u);
		EXPECT_EQ(session.Field().real[0], glm::dvec3(0, 1, 0));
		session.SetMixAngle(std::numbers::pi / 4.0);
		result.eigenvectors = {1, 0, 0, 0, 0, 1};
		session.SetResult(result);
		EXPECT_DOUBLE_EQ(session.MixAngle(), 0.0);
		EXPECT_EQ(session.Field().real[0], glm::dvec3(0, 0, 1));
		session.ClearResult();
		EXPECT_TRUE(session.Field().real.empty());
		EXPECT_DOUBLE_EQ(session.MixAngle(), 0.0);
		EXPECT_FALSE(session.DegeneratePartnerOfSelected());
	}

	TEST(PhononViewsTests, MixedAnimationRestoresPositionsWhenHiddenAndOnProjectReplacement)
	{
		PhononPanelSession session;
		session.SetResult(OneAtom());
		auto windows = Windows();
		const auto rest = windows[0].structure.atoms[0].cartesianPosition;
		session.Update(windows, 0, true);
		session.StartAnimation(windows[0]);
		session.SetMixAngle(std::numbers::pi / 4.0);
		session.SetPhase(windows[0], std::numbers::pi / 2.0);
		session.Update(windows, 0, false);
		EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, rest);
		EXPECT_FALSE(session.IsAnimating());
		EXPECT_FALSE(windows[0].modeArrows);
		session.Update(windows, 0, true);
		session.StartAnimation(windows[0]);
		session.SetPhase(windows[0], std::numbers::pi / 2.0);
		session.SetPersistedState({}, {});
		EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, rest);
		EXPECT_FALSE(session.IsAnimating());
		EXPECT_TRUE(session.Field().real.empty());
		EXPECT_DOUBLE_EQ(session.MixAngle(), 0.0);
	}

	TEST(PhononViewsTests, MixedAnimationRestoresPositionsAndDropsFieldOnCaseSwitchWithoutResults)
	{
		PhononPanelSession session;
		const auto first = session.AddCase("first");
		const auto second = session.AddCase("second");
		ASSERT_TRUE(session.ActivateCase(first));
		session.SetResult(OneAtom());
		auto windows = Windows();
		session.Update(windows, 0, true);
		const auto rest = windows[0].structure.atoms[0].cartesianPosition;
		session.StartAnimation(windows[0]);
		session.SetMixAngle(std::numbers::pi / 4.0);
		session.SetPhase(windows[0], std::numbers::pi / 2.0);
		ASSERT_TRUE(session.ActivateCase(second));
		EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, rest);
		EXPECT_FALSE(session.IsAnimating());
		EXPECT_TRUE(session.Field().real.empty());
		EXPECT_DOUBLE_EQ(session.MixAngle(), 0.0);
	}

	TEST(PhononViewsTests, PhononViewsUseExistingLeftBottomAndRightDockRegions)
	{
		EXPECT_EQ(AnalysisDockRegion("Fonony"), DockRegion::Left);
		EXPECT_EQ(AnalysisDockRegion("Fonony: mody"), DockRegion::Bottom);
		EXPECT_EQ(AnalysisDockRegion("Fonony: wykresy"), DockRegion::Right);
	}
}
