#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include "Core/Utils/Uuid.hpp"
#include "Domain/Vibrations/VibrationField.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio::Tests
{
	struct PhononServerTestAccess
	{
		static void Queue(PhononPanelSession &session, std::vector<ServerHrResult> hits)
		{
			session.m_CaseJobSubmitter = [](const Ref<IJob> &) { return JobId{1}; };
			session.m_ServerResults = std::move(hits);
			session.m_ServerChecked.assign(session.m_ServerResults.size(), true);
			session.importServerResults();
		}
		static PhononAnalysisRequest Current(const PhononPanelSession &session)
		{
			return std::get<PhononAnalysisRequest>(session.m_BatchEntry->request);
		}
		static void FailCurrent(PhononPanelSession &session)
		{
			session.finishCaseJob(PhononBatchStep::Phonons, session.m_PendingCaseId, Current(session).outputDirectory,
				JobStatus::Failed, "test failure");
			session.m_PendingJob.reset(); session.m_PendingJobId = 0;
			session.submitNextBatch();
		}
		static const PhononCaseRun &Run(const PhononPanelSession &session, const Path &hr)
		{
			return session.m_ServerRuns.at(hr.Utf8());
		}
		static Path Output(const PhononPanelSession &session, const ServerHrResult &hit) { return session.serverOutputPath(hit); }
	};

	TEST(PhononServerSessionTests, MissingVectorsDisableEveryModeConsumerEvenIfArraysRemain)
	{
		PhononAnalysisResult result;
		result.hasEigenvectors = false;
		result.source = "server";
		result.symbols = {"C"}; result.massesAmu = {12}; result.positionsAngstrom = {glm::dvec3(1.0)};
		PhononMode mode; mode.frequencyMeV = 100; mode.huangRhys = 1;
		result.modes = {mode}; result.eigenvectors = {1, 0, 0}; result.participation = {1};
		EXPECT_TRUE(BuildModeArrows(result, 0, 0, 1).empty());
		EXPECT_EQ(AtomsCarryingFraction(result, 0, .9), 0u);
		EXPECT_EQ(AnimatedPositions(result, 0, 1, 1), result.positionsAngstrom);
		EXPECT_TRUE(DefectModeField(result, 0).real.empty());
		PhononPanelSession session;
		session.SetResult(result);
		EXPECT_TRUE(session.Field().real.empty());
		RendererWindowState window;
		std::string error;
		EXPECT_FALSE(session.WindowMatches(window, error));
		EXPECT_FALSE(error.empty());
		EXPECT_FALSE(session.CanBindWindow(window, {}, {}));
		session.StartAnimation(window);
		EXPECT_FALSE(session.IsAnimating());
	}

	TEST(PhononServerSessionTests, UnknownParticipationSortsLastAndExportsAsUnknown)
	{
		PhononAnalysisResult result;
		result.hasEigenvectors = false;
		result.modes.resize(3);
		result.modes[0].beta = std::numeric_limits<double>::quiet_NaN();
		result.modes[1].beta = 7; result.modes[2].beta = 1;
		std::vector<std::size_t> modes = {0, 1, 2};
		SortPhononModes(result, modes, 3, true);
		EXPECT_EQ(modes, (std::vector<std::size_t>{2, 1, 0}));
		SortPhononModes(result, modes, 3, false);
		EXPECT_EQ(modes, (std::vector<std::size_t>{1, 2, 0}));
		const auto columns = PhononModesColumns(result, modes, .9, {});
		for (const auto value : columns[6].values) EXPECT_TRUE(std::isnan(value));
	}

	TEST(PhononServerSessionTests, ReimportReusesItsDirectoryAndKeepsOtherSystemsSeparate)
	{
		const auto root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_server_reimport_" + ToString(GenerateUuid())));
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto system = root / "server/9x9x1";
		ServerHrResult hit{system / "spectral-function/12x12x1/displacements", system, {12, 12, 1}, "displacements", "displacements", {}};
		const auto output = PhononServerTestAccess::Output(session, hit);
		std::filesystem::create_directories(output.Native());
		const nlohmann::json summary = {{"schema", "ds.phonon_vibronic.v1"}, {"input_paths", {{"hr_dir", hit.hrDirectory.Utf8()}}}};
		std::ofstream((output / "summary.json").Native()) << summary.dump();
		EXPECT_EQ(PhononServerTestAccess::Output(session, hit), output);
		hit.hrDirectory = root / "other/9x9x1/spectral-function/12x12x1/displacements";
		EXPECT_NE(PhononServerTestAccess::Output(session, hit), output);
		std::filesystem::remove_all(root.Native());
	}

	TEST(PhononServerSessionTests, ServerHitsShareTheBatchQueueWithoutSwitchingCasesAndKeepEntryStatus)
	{
		const auto root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_server_queue_" + ToString(GenerateUuid())));
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto a = session.AddCase("A");
		session.Settings().sigmaMeV = 2; session.Settings().gammaMeV = .5; session.Settings().zplFormula = "1.945";
		session.Settings().sigmaMaxMeV = 8;
		const Path systemA = root / "server/a/9x9x1", systemB = root / "server/b/9x9x1";
		ServerHrResult first{systemA / "spectral-function/12x12x1/displacements", systemA, {12, 12, 1}, "displacements", "displacements", {}};
		ServerHrResult second{systemB / "spectral-function/12x12x1/displacements", systemB, {12, 12, 1}, "displacements", "displacements", {}};
		PhononServerTestAccess::Queue(session, {first, second});
		ASSERT_TRUE(session.IsBatchRunning());
		const auto request = PhononServerTestAccess::Current(session);
		EXPECT_EQ(request.serverHrDirectory, first.hrDirectory);
		EXPECT_DOUBLE_EQ(request.sigmaMeV, 2); EXPECT_DOUBLE_EQ(request.gammaMeV, .5);
		EXPECT_EQ(request.sigmaMaxMeV, 8);
		EXPECT_EQ(request.zplFormula, "1.945");
		EXPECT_TRUE(request.forceConstantDirectory.Empty());
		EXPECT_EQ(request.outputDirectory, root / "defectstudio/phonons/server/9x9x1/12x12x1/displacements");
		const auto b = session.AddCase("B");
		PhononServerTestAccess::FailCurrent(session);
		EXPECT_EQ(session.ActiveCaseId(), b);
		EXPECT_EQ(PhononServerTestAccess::Run(session, first.hrDirectory).status, PhononCaseStatus::Failed);
		const auto next = PhononServerTestAccess::Current(session);
		EXPECT_EQ(next.serverHrDirectory, second.hrDirectory);
		EXPECT_NE(next.outputDirectory, request.outputDirectory);
		EXPECT_EQ(PhononServerTestAccess::Run(session, second.hrDirectory).status, PhononCaseStatus::Running);
		session.CancelBatch();
		PhononServerTestAccess::FailCurrent(session);
		EXPECT_EQ(PhononServerTestAccess::Run(session, second.hrDirectory).status, PhononCaseStatus::Cancelled);
		EXPECT_FALSE(session.IsBatchRunning());
		EXPECT_EQ(session.ActiveCaseId(), b);
		EXPECT_EQ(session.CaseRun(a).output, next.outputDirectory);
	}
}
