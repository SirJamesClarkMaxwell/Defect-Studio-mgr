#include <gtest/gtest.h>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <numbers>
#include <nlohmann/json.hpp>
#include "Core/Utils/Uuid.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	struct PhononModeVectorTestAccess
	{
		static bool Cache(PhononPanelSession &session, const Path &dataset, std::size_t mode, double frequency = 100.)
		{
			auto row = CreateRef<PhononModeVector>(); row->modeIndex = mode; row->frequencyMeV = frequency;
			row->eigenvector = {.8f, 0, 0, 0, .6f, 0, 0, 0, 0, 0, 0, 0}; row->participation = {.64f, .36f, 0, 0};
			return session.cacheModeVector(dataset, row);
		}
		static std::size_t CacheSize(const PhononPanelSession &session) { return session.m_ModeVectorCache.size(); }
		static void Remember(PhononPanelSession &session) { session.cacheActiveResult(); }
		static Path Supercell(const PhononPanelSession &session) { return session.supercellPath(); }
		static JobId Pending(const PhononPanelSession &session) { return session.m_ModeVectorJobId; }
		static std::string AutoAnimate(const PhononPanelSession &session) { return session.m_ModeVectorAnimateWindow; }
	};

	class PhononModeVectorSessionTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::current_path() / ".task-113g-tests" / ToString(GenerateUuid()));
		void SetUp() override
		{
			std::filesystem::create_directories((root / "dataset").Native());
			std::filesystem::create_directories((root / "server/embedded/12x12x1").Native());
			std::ofstream((root / "server/embedded/12x12x1/POSCAR").Native()) << "fixture";
			std::ofstream((root / "server/embedded/12x12x1/modes.h5").Native()) << "not read by queue tests";
			std::ofstream((root / "dataset/summary.json").Native()) << nlohmann::json{
				{"server", {{"hr_dir", (root / "server/spectral-function/12x12x1/displacements").Utf8()}}}}.dump();
		}
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		PhononAnalysisResult Result(const Path &directory)
		{
			PhononAnalysisResult result; result.directory = directory; result.source = "server"; result.hasEigenvectors = false;
			result.symbols = {"B", "N", "B", "N"}; result.massesAmu = {1., 4., 1., 4.};
			result.positionsAngstrom = {glm::dvec3(0.), glm::dvec3(1.), glm::dvec3(2.), glm::dvec3(3.)};
			result.modes.resize(12);
			for (std::size_t i = 0; i < result.modes.size(); ++i)
			{
				result.modes[i].index = static_cast<int>(i); result.modes[i].frequencyMeV = 100.;
				result.modes[i].isTranslation = i < 3;
			}
			return result;
		}
	};

	TEST_F(PhononModeVectorSessionTests, EvictsLeastRecentlyUsedAcrossDatasetsIncludingPlotCache)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root); session.Settings().showArrows = false;
		const auto a = root / "dataset", b = root / "other";
		session.Settings().resultDirectory = a; session.SetResult(Result(a));
		for (std::size_t mode = 3; mode < 11; ++mode) ASSERT_TRUE(PhononModeVectorTestAccess::Cache(session, a, mode));
		session.SelectMode(3); // Touch the oldest entry; mode 4 must be evicted next.
		PhononModeVectorTestAccess::Remember(session);
		session.SetResult(Result(b));
		ASSERT_TRUE(PhononModeVectorTestAccess::Cache(session, b, 3));
		EXPECT_EQ(PhononModeVectorTestAccess::CacheSize(session), 8u);
		EXPECT_FALSE(PhononModeVectorTestAccess::Cache(session, a, 3));
		EXPECT_FALSE(PhononModeVectorTestAccess::Cache(session, b, 99));
		EXPECT_FALSE(PhononModeVectorTestAccess::Cache(session, b, 4, 200.));
		session.SetResult(Result(a));
		EXPECT_FALSE(ModeRows(*session.Result(), 3).eigenvector.empty());
		EXPECT_TRUE(ModeRows(*session.Result(), 4).eigenvector.empty());
		EXPECT_TRUE(session.Result()->eigenvectors.empty());
		EXPECT_TRUE(session.Result()->participation.empty());
		std::string error;
		const auto plotted = session.PlotModes(a, error);
		ASSERT_TRUE(plotted); EXPECT_TRUE(ModeRows(plotted->get(), 4).eigenvector.empty());
		session.SetPersistedState({}, root);
		EXPECT_EQ(PhononModeVectorTestAccess::CacheSize(session), 0u);
	}

	TEST_F(PhononModeVectorSessionTests, RefreshingSummaryInvalidatesCachedVectors)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root);
		const auto dataset = root / "dataset";
		session.SetResult(Result(dataset));
		ASSERT_TRUE(PhononModeVectorTestAccess::Cache(session, dataset, 3));
		const auto summary = (dataset / "summary.json").Native();
		std::filesystem::last_write_time(summary, std::filesystem::last_write_time(summary) + std::chrono::seconds(1));
		session.SetResult(Result(dataset));
		EXPECT_TRUE(ModeRows(*session.Result(), 3).eigenvector.empty());
		EXPECT_EQ(PhononModeVectorTestAccess::CacheSize(session), 0u);
	}

	TEST_F(PhononModeVectorSessionTests, OpensEmbeddedStructureAndAnimatesSparseRowThenRestoresPositions)
	{
		PhononPanelSession session;
		session.SetPersistedState({}, root); session.SetResult(Result(root / "dataset"));
		session.SelectMode(3);
		EXPECT_EQ(PhononModeVectorTestAccess::Supercell(session), root / "server/embedded/12x12x1/POSCAR");
		std::vector<RendererWindowState> windows(1);
		auto &window = windows.front(); window.windowId = "server";
		window.structure.sourcePath = PhononModeVectorTestAccess::Supercell(session);
		for (const auto &symbol : {"B", "N", "B", "N"})
		{
			RendererAtomData atom; atom.element = symbol; atom.cartesianPosition = glm::vec3(3, 4, 5);
			window.structure.atoms.push_back(atom);
		}
		session.Update(windows, 0, true);
		EXPECT_EQ(session.WindowId(), "server");
		ASSERT_TRUE(PhononModeVectorTestAccess::Cache(session, root / "dataset", 3));
		EXPECT_FALSE(session.Field().real.empty());
		session.Settings().showArrows = true;
		std::string error;
		ASSERT_TRUE(session.ApplyArrows(window, error)); ASSERT_TRUE(window.modeArrows);
		const auto rest = window.structure.atoms[0].cartesianPosition;
		session.StartAnimation(window); ASSERT_TRUE(session.IsAnimating());
		session.SetPhase(window, std::numbers::pi / 2);
		EXPECT_GT(window.structure.atoms[0].cartesianPosition.x, rest.x);
		session.StopAnimation(); EXPECT_EQ(window.structure.atoms[0].cartesianPosition, rest);
		std::filesystem::remove((root / "server/embedded/12x12x1/modes.h5").Native());
		session.SelectMode(4);
		EXPECT_NE(session.ModeVectorBlocker().find("Brak modes.h5"), std::string::npos);
		EXPECT_TRUE(session.Field().real.empty());
	}

	TEST_F(PhononModeVectorSessionTests, SelectionAndProjectReplacementCancelPendingRequests)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		const auto gate = CreateRef<Gate>();
		struct Release { Ref<Gate> gate; ~Release() { gate->Open(); } } release{gate};
		const auto blocking = jobs->Submit(CreateRef<BlockingJob>(gate));
		ASSERT_TRUE(WaitUntil([&] { return jobs->GetJob(blocking)->status == JobStatus::Running; }, Time::Milliseconds(5000)));
		PhononPanelSession session(jobs);
		session.SetPersistedState({}, root); session.Settings().showArrows = false;
		session.SetResult(Result(root / "dataset")); session.SelectMode(3); session.SubmitModeVector();
		const auto first = PhononModeVectorTestAccess::Pending(session); ASSERT_NE(first, 0u);
		session.SelectMode(4); EXPECT_TRUE(jobs->GetJob(first)->cancellationRequested);
		std::vector<RendererWindowState> windows(1);
		auto &window = windows.front(); window.windowId = "server";
		window.structure.sourcePath = PhononModeVectorTestAccess::Supercell(session);
		for (const auto &symbol : {"B", "N", "B", "N"})
		{
			RendererAtomData atom; atom.element = symbol; window.structure.atoms.push_back(atom);
		}
		session.Update(windows, 0, true);
		session.StartAnimation(window);
		const auto second = PhononModeVectorTestAccess::Pending(session); ASSERT_NE(second, 0u);
		EXPECT_EQ(PhononModeVectorTestAccess::AutoAnimate(session), "server");
		session.StopAnimation(); EXPECT_TRUE(PhononModeVectorTestAccess::AutoAnimate(session).empty());
		session.SetPersistedState({}, root);
		EXPECT_TRUE(jobs->GetJob(second)->cancellationRequested);
		gate->Open();
		ASSERT_TRUE(WaitUntil([&] { return jobs->GetJob(second)->status == JobStatus::Cancelled; }, Time::Milliseconds(5000)));
		session.Update(windows, 0, false);
		EXPECT_EQ(PhononModeVectorTestAccess::Pending(session), 0u); EXPECT_FALSE(session.Result());
	}
}
