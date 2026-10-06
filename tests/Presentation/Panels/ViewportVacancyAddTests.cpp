#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <variant>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Presentation/Panels/ViewportVacancyAdd.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"
#include "Renderer/Path/VacancyBond.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// A vacancy at the origin with three atoms at 1.45 A and one at 2.5 A.
		[[nodiscard]] RendererWindowState WindowWithVacancy()
		{
			RendererWindowState window;
			for (const glm::vec3 position : {glm::vec3(1.45f, 0, 0), glm::vec3(0, 1.45f, 0), glm::vec3(-1.45f, 0, 0),
					 glm::vec3(0, 0, 2.5f)})
			{
				RendererAtomData atom;
				atom.cartesianPosition = position;
				atom.color = glm::vec3(0.0f, 0.0f, 1.0f);
				window.structure.atoms.push_back(atom);
			}
			RendererVacancyData vacancy;
			vacancy.color = glm::vec3(1.0f, 0.0f, 0.0f);
			window.structure.vacancies = {vacancy};
			return window;
		}
	} // namespace

	TEST(ViewportVacancyAddTests, NeighbourShellIsTheFirstShell)
	{
		const RendererWindowState window = WindowWithVacancy();
		EXPECT_EQ(NeighbourShell(window, glm::vec3(0.0f)), (std::vector<std::size_t>{0, 1, 2}));
	}

	// Selected vacancy, no atoms: one atom -> vacancy line per neighbour, coloured atom -> vacancy.
	TEST(ViewportVacancyAddTests, SelectedVacancyGetsALineToEachNeighbour)
	{
		RendererWindowState window = WindowWithVacancy();
		window.selectedVacancies = {0};
		ASSERT_EQ(AddVacancyBonds(window), 3u);
		ASSERT_NE(window.paths, nullptr);
		ASSERT_EQ(window.paths->Store().Size(), 3u);
		ASSERT_EQ(window.selectedScenePaths.size(), 3u);
		EXPECT_EQ(window.selectedScenePaths, window.paths->Store().Ids());
		const ScenePath &line = *window.paths->Store().Find(window.selectedScenePaths.front());
		ASSERT_EQ(line.segments.size(), 1u);
		EXPECT_TRUE(std::holds_alternative<LineSegmentData>(line.segments[0].data));
		ASSERT_EQ(line.nodes.size(), 2u);
		ASSERT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(line.nodes[0].binding.value));
		const auto &binding = std::get<PathBinding::CopyPosition>(line.nodes[0].binding.value);
		EXPECT_EQ(binding.atomIndex, 0u);
		EXPECT_FLOAT_EQ(binding.buffer, 0.0f);
		ASSERT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(line.nodes[1].binding.value));
		const auto &vacancyBinding = std::get<PathBinding::CopyVacancy>(line.nodes[1].binding.value);
		EXPECT_EQ(vacancyBinding.vacancyIndex, 0u);
		EXPECT_FLOAT_EQ(vacancyBinding.buffer, 0.0f);
		EXPECT_EQ(line.style.depthMode, PathDepthMode::DepthTest);
		// Both endpoints stay at the sphere centres, even for a tube too thick for the old 0.9 buffer.
		window.structure.atoms[0].radius = 0.15f;
		const auto resolved = ResolveNodePositions(line, SceneSystem::MakePathBindingContext(window));
		EXPECT_EQ(resolved.positions.front(), window.structure.atoms[0].cartesianPosition);
		const glm::vec3 end = resolved.positions[1];
		EXPECT_NEAR(end.x, 0.0f, 1e-5f);
		EXPECT_NEAR(end.y, 0.0f, 1e-5f);
		EXPECT_EQ(line.style.profile, StrokeProfile::Round);
		EXPECT_FLOAT_EQ(line.style.width, 0.18f);
		EXPECT_EQ(line.style.startDecoration.kind, PathDecorationKind::None);
		EXPECT_EQ(line.style.endDecoration.kind, PathDecorationKind::None);
		EXPECT_TRUE(line.style.gradient.enabled);
		ASSERT_EQ(line.style.gradient.stops.size(), 2u);
		EXPECT_EQ(line.style.gradient.stops[0].color, glm::vec3(0.0f, 0.0f, 1.0f));
		EXPECT_EQ(line.style.gradient.stops[1].color, glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_FLOAT_EQ(line.style.gradient.stops[0].position, 0.0f);
		EXPECT_FLOAT_EQ(line.style.gradient.stops[1].position, 1.0f);
	}

	TEST(ViewportVacancyAddTests, SelectedAtomsGoToTheirNearestVacancy)
	{
		RendererWindowState window = WindowWithVacancy();
		window.selectedAtomIndices = {3};
		ASSERT_EQ(AddVacancyBonds(window), 1u);
		ASSERT_NE(window.paths, nullptr);
		const ScenePath &line = *window.paths->Store().Find(window.selectedScenePaths.front());
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(line.nodes[0].binding.value).atomIndex, 3u);
		EXPECT_EQ(line.transform.position + line.nodes[0].position, glm::vec3(0, 0, 2.5f));
	}
	// A vacancy and a first-shell neighbouring vacancy get a line, even when only one is selected.
	TEST(ViewportVacancyAddTests, NeighbouringVacanciesAreJoined)
	{
		RendererWindowState window = WindowWithVacancy();
		RendererVacancyData second;
		second.cartesianPosition = glm::vec3(0.0f, -1.45f, 0.0f);
		window.structure.vacancies.push_back(second);
		window.selectedVacancies = {0};
		const std::size_t added = AddVacancyBonds(window);
		ASSERT_NE(window.paths, nullptr);
		EXPECT_EQ(window.paths->Store().Size(), added);
		bool joined = false;
		for (const SceneObjectId id : window.paths->Store().Ids())
		{
			const ScenePath &path = *window.paths->Store().Find(id);
			if (!std::holds_alternative<PathBinding::CopyVacancy>(path.nodes[0].binding.value))
				continue;
			joined = true;
			ASSERT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(path.nodes[1].binding.value));
			const auto &first = std::get<PathBinding::CopyVacancy>(path.nodes[0].binding.value);
			const auto &last = std::get<PathBinding::CopyVacancy>(path.nodes[1].binding.value);
			EXPECT_EQ(first.vacancyIndex, 0u);
			EXPECT_EQ(last.vacancyIndex, 1u);
			EXPECT_FLOAT_EQ(first.buffer, 0.0f);
			EXPECT_FLOAT_EQ(last.buffer, 0.0f);
			const auto resolved = ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window));
			EXPECT_NEAR(resolved.positions[0].y, 0.0f, 1e-5f);
			EXPECT_NEAR(resolved.positions[1].y, -1.45f, 1e-5f);
		}
		EXPECT_TRUE(joined);
	}

	TEST(ViewportVacancyAddTests, MovingVacancyUpdatesCreatedBondAndCacheRevision)
	{
		RendererWindowState window = WindowWithVacancy();
		window.selectedAtomIndices = {0};
		ASSERT_EQ(AddVacancyBonds(window), 1u);
		const auto &line = *window.paths->Store().Find(window.selectedScenePaths.front());
		const auto context = SceneSystem::MakePathBindingContext(window);
		const auto before = ResolveNodePositions(line, context);
		window.structure.vacancies[0].cartesianPosition = glm::vec3(0, 1, 0);
		const auto after = ResolveNodePositions(line, context);
		// The atom end remains at the centre when the bond turns.
		const glm::vec3 atom = window.structure.atoms[0].cartesianPosition;
		EXPECT_EQ(before.positions[0], atom);
		EXPECT_EQ(after.positions[0], atom);
		EXPECT_NE(after.positions[1], before.positions[1]);
		EXPECT_NEAR(glm::length(after.positions[1] - window.structure.vacancies[0].cartesianPosition), 0.0f, 1e-5f);
		EXPECT_NE(BindingSourceRevision(line, before), BindingSourceRevision(line, after));
	}
	TEST(ViewportVacancyAddTests, WidthUsesEffectiveBondRadiusIncludingGlobalMultiplier)
	{
		for (float radius : {0.0f, 0.07f})
		{
			auto window = WindowWithVacancy();
			window.structure.bonds.emplace_back();
			window.structure.bonds.front().radius = radius;
			window.selectedAtomIndices = {0};
			ASSERT_EQ(AddVacancyBonds(window, 2.5f), 1u);
			const auto &line = *window.paths->Store().Find(window.selectedScenePaths.front());
			EXPECT_FLOAT_EQ(line.style.width, 2.0f * std::max(radius, 0.001f) * 2.5f);
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(line.nodes.front().binding.value).buffer, 0.0f);
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyVacancy>(line.nodes.back().binding.value).buffer, 0.0f);
		}
	}

	TEST(ViewportVacancyAddTests, NoSelectionGeneratesNeighboursForEveryVacancy)
	{
		auto window = WindowWithVacancy();
		RendererVacancyData second;
		second.cartesianPosition = glm::vec3(0, -1.45f, 0);
		window.structure.vacancies.push_back(second);
		ASSERT_GT(AddVacancyBonds(window), 0u);
		bool secondHasAtomBond = false;
		window.paths->Store().Visit([&](const ScenePath &path) {
			const auto pair = GeneratedVacancyBondPair(path);
			if (pair && !pair->vacancyPair && pair->second == 1u)
				secondHasAtomBond = true;
		});
		EXPECT_TRUE(secondHasAtomBond);
	}

	TEST(ViewportVacancyAddTests, GeneratedTubeHasNoTerminalCapAndMatchesBondColourRamp)
	{
		auto window = WindowWithVacancy();
		window.selectedAtomIndices = {0};
		ASSERT_EQ(AddVacancyBonds(window), 1u);
		const ScenePath &path = *window.paths->Store().Find(window.selectedScenePaths.front());
		const auto evaluated = Tessellate(path, ResolveNodePositions(path, SceneSystem::MakePathBindingContext(window)), {});
		const auto open = BuildStroke(evaluated, path.style, !GeneratedVacancyBondPair(path).has_value());
		const auto capped = BuildStroke(evaluated, path.style);
		ASSERT_FALSE(open.indices.empty());
		EXPECT_LT(open.indices.size(), capped.indices.size());
		for (const auto &vertex : open.tubeVertices)
			EXPECT_NEAR(glm::dot(glm::dvec3(vertex.normal), evaluated.samples.front().tangent), 0.0f, 1e-5f);
		const glm::vec3 mixed = (window.structure.atoms[0].color + window.structure.vacancies[0].color) * 0.5f;
		EXPECT_EQ(glm::vec3(SampleStrokeColor(path.style, 0.5)), mixed);
	}

	class VacancyBondRegenerationTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undoStack);
			auto window = WindowWithVacancy();
			window.windowId = "vacancy-bond-regeneration";
			renderer.AddWindow(std::move(window));
		}
		void TearDown() override { renderer.OnDetach(); }
		RendererWindowState &Window() { return renderer.GetWindows().front(); }
		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{[] { RendererStartupConfig config; config.loadDefaultScene = false; return config; }()};
	};

	TEST_F(VacancyBondRegenerationTests, RepairsRenamedLegacyBondsAndDuplicatesWithOneUndo)
	{
		auto &window = Window();
		window.selectedVacancies = {0};
		ASSERT_EQ(AddVacancyBonds(window), 3u);
		const auto originalIds = window.selectedScenePaths;
		const SceneObjectId retained = originalIds.front();
		ASSERT_TRUE(window.paths->Store().MutateGeometry(retained, [](ScenePath &path) {
			path.name = "My renamed bond";
			path.persistKey = "keep-this-key";
			path.style.width = 0.03f;
			std::get<PathBinding::CopyPosition>(path.nodes.front().binding.value).buffer = 0.9f;
			std::get<PathBinding::CopyVacancy>(path.nodes.back().binding.value).buffer = 1.0f;
		}));
		ScenePath duplicate = *window.paths->Store().Find(retained);
		std::swap(duplicate.nodes.front(), duplicate.nodes.back());
		duplicate.id = {};
		duplicate.persistKey.clear();
		const auto added = AddScenePath(MakeSilentPathEditContext(window), std::move(duplicate));
		ASSERT_TRUE(added);
		const SceneObjectId duplicateId = added.Value();
		undoStack->Clear();

		window.selectedVacancies = {0};
		ASSERT_EQ(AddVacancyBonds(window, 2.0f), 3u);
		EXPECT_EQ(window.paths->Store().Size(), 3u);
		EXPECT_EQ(window.selectedScenePaths, originalIds);
		EXPECT_FALSE(window.paths->Store().Contains(duplicateId));
		const auto &fixed = *window.paths->Store().Find(retained);
		EXPECT_EQ(fixed.name, "My renamed bond");
		EXPECT_EQ(fixed.persistKey, "keep-this-key");
		EXPECT_FLOAT_EQ(fixed.style.width, 0.36f);
		EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(fixed.nodes.front().binding.value).buffer, 0.0f);
		EXPECT_FLOAT_EQ(std::get<PathBinding::CopyVacancy>(fixed.nodes.back().binding.value).buffer, 0.0f);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);

		ASSERT_TRUE(undoStack->Undo());
		EXPECT_EQ(window.paths->Store().Size(), 4u);
		EXPECT_TRUE(window.paths->Store().Contains(duplicateId));
		const auto &old = *window.paths->Store().Find(retained);
		EXPECT_FLOAT_EQ(old.style.width, 0.03f);
		EXPECT_FLOAT_EQ(std::get<PathBinding::CopyVacancy>(old.nodes.back().binding.value).buffer, 1.0f);
		ASSERT_TRUE(undoStack->Redo());
		EXPECT_EQ(window.paths->Store().Size(), 3u);
		window.selectedVacancies = {0};
		ASSERT_EQ(AddVacancyBonds(window), 3u);
		EXPECT_EQ(window.paths->Store().Size(), 3u);
		EXPECT_EQ(undoStack->GetUndoDepth(), 2u);
	}

	TEST_F(VacancyBondRegenerationTests, VacancyPairIsRecognisedInEitherDirectionAndUndoRestoresDuplicates)
	{
		auto &window = Window();
		RendererVacancyData second;
		second.cartesianPosition = glm::vec3(0, -1.45f, 0);
		window.structure.vacancies.push_back(second);
		window.selectedVacancies = {0};
		ASSERT_EQ(AddVacancyBonds(window), 4u);
		const SceneObjectId retained = window.selectedScenePaths.back();
		ASSERT_TRUE(window.paths->Store().MutateGeometry(retained, [](ScenePath &path) {
			std::swap(path.nodes.front(), path.nodes.back());
			std::get<PathBinding::CopyVacancy>(path.nodes.front().binding.value).buffer = 1.0f;
			std::get<PathBinding::CopyVacancy>(path.nodes.back().binding.value).buffer = 1.0f;
			path.name = "Renamed vacancy pair";
		}));
		ScenePath duplicate = *window.paths->Store().Find(retained);
		duplicate.id = {};
		ASSERT_TRUE(AddScenePath(MakeSilentPathEditContext(window), std::move(duplicate)));
		undoStack->Clear();
		window.selectedVacancies = {0};
		ASSERT_EQ(AddVacancyBonds(window), 4u);
		EXPECT_EQ(window.paths->Store().Size(), 4u);
		const auto &pair = *window.paths->Store().Find(retained);
		EXPECT_EQ(pair.name, "Renamed vacancy pair");
		for (const auto &node : pair.nodes)
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyVacancy>(node.binding.value).buffer, 0.0f);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
		ASSERT_TRUE(undoStack->Undo());
		EXPECT_EQ(window.paths->Store().Size(), 5u);
		ASSERT_TRUE(undoStack->Redo());
		EXPECT_EQ(window.paths->Store().Size(), 4u);
	}

	TEST_F(VacancyBondRegenerationTests, LeavesUserPathsAndOtherPairsAlone)
	{
		auto &window = Window();
		window.selectedAtomIndices = {0};
		ASSERT_EQ(AddVacancyBonds(window), 1u);
		const ScenePath generated = *window.paths->Store().Find(window.selectedScenePaths.front());
		std::vector<SceneObjectId> userIds;
		for (int variant = 0; variant < 8; ++variant)
		{
			ScenePath user = generated;
			user.id = {};
			// A misleading name must not turn a user path into a generated bond.
			user.name = "Vacancy bond";
			switch (variant)
			{
			case 0: user.style.gradient.enabled = false; break;
			case 1: user.style.dash.enabled = true; break;
			case 2: user.style.endDecoration.kind = PathDecorationKind::Arrow; break;
			case 3: user.style.depthMode = PathDepthMode::AlwaysOnTop; break;
			case 4: user.style.alpha = 0.5f; break;
			case 5: user.style.cap = PathLineCap::Round; break;
			case 6: std::get<PathBinding::CopyVacancy>(user.nodes.back().binding.value).buffer = 1.15f; break;
			case 7: std::get<PathBinding::CopyPosition>(user.nodes.front().binding.value).offset.x = 0.2f; break;
			}
			ASSERT_FALSE(GeneratedVacancyBondPair(user));
			const auto added = AddScenePath(MakeSilentPathEditContext(window), std::move(user));
			ASSERT_TRUE(added);
			userIds.push_back(added.Value());
		}
		ScenePath other = generated;
		other.id = {};
		std::get<PathBinding::CopyPosition>(other.nodes.front().binding.value).atomIndex = 3;
		const auto otherAdded = AddScenePath(MakeSilentPathEditContext(window), std::move(other));
		ASSERT_TRUE(otherAdded);
		undoStack->Clear();
		window.selectedAtomIndices = {0};
		ASSERT_EQ(AddVacancyBonds(window, 2.0f), 1u);
		EXPECT_EQ(window.paths->Store().Size(), 10u);
		for (const auto id : userIds)
		{
			ASSERT_TRUE(window.paths->Store().Contains(id));
			EXPECT_FALSE(GeneratedVacancyBondPair(*window.paths->Store().Find(id)));
			EXPECT_FLOAT_EQ(window.paths->Store().Find(id)->style.width, generated.style.width);
		}
		EXPECT_FLOAT_EQ(window.paths->Store().Find(otherAdded.Value())->style.width, generated.style.width);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(VacancyBondRegenerationTests, FailedReplacementKeepsOldBondsAndCreatesNoUndoEntry)
	{
		auto &window = Window();
		window.selectedVacancies = {0};
		ASSERT_EQ(AddVacancyBonds(window), 3u);
		const auto original = CaptureSceneObjectsSnapshot(window);
		undoStack->Clear();
		window.selectedVacancies = {0};
		EXPECT_EQ(AddVacancyBonds(window, 0.0f), 0u); // invalid zero width
		EXPECT_EQ(window.paths->Store().Ids(), original.paths.Ids());
		window.paths->Store().Visit([&](const ScenePath &path) {
			EXPECT_EQ(path.style.width, original.paths.Find(path.id)->style.width);
		});
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}
} // namespace DefectStudio::Tests
