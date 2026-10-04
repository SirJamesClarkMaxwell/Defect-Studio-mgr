#include <gtest/gtest.h>

#include "Presentation/Panels/ViewportVacancyAdd.hpp"
#include "Renderer/RendererWindowState.hpp"

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
		ASSERT_EQ(window.sceneArrows.size(), 3u);
		const RendererWindowState::SceneArrow &line = window.sceneArrows.front();
		EXPECT_EQ(line.kind, RendererWindowState::ArrowKind::Line);
		EXPECT_EQ(line.startAnchorAtom, std::optional<std::size_t>(0));
		EXPECT_EQ(line.end(), glm::vec3(0.0f));
		EXPECT_TRUE(line.style.useGradient);
		EXPECT_EQ(line.style.gradient.start, glm::vec3(0.0f, 0.0f, 1.0f));
		EXPECT_EQ(line.style.gradient.finish, glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_EQ(window.selectedSceneArrows.size(), 3u);
	}

	TEST(ViewportVacancyAddTests, SelectedAtomsGoToTheirNearestVacancy)
	{
		RendererWindowState window = WindowWithVacancy();
		window.selectedAtomIndices = {3};
		ASSERT_EQ(AddVacancyBonds(window), 1u);
		EXPECT_EQ(window.sceneArrows.front().start(), glm::vec3(0, 0, 2.5f));
	}
} // namespace DefectStudio::Tests
