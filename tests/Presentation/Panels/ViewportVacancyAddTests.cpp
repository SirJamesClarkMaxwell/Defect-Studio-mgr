#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <variant>

#include "Presentation/Panels/ViewportVacancyAdd.hpp"
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
		EXPECT_FLOAT_EQ(binding.buffer, 0.9f);
		ASSERT_TRUE(std::holds_alternative<PathBinding::CopyVacancy>(line.nodes[1].binding.value));
		const auto &vacancyBinding = std::get<PathBinding::CopyVacancy>(line.nodes[1].binding.value);
		EXPECT_EQ(vacancyBinding.vacancyIndex, 0u);
		EXPECT_FLOAT_EQ(vacancyBinding.buffer, 0.0f);
		// The marker depth hides the centre-bound vacancy end.
		const glm::vec3 end = ResolveNodePositions(line, SceneSystem::MakePathBindingContext(window)).positions[1];
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
		// The atom end stays the same buffered distance inside the atom; only its direction turns.
		const glm::vec3 atom = window.structure.atoms[0].cartesianPosition;
		EXPECT_NEAR(glm::distance(after.positions[0], atom), glm::distance(before.positions[0], atom), 1e-5f);
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
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(line.nodes.front().binding.value).buffer, 0.9f);
			EXPECT_FLOAT_EQ(std::get<PathBinding::CopyVacancy>(line.nodes.back().binding.value).buffer, 0.0f);
		}
	}
} // namespace DefectStudio::Tests
