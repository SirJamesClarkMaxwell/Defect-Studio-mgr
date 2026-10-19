#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <nlohmann/json.hpp>
#include "Core/Utils/Uuid.hpp"
#include "IO/AnalysisDatasetIO.hpp"
#include "IO/PhononModeIO.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	class PhononModeIOTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_bulk_mode_" + ToString(GenerateUuid())));
		void SetUp() override { std::filesystem::create_directories(root.Native()); }
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		void Write(const Path &directory, const char *name, const std::string &text)
		{
			std::filesystem::create_directories(directory.Native());
			std::ofstream file((directory / name).Native(), std::ios::binary);
			file << text;
			ASSERT_TRUE(file.good());
		}
		nlohmann::json Data()
		{
			return {{"schema", "ds.phonon_mode.v1"}, {"label", "K"}, {"frequency_meV", 159.0},
				{"q", {1.0 / 3, 1.0 / 3, 0}}, {"q_basis", "primitive_reciprocal_reduced"}, {"band", 5}, {"n_bands", 6},
				{"supercell", {3, 3, 1}}, {"commensurate", true}, {"suggested_supercell", {3, 3, 1}},
				{"symbols", {"B", "N"}}, {"masses_amu", {10.8, 14.0}},
				{"cell_angstrom", {{3, 0, 0}, {0, 3, 0}, {0, 0, 1}}},
				{"positions_angstrom", {{0, 0, 0}, {1, 1, 0}}},
				{"real_angstrom", {{1, 0, 0}, {0, 1, 0}}}, {"imag_angstrom", {{0, 0, 1}, {0, 0, 0}}},
				{"partner", nullptr}};
		}
		void Dataset(const Path &directory, bool partner = false)
		{
			auto data = Data();
			if (partner) data["partner"] = {{"band", 4}, {"frequency_meV", 159.1},
				{"real_angstrom", {{0, 1, 0}, {1, 0, 0}}}, {"imag_angstrom", {{0, 0, 0}, {0, 0, 0}}}};
			Write(directory, "vibration.json", data.dump());
			Write(directory, "summary.json", nlohmann::json{{"schema", "ds.phonon_mode.v1"}, {"label", "K"},
				{"input_paths", {{"fc_dir", (root / "fc").Utf8()}}}}.dump());
		}
		void Dispersion(const Path &directory, const Path &fc, double frequency)
		{
			Write(directory, "summary.json", nlohmann::json{{"schema", "ds.phonon_dispersion.v1"}, {"n_bands", 1},
				{"in_plane", true}, {"min_freq_meV", frequency}, {"max_freq_meV", frequency},
				{"ticks", {{{"distance", 0}, {"label", "G"}}}}, {"breaks", nlohmann::json::array()},
				{"input_paths", {{"fc_dir", fc.Utf8()}}}}.dump());
			Write(directory, "dispersion.tsv", "segment\tdistance\tband_0\n0\t0\t" + std::to_string(frequency) + "\n");
		}
	};

	TEST_F(PhononModeIOTests, LoadsGeometryOptionalPartnerAndScanKind)
	{
		const auto directory = root / "defectstudio/bulk_mode";
		Dataset(directory);
		PhononModeField mode;
		std::string error = "old";
		ASSERT_TRUE(PhononModeIO::Load(directory, mode, error)) << error;
		EXPECT_TRUE(error.empty()); EXPECT_EQ(mode.label, "K"); EXPECT_EQ(mode.band, 5);
		EXPECT_EQ(mode.bandCount, 6); EXPECT_EQ(mode.supercell, (std::array<int, 3>{3, 3, 1}));
		EXPECT_TRUE(mode.commensurate); EXPECT_EQ(mode.suggestedSupercell, mode.supercell);
		EXPECT_EQ(mode.symbols, (std::vector<std::string>{"B", "N"})); EXPECT_EQ(mode.masses[1], 14.0);
		EXPECT_EQ(mode.positions[1], glm::dvec3(1, 1, 0)); EXPECT_EQ(mode.cell[2], glm::dvec3(0, 0, 1));
		EXPECT_EQ(mode.imag[0], glm::dvec3(0, 0, 1)); EXPECT_FALSE(mode.partner);
		Dataset(directory, true);
		ASSERT_TRUE(PhononModeIO::Load(directory, mode, error)) << error;
		ASSERT_TRUE(mode.partner); EXPECT_EQ(mode.partner->band, 4);
		EXPECT_DOUBLE_EQ(mode.partner->frequencyMeV, 159.1);
		EXPECT_EQ(mode.partner->real[0], glm::dvec3(0, 1, 0));
		const auto datasets = FindDatasets(root, {});
		ASSERT_EQ(datasets.size(), 1u); EXPECT_EQ(datasets[0].kind, AnalysisDatasetKind::BulkMode);
	}

	TEST_F(PhononModeIOTests, RejectsMalformedFieldsWithoutReplacingOutput)
	{
		const auto directory = root / "mode";
		for (const auto &bad : std::vector<nlohmann::json>{
			nlohmann::json{{"schema", "wrong"}}, {{"q_basis", "unit"}}, {{"band", 6}}, {{"band", 1.5}},
			{{"n_bands", 0}}, {{"frequency_meV", nullptr}}, {{"supercell", {0, 1, 1}}},
			{{"suggested_supercell", {1, 2}}}, {{"masses_amu", {1}}}, {{"masses_amu", {1, -1}}},
			{{"symbols", {"B", ""}}}, {{"positions_angstrom", {{0, 0, 0}}}}, {{"real_angstrom", {{0, 0, 0}, {1, 2}}}},
			{{"imag_angstrom", {{0, 0, 0}}}}, {{"cell_angstrom", {{0, 0, 0}}}},
			{{"partner", {{"band", 5}, {"frequency_meV", 159}, {"real_angstrom", {{1, 0, 0}}}}}}})
		{
			auto data = Data(); data.update(bad);
			Write(directory, "vibration.json", data.dump());
			PhononModeField mode; mode.label = "retained";
			std::string error;
			EXPECT_FALSE(PhononModeIO::Load(directory, mode, error)) << bad;
			EXPECT_FALSE(error.empty()); EXPECT_EQ(mode.label, "retained");
		}
		Write(directory, "vibration.json", "not JSON");
		PhononModeField mode; std::string error;
		EXPECT_FALSE(PhononModeIO::Load(directory, mode, error));
		EXPECT_FALSE(PhononModeIO::Load(root / "missing", mode, error));
	}

	TEST_F(PhononModeIOTests, CasesRoundTripBulkSelectionsAndLegacyGlobalDispersionDefaults)
	{
		PhononPanelSettings settings, loaded;
		PhononCaseSettings first, second;
		first.bulkForceConstantDirectory = root / "fc-a"; first.selectedDispersionDirectory = root / "disp-a";
		first.selectedBulkModeDirectory = root / "mode-a"; first.bulkQ = {0.25, 0.5, 0};
		first.bulkBand = 3; first.bulkSupercell = "4x2x1"; first.animationSource = "bulk_q";
		second = first; second.bulkForceConstantDirectory = root / "fc-b"; second.selectedDispersionDirectory = root / "disp-b";
		second.selectedBulkModeDirectory = root / "mode-b"; second.bulkQ = {0.125, 0, 0}; second.bulkBand = 1; second.bulkSupercell = "auto";
		settings.cases = {{"case-1", "A", {}, first}, {"case-2", "B", {}, second}};
		settings.activeCaseId = "case-1"; static_cast<PhononCaseSettings &>(settings) = first;
		settings.bulkBand = 4;
		std::vector<std::string> warnings; std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(PhononPanelSettingsIO::Serialize(settings, root), root, loaded, warnings, error)) << error;
		ASSERT_TRUE(warnings.empty()); ASSERT_EQ(loaded.cases.size(), 2u);
		EXPECT_EQ(loaded.bulkBand, 4); EXPECT_EQ(loaded.cases[1].settings.bulkBand, 1);
		EXPECT_EQ(loaded.cases[1].settings.bulkQ, second.bulkQ);
		EXPECT_EQ(loaded.cases[1].settings.bulkSupercell, "auto");
		EXPECT_EQ(loaded.cases[0].settings.selectedDispersionDirectory, first.selectedDispersionDirectory);
		EXPECT_EQ(loaded.cases[1].settings.selectedDispersionDirectory, second.selectedDispersionDirectory);
		EXPECT_EQ(loaded.cases[1].settings.selectedBulkModeDirectory, second.selectedBulkModeDirectory);
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("formatVersion: 2\ndatasets: {dispersion: old}\ncases:\n"
			"- {id: case-1, name: A, datasets: []}\n- {id: case-2, name: B, datasets: []}\n"
			"- {id: case-3, name: C, datasets: [], dispersion: own}\n", root, loaded, warnings, error));
		EXPECT_TRUE(warnings.empty()); ASSERT_EQ(loaded.cases.size(), 3u);
		EXPECT_EQ(loaded.cases[0].settings.selectedDispersionDirectory, root / "old");
		EXPECT_EQ(loaded.cases[1].settings.selectedDispersionDirectory, root / "old");
		EXPECT_EQ(loaded.cases[2].settings.selectedDispersionDirectory, root / "own");
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("view: {bulkQ: [0, .nan, 0], bulkBand: -1, bulkSupercell: wrong}", root, loaded, warnings, error));
		EXPECT_EQ(warnings.size(), 3u); EXPECT_EQ(loaded.bulkQ, glm::dvec3(0));
		EXPECT_EQ(loaded.bulkBand, 0); EXPECT_EQ(loaded.bulkSupercell, "auto");
	}

	TEST_F(PhononModeIOTests, SessionSwitchesBulkPerCaseAndLoadsWithoutStartingAnything)
	{
		const auto firstMode = root / "mode-a", secondMode = root / "mode-b";
		Dataset(firstMode); Dataset(secondMode, true);
		Dispersion(root / "disp-a", root / "fc-a", 10);
		Dispersion(root / "disp-b", root / "fc-b", 20);
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto a = session.AddCase("A"); session.Settings().bulkForceConstantDirectory = root / "fc-a";
		ASSERT_TRUE(session.LoadBulkMode(firstMode)); session.Settings().bulkSupercell = "auto";
		const auto b = session.AddCase("B"); session.Settings().bulkForceConstantDirectory = root / "fc-b";
		ASSERT_TRUE(session.LoadBulkMode(secondMode)); session.Settings().bulkQ = {0.125, 0, 0};
		session.SetDatasets({*ReadDatasetRef(root / "disp-a"), *ReadDatasetRef(root / "disp-b")});
		EXPECT_EQ(session.Settings().selectedDispersionDirectory, root / "disp-b");
		ASSERT_TRUE(session.Dispersion()); EXPECT_DOUBLE_EQ(session.Dispersion()->bandsMeV[0][0], 20);
		ASSERT_TRUE(session.ActivateCase(a));
		EXPECT_EQ(session.Settings().selectedDispersionDirectory, root / "disp-a");
		ASSERT_TRUE(session.Dispersion()); EXPECT_DOUBLE_EQ(session.Dispersion()->bandsMeV[0][0], 10);
		EXPECT_EQ(session.Settings().selectedBulkModeDirectory, firstMode);
		EXPECT_EQ(session.Settings().bulkSupercell, "auto");
		ASSERT_TRUE(session.BulkMode()); EXPECT_FALSE(session.BulkMode()->partner);
		ASSERT_TRUE(session.ActivateCase(b));
		EXPECT_EQ(session.Settings().bulkQ, glm::dvec3(0.125, 0, 0));
		ASSERT_TRUE(session.BulkMode()); EXPECT_TRUE(session.BulkMode()->partner);
		const auto snapshot = session.Settings();
		session.SetPersistedState(snapshot, root);
		EXPECT_FALSE(session.IsBusy()); EXPECT_FALSE(session.IsAnimating());
		EXPECT_EQ(session.Settings().selectedDispersionDirectory, root / "disp-b");
		EXPECT_EQ(session.Settings().bulkQ, glm::dvec3(0.125, 0, 0));
		const auto c = session.AddCase("shared"); session.Settings().bulkForceConstantDirectory = root / "fc-a";
		ASSERT_TRUE(session.ActivateCase(b)); ASSERT_TRUE(session.ActivateCase(c));
		EXPECT_EQ(session.Settings().selectedDispersionDirectory, root / "disp-a");
	}

	TEST_F(PhononModeIOTests, BulkAnimationRestoresExactlyAndNeverBindsOrChangesDefectWindow)
	{
		Dataset(root / "mode", true);
		PhononPanelSession session;
		session.SetPersistedState({}, root); (void)session.AddCase("A");
		EXPECT_EQ(session.BulkModeBlocker(), "Ustaw katalog FC bulku w przypadku.");
		EXPECT_TRUE(PhononPanelSession::ValidBulkSupercell("auto"));
		EXPECT_TRUE(PhononPanelSession::ValidBulkSupercell("3x3x1"));
		EXPECT_FALSE(PhononPanelSession::ValidBulkSupercell("0x3x1"));
		EXPECT_FALSE(PhononPanelSession::ValidBulkSupercell("999999999999999999999x1x1"));
		session.Settings().bulkForceConstantDirectory = root / "fc";
		ASSERT_TRUE(session.LoadBulkMode(root / "mode"));
		session.SetAnimationSource("bulk_q");
		std::vector<RendererWindowState> windows(2);
		for (int w = 0; w < 2; ++w)
		{
			windows[w].windowId = std::to_string(w);
			windows[w].title = std::string(PhononPanelSession::SupercellWindowTitle);
			windows[w].structure.sourcePath = root / (w == 0 ? "defect/POSCAR" : "mode/POSCAR");
			for (const auto symbol : {"B", "N"})
			{
				RendererAtomData atom; atom.element = symbol; atom.cartesianPosition = glm::vec3(3, 4, 5);
				windows[w].structure.atoms.push_back(atom);
			}
		}
		session.Update(windows, 0, true); ASSERT_EQ(session.WindowId(), "1");
		const auto rest = windows[1].structure.atoms[0].cartesianPosition;
		session.StartAnimation(windows[0]); EXPECT_FALSE(session.IsAnimating());
		session.StartAnimation(windows[1]); ASSERT_TRUE(session.IsAnimating());
		session.SetPhase(windows[1], std::numbers::pi / 2);
		EXPECT_NE(windows[1].structure.atoms[0].cartesianPosition, rest);
		EXPECT_EQ(windows[0].structure.atoms[0].cartesianPosition, rest); EXPECT_FALSE(windows[0].modeArrows);
		session.SetMixAngle(std::numbers::pi / 2);
		session.SetPhase(windows[1], std::numbers::pi / 2);
		EXPECT_GT(windows[1].structure.atoms[0].cartesianPosition.y, rest.y);
		session.StopAnimation(); EXPECT_EQ(windows[1].structure.atoms[0].cartesianPosition, rest);
		session.StartAnimation(windows[1]); session.SetPhase(windows[1], 1.0);
		session.SetAnimationSource("mode");
		EXPECT_EQ(windows[1].structure.atoms[0].cartesianPosition, rest); EXPECT_FALSE(windows[1].modeArrows);
		std::string error;
		EXPECT_FALSE(session.CanBindWindow(windows[1], root / "defect/POSCAR", root));
		EXPECT_FALSE(session.ApplyArrows(windows[1], error));
	}

	TEST_F(PhononModeIOTests, NewModeRequestCancelsPreviousAndProjectReplacementCancelsNewest)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		const auto gate = CreateRef<Gate>();
		struct Release { Ref<Gate> gate; ~Release() { gate->Open(); } } release{gate};
		const auto blocking = jobs->Submit(CreateRef<BlockingJob>(gate));
		ASSERT_TRUE(WaitUntil([&] { return jobs->GetJob(blocking)->status == JobStatus::Running; }, Time::Milliseconds(5000)));
		PhononPanelSession session(jobs);
		session.SetPersistedState({}, root); (void)session.AddCase("A");
		session.Settings().bulkForceConstantDirectory = root;
		session.SubmitBulkMode(); ASSERT_TRUE(session.IsBulkModeRunning());
		JobId first = 0;
		for (const auto &job : jobs->GetAllJobs()) if (job.type == "phonon-mode") first = job.id;
		ASSERT_NE(first, 0u);
		session.Settings().bulkQ = {0.5, 0, 0}; session.SubmitBulkMode();
		EXPECT_TRUE(jobs->GetJob(first)->cancellationRequested);
		EXPECT_TRUE(session.IsBulkModeRunning());
		JobId second = 0;
		for (const auto &job : jobs->GetAllJobs()) if (job.type == "phonon-mode" && job.id != first) second = job.id;
		ASSERT_NE(second, 0u); EXPECT_FALSE(jobs->GetJob(second)->cancellationRequested);
		session.SetPersistedState({}, {});
		EXPECT_TRUE(jobs->GetJob(second)->cancellationRequested); EXPECT_FALSE(session.IsBulkModeRunning());
		gate->Open();
		ASSERT_TRUE(WaitUntil([&] { return jobs->GetJob(second)->status == JobStatus::Cancelled; }, Time::Milliseconds(5000)));
		EXPECT_EQ(jobs->GetJob(first)->status, JobStatus::Cancelled);
		std::vector<RendererWindowState> windows;
		session.Update(windows, 0, false); EXPECT_FALSE(session.BulkMode());
	}
}
