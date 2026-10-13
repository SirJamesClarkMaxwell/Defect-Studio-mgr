#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/ModeArrows.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Two atoms (masses 1 and 4), six modes:
		//   0..2 translations; 3: E, 50 meV, S 0.1; 4: A1, 200 meV, S 0.5; 5: A1, 120 meV, S 0.2
		// Mode 4: e_0 = (0.8, 0, 0), e_1 = (0, 0.6, 0) -> u_0 = (0.8, 0, 0), u_1 = (0, 0.3, 0).
		PhononAnalysisResult TwoAtoms()
		{
			PhononAnalysisResult result;
			result.symbols = {"B", "N"};
			result.positionsAngstrom = {glm::dvec3(0.0), glm::dvec3(1.0, 0.0, 0.0)};
			result.massesAmu = {1.0, 4.0};
			const double freq[6] = {0.0, 0.0, 0.0, 50.0, 200.0, 120.0};
			const double s[6] = {0.0, 0.0, 0.0, 0.1, 0.5, 0.2};
			const char *irrep[6] = {"A1", "E", "E", "E", "A1", "A1"};
			for (int k = 0; k < 6; ++k)
			{
				PhononMode mode;
				mode.index = k;
				mode.frequencyMeV = freq[k];
				mode.huangRhys = s[k];
				mode.irrep = irrep[k];
				mode.isTranslation = k < 3;
				result.modes.push_back(mode);
			}
			result.eigenvectors.assign(6 * 2 * 3, 0.0f);
			result.participation.assign(6 * 2, 0.5f);
			result.eigenvectors[(4 * 2 + 0) * 3 + 0] = 0.8f;
			result.eigenvectors[(4 * 2 + 1) * 3 + 1] = 0.6f;
			result.participation[4 * 2 + 0] = 0.64f;
			result.participation[4 * 2 + 1] = 0.36f;
			for (int k : {3, 5})
			{
				result.eigenvectors[(k * 2 + 0) * 3 + 2] = std::sqrt(0.5f);
				result.eigenvectors[(k * 2 + 1) * 3 + 2] = -std::sqrt(0.5f);
			}
			return result;
		}

		// The renderer window shows the same atoms, shifted by +10 in x (as a scene might be).
		RendererWindowState Window(const char *secondElement = "N")
		{
			RendererWindowState window;
			for (int i = 0; i < 2; ++i)
			{
				RendererAtomData atom;
				atom.element = i == 0 ? "B" : secondElement;
				atom.cartesianPosition = glm::vec3(10.0f + static_cast<float>(i), 0.0f, 0.0f);
				window.structure.atoms.push_back(atom);
			}
			return window;
		}

		void ExpectVec(const glm::vec3 &actual, const glm::vec3 &expected)
		{
			EXPECT_NEAR(actual.x, expected.x, 1e-5);
			EXPECT_NEAR(actual.y, expected.y, 1e-5);
			EXPECT_NEAR(actual.z, expected.z, 1e-5);
		}
	} // namespace

	TEST(PhononPanelSessionTests, NewResultSelectsTheStrongestCoupledMode)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		ASSERT_TRUE(session.SelectedMode().has_value());
		EXPECT_EQ(*session.SelectedMode(), 4u);
		EXPECT_EQ(session.Settings().selectedMode, 4);
	}

	TEST(PhononPanelSessionTests, DirectoryLabelsPreferTheProjectThenLabelledRoots)
	{
		const Path project = Path::FromUtf8("D:/work/project");
		const std::vector<ProjectRootEntry> roots = {{"1", Path::FromUtf8("D:/work"), "work"},
			{"2", Path::FromUtf8("K:/calculations"), "server"}};
		EXPECT_EQ(PhononPanelSession::DirectoryLabel(project / "phonons" / ".." / "bulk", project, roots), "bulk");
		EXPECT_EQ(PhononPanelSession::DirectoryLabel(project, project, roots), ".");
		EXPECT_EQ(PhononPanelSession::DirectoryLabel(Path::FromUtf8("K:/calculations/hbn/fc"), project, roots), "server/hbn/fc");
		EXPECT_EQ(PhononPanelSession::DirectoryLabel(Path::FromUtf8("D:/work/project-other/fc"), project, {}),
			Path::FromUtf8("D:/work/project-other/fc").Utf8());
		EXPECT_EQ(PhononPanelSession::DirectoryLabel(Path::FromUtf8("Z:/fc"), project, roots), Path::FromUtf8("Z:/fc").Utf8());
		EXPECT_TRUE(PhononPanelSession::DirectoryLabel({}, project, roots).empty());
	}

	TEST(PhononPanelSessionTests, TypedDirectoriesTrimQuotesAndResolveAgainstTheProject)
	{
		const Path project = Path::FromUtf8("D:/work/project");
		EXPECT_EQ(PhononPanelSession::ResolveDirectoryText("  \"phonons/../bulk\"  ", project), project / "bulk");
		EXPECT_EQ(PhononPanelSession::ResolveDirectoryText(" 'K:/fc' ", project), Path::FromUtf8("K:/fc"));
		EXPECT_TRUE(PhononPanelSession::ResolveDirectoryText(" \" \" ", project).Empty());
	}

	TEST(PhononPanelSessionTests, PlotRangesIncludeImaginaryModesAndTheVisiblePlTail)
	{
		PhononPanelSession session;
		auto result = TwoAtoms();
		result.modes[3].frequencyMeV = -4.0;
		result.plPhotonEnergyEv = {1.0, 1.5, 2.0, 2.5};
		result.plExperimental = {0.0, 0.001, 0.5, 1.0};
		result.zplEv.experimental = 2.5;
		result.plComputed = {0.0, 0.1, 1.0, 0.0};
		result.zplEv.computed = 2.0;
		session.SetResult(std::move(result));
		EXPECT_DOUBLE_EQ(session.ModeEnergyRangeMeV().first, -9.0);
		EXPECT_DOUBLE_EQ(session.ModeEnergyRangeMeV().second, 220.0);
		EXPECT_DOUBLE_EQ(session.PlEnergyRangeEv().first, 2.0);
		EXPECT_DOUBLE_EQ(session.PlEnergyRangeEv().second, 2.65);
		session.Settings().plZpl = "computed";
		EXPECT_DOUBLE_EQ(session.PlEnergyRangeEv().first, 1.5);
		EXPECT_DOUBLE_EQ(session.PlEnergyRangeEv().second, 2.15);
	}

	TEST(PhononPanelSessionTests, SameFilePathsNormalizeSeparatorsDotsAndWindowsCase)
	{
		const Path source = Path::FromUtf8("C:/a/b/POSCAR");
#ifdef DS_PLATFORM_WINDOWS
		EXPECT_TRUE(PhononPanelSession::SameFilePath(source, Path::FromUtf8("c:\\a\\b\\.\\POSCAR"), {}));
		EXPECT_TRUE(PhononPanelSession::SameFilePath(Path::FromUtf8("C:/Żółć/POSCAR"),
			Path::FromUtf8("c:/żółć/poscar"), {}));
#else
		EXPECT_FALSE(PhononPanelSession::SameFilePath(source, Path::FromUtf8("c:/a/b/POSCAR"), {}));
#endif
		EXPECT_TRUE(PhononPanelSession::SameFilePath(source, Path::FromUtf8("C:/a/b/../b/./POSCAR"), {}));
		EXPECT_FALSE(PhononPanelSession::SameFilePath(source, Path::FromUtf8("C:/a/c/POSCAR"), {}));
		EXPECT_FALSE(PhononPanelSession::SameFilePath({}, source, {}));
		EXPECT_FALSE(PhononPanelSession::SameFilePath(source, {}, {}));
		EXPECT_FALSE(PhononPanelSession::SameFilePath({}, {}, {}));
	}

	TEST(PhononPanelSessionTests, SameFilePathsResolveRelativeRestoredSourcesAgainstTheProject)
	{
		const Path project = Path::FromResolved(FileSystem::CurrentPath()) / "phonon-project";
		const Path source = project / "result" / "POSCAR";
		const Path relative = Path::FromUtf8("result/./POSCAR");
		EXPECT_TRUE(PhononPanelSession::SameFilePath(relative, source, project));
		EXPECT_TRUE(PhononPanelSession::SameFilePath(source, relative, project));
		EXPECT_FALSE(PhononPanelSession::SameFilePath(relative, source, project / "other"));
		EXPECT_TRUE(PhononPanelSession::SameFilePath(Path::FromUtf8("./POSCAR"),
			Path::FromResolved(FileSystem::CurrentPath()) / "POSCAR", {}));
	}

	TEST(PhononPanelSessionTests, GridIndexRangeIncludesOneSampleOnEachSide)
	{
		const std::vector<double> grid{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
		const auto expect = [&](double minimum, double maximum, std::size_t first, std::size_t last) {
			EXPECT_EQ(PhononPanelSession::GridIndexRange(grid, minimum, maximum), (std::pair{first, last}));
		};
		expect(2.5, 6.2, 2, 8); // samples 2..7
		expect(2.0, 6.0, 1, 8); // inclusive endpoints, plus the margin
		expect(2.2, 2.8, 2, 4); // no interior sample: keep its two neighbours
		expect(3.0, 3.0, 2, 5);
		expect(-2.0, 0.2, 0, 2);
		expect(9.8, 12.0, 9, 11);
		expect(-2.0, 12.0, 0, 11);
		expect(-2.0, -1.0, 0, 1);
		expect(11.0, 12.0, 10, 11);
		expect(6.0, 2.0, 0, 0);
	}

	TEST(PhononPanelSessionTests, GridIndexRangeHandlesEmptyAndSingleSampleGrids)
	{
		EXPECT_EQ(PhononPanelSession::GridIndexRange({}, 2.5, 6.2), (std::pair<std::size_t, std::size_t>{0, 0}));
		const std::vector<double> grid{4.0};
		EXPECT_EQ(PhononPanelSession::GridIndexRange(grid, 2.5, 6.2), (std::pair<std::size_t, std::size_t>{0, 1}));
	}

	TEST(PhononPanelSessionTests, PersistedValidSelectionSurvivesSetResult)
	{
		PhononPanelSession session;
		session.Settings().selectedMode = 5;
		session.SetResult(TwoAtoms());
		EXPECT_EQ(*session.SelectedMode(), 5u);

		session.Settings().selectedMode = 1; // a translation is not a valid selection
		session.SetResult(TwoAtoms());
		EXPECT_EQ(*session.SelectedMode(), 4u);
	}

	TEST(PhononPanelSessionTests, VisibleModesSkipTranslationsSortByFrequencyAndFilterByIrrep)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		EXPECT_EQ(session.VisibleModes(), (std::vector<std::size_t>{3, 5, 4}));
		EXPECT_EQ(session.Irreps(), (std::vector<std::string>{"A1", "E"}));
		session.Settings().irrepFilter = "A1";
		EXPECT_EQ(session.VisibleModes(), (std::vector<std::size_t>{5, 4}));
	}

	TEST(PhononPanelSessionTests, SelectModeIgnoresTranslationsAndBadIndices)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		session.SelectMode(5);
		EXPECT_EQ(*session.SelectedMode(), 5u);
		session.SelectMode(0);
		session.SelectMode(42);
		session.SelectMode(-3);
		EXPECT_EQ(*session.SelectedMode(), 5u);
	}

	TEST(PhononPanelSessionTests, ReadoutsFollowThresholdAndFraction)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		session.Settings().participationThreshold = 0.5;
		EXPECT_EQ(session.ArrowCount(), 1u);
		session.Settings().participationThreshold = 0.0;
		EXPECT_EQ(session.ArrowCount(), 2u);
		session.Settings().carriedFraction = 0.6;
		EXPECT_EQ(session.AtomsCarryingSelectedFraction(), 1u);
		session.Settings().carriedFraction = 0.9;
		EXPECT_EQ(session.AtomsCarryingSelectedFraction(), 2u);
	}

	TEST(PhononPanelSessionTests, WindowMustShowTheSameAtomsInOrder)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		std::string error;
		EXPECT_TRUE(session.WindowMatches(Window(), error)) << error;
		EXPECT_FALSE(session.WindowMatches(Window("C"), error));
		EXPECT_FALSE(error.empty());
		RendererWindowState extra = Window();
		extra.structure.atoms.push_back(extra.structure.atoms[0]);
		EXPECT_FALSE(session.WindowMatches(extra, error));
	}

	TEST(PhononPanelSessionTests, ApplyArrowsWritesTheSelectedModeIntoTheWindow)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		session.Settings().participationThreshold = 0.0;
		session.Settings().arrowLengthAngstrom = 2.0;
		session.Settings().arrowLowColor = glm::vec3(0.1f, 0.2f, 0.3f);
		RendererWindowState window = Window();
		std::string error;
		ASSERT_TRUE(session.ApplyArrows(window, error)) << error;
		ASSERT_TRUE(window.modeArrows.has_value());
		ASSERT_EQ(window.modeArrows->arrows.size(), 2u);
		EXPECT_EQ(window.modeArrows->arrows[0].atomIndex, 0u);
		ExpectVec(window.modeArrows->arrows[0].vectorAngstrom, glm::vec3(2.0f, 0.0f, 0.0f));
		ExpectVec(window.modeArrows->arrows[1].vectorAngstrom, glm::vec3(0.0f, 0.75f, 0.0f));
		ExpectVec(window.modeArrows->lowColor, glm::vec3(0.1f, 0.2f, 0.3f));

		session.Settings().showArrows = false;
		ASSERT_TRUE(session.ApplyArrows(window, error));
		EXPECT_FALSE(window.modeArrows.has_value());
	}

	TEST(PhononPanelSessionTests, ApplyArrowsToAMismatchingWindowClearsThemAndFails)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		RendererWindowState window = Window("C");
		window.modeArrows = ModeArrowsState{};
		std::string error;
		EXPECT_FALSE(session.ApplyArrows(window, error));
		EXPECT_FALSE(window.modeArrows.has_value());
		EXPECT_FALSE(error.empty());
	}

	TEST(PhononPanelSessionTests, ArrowsFollowPhaseOnAdvanceAndPausedSliderAndRestoreOnStop)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		RendererWindowState window = Window();
		std::string error;
		ASSERT_TRUE(session.ApplyArrows(window, error));
		const auto staticArrows = window.modeArrows->arrows;
		session.StartAnimation(window);
		// Atoms move with sin(phase); the arrows must match: full at pi/2, flipped at -pi/2, gone at pi.
		session.SetPhase(window, std::numbers::pi / 2.0);
		ASSERT_EQ(window.modeArrows->arrows.size(), staticArrows.size());
		for (std::size_t i = 0; i < staticArrows.size(); ++i)
			EXPECT_LE(glm::length(window.modeArrows->arrows[i].vectorAngstrom - staticArrows[i].vectorAngstrom), 1e-6f);
		session.SetPhase(window, -std::numbers::pi / 2.0);
		for (std::size_t i = 0; i < staticArrows.size(); ++i)
			EXPECT_LE(glm::length(window.modeArrows->arrows[i].vectorAngstrom + staticArrows[i].vectorAngstrom), 1e-6f);
		session.SetPhase(window, std::numbers::pi);
		for (const auto &arrow : window.modeArrows->arrows) EXPECT_LE(glm::length(arrow.vectorAngstrom), 1e-6f);
		EXPECT_TRUE(BuildModeArrowSegments(window.structure, *window.modeArrows).empty());
		session.AdvanceAnimation(window, session.Settings().animationPeriodSeconds / 4.0); // pi -> 3pi/2 = -pi/2
		for (std::size_t i = 0; i < staticArrows.size(); ++i)
			EXPECT_LE(glm::length(window.modeArrows->arrows[i].vectorAngstrom + staticArrows[i].vectorAngstrom), 1e-6f);
		session.Settings().arrowsFollowPhase = false;
		session.SetPhase(window, std::numbers::pi);
		for (std::size_t i = 0; i < staticArrows.size(); ++i)
			EXPECT_LE(glm::length(window.modeArrows->arrows[i].vectorAngstrom - staticArrows[i].vectorAngstrom), 1e-6f);
		session.Settings().arrowsFollowPhase = true;
		session.StopAnimation(window);
		for (std::size_t i = 0; i < staticArrows.size(); ++i)
			EXPECT_LE(glm::length(window.modeArrows->arrows[i].vectorAngstrom - staticArrows[i].vectorAngstrom), 1e-6f);
	}

	TEST(PhononPanelSessionTests, AnimationMovesAtomsAroundTheirWindowPositionsAndStopRestoresThem)
	{
		PhononPanelSession session;
		session.SetResult(TwoAtoms());
		session.Settings().animationAmplitudeAngstrom = 0.2;
		session.Settings().animationPeriodSeconds = 2.0;
		RendererWindowState window = Window();

		session.StartAnimation(window);
		ASSERT_TRUE(session.IsAnimating());
		// a quarter period: sin = 1, largest displacement (atom 0) swings by the full amplitude
		session.AdvanceAnimation(window, 0.5);
		EXPECT_NEAR(session.Phase(), std::numbers::pi / 2.0, 1e-9);
		ExpectVec(window.structure.atoms[0].cartesianPosition, glm::vec3(10.2f, 0.0f, 0.0f));
		ExpectVec(window.structure.atoms[1].cartesianPosition, glm::vec3(11.0f, 0.075f, 0.0f));

		session.SetPhase(window, -std::numbers::pi / 2.0);
		ExpectVec(window.structure.atoms[0].cartesianPosition, glm::vec3(9.8f, 0.0f, 0.0f));

		session.StopAnimation(window);
		EXPECT_FALSE(session.IsAnimating());
		ExpectVec(window.structure.atoms[0].cartesianPosition, glm::vec3(10.0f, 0.0f, 0.0f));
		ExpectVec(window.structure.atoms[1].cartesianPosition, glm::vec3(11.0f, 0.0f, 0.0f));
	}

	TEST(PhononPanelSessionTests, AnimationDoesNothingWithoutAValidTarget)
	{
		PhononPanelSession session;
		RendererWindowState window = Window();
		session.StartAnimation(window); // no result
		EXPECT_FALSE(session.IsAnimating());
		session.AdvanceAnimation(window, 0.5);
		ExpectVec(window.structure.atoms[0].cartesianPosition, glm::vec3(10.0f, 0.0f, 0.0f));

		session.SetResult(TwoAtoms());
		RendererWindowState wrong = Window("C");
		session.StartAnimation(wrong);
		EXPECT_FALSE(session.IsAnimating());
	}
} // namespace DefectStudio::Tests
