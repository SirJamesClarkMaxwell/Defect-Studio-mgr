#include <gtest/gtest.h>

#include <cmath>
#include <variant>

#include "Presentation/Panels/ViewportVacancyAdd.hpp"
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
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(line.nodes[1].binding.value));
		// The vacancy end stops on the marker edge (radius 0.45 by default), not at its centre.
		const glm::vec3 end = line.transform.position + line.nodes[1].position;
		EXPECT_NEAR(end.x, 0.45f, 1e-5f);
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
	// Two vacancies that are first-shell neighbours get a line between them as well.
	TEST(ViewportVacancyAddTests, NeighbouringVacanciesAreJoined)
	{
		RendererWindowState window = WindowWithVacancy();
		RendererVacancyData second;
		second.cartesianPosition = glm::vec3(0.0f, -1.45f, 0.0f);
		window.structure.vacancies.push_back(second);
		window.selectedVacancies = {0, 1};
		const std::size_t added = AddVacancyBonds(window);
		ASSERT_NE(window.paths, nullptr);
		EXPECT_EQ(window.paths->Store().Size(), added);
		bool joined = false;
		for (const SceneObjectId id : window.paths->Store().Ids())
		{
			const ScenePath &path = *window.paths->Store().Find(id);
			const glm::vec3 a = path.transform.position + path.nodes[0].position;
			const glm::vec3 b = path.transform.position + path.nodes[1].position;
			joined |= std::abs(a.x) < 1e-4f && std::abs(b.x) < 1e-4f && a.y < 0.0f && b.y < 0.0f; // only V0-V1 lies below y = 0
		}
		EXPECT_TRUE(joined);
	}
} // namespace DefectStudio::Tests
