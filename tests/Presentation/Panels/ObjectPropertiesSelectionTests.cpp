#include <gtest/gtest.h>

#include "Presentation/Panels/ObjectPropertiesSelection.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportVacancySelection.hpp"

namespace DefectStudio::Tests
{
	TEST(ObjectPropertiesSelectionTests, NoSupportedSelectionNeedsTheEmptyMessage)
	{
		const RendererWindowState window;

		const ObjectPropertiesSections sections = ResolveObjectPropertiesSections(window);

		EXPECT_TRUE(sections.Empty());
	}

	TEST(ObjectPropertiesSelectionTests, EverySelectedKindGetsItsOwnSection)
	{
		RendererWindowState window;
		window.selectedAtomIndices = {1, 2};
		window.selectedPinnedMeasurements = {SceneObjectId{11}};
		window.selectedFreeLabels = {SceneObjectId{12}};
		window.selectedSceneArrows = {SceneObjectId{13}};
		window.selectedSceneOrbitals = {SceneObjectId{14}};
		window.selectedScenePlanes = {SceneObjectId{15}};

		const ObjectPropertiesSections sections = ResolveObjectPropertiesSections(window);

		EXPECT_TRUE(sections.atoms);
		EXPECT_TRUE(sections.labels);
		EXPECT_TRUE(sections.arrows);
		EXPECT_TRUE(sections.orbitals);
		EXPECT_TRUE(sections.planes);
		EXPECT_FALSE(sections.Empty());
	}

	TEST(ObjectPropertiesSelectionTests, LabelsCombinePinnedAndFreeSelections)
	{
		RendererWindowState pinnedWindow;
		pinnedWindow.selectedPinnedMeasurements = {SceneObjectId{21}};
		RendererWindowState freeWindow;
		freeWindow.selectedFreeLabels = {SceneObjectId{22}};

		EXPECT_TRUE(ResolveObjectPropertiesSections(pinnedWindow).labels);
		EXPECT_TRUE(ResolveObjectPropertiesSections(freeWindow).labels);
	}

	TEST(ObjectPropertiesSelectionTests, SelectedVacancyGetsItsSection)
	{
		RendererWindowState window;
		window.selectedVacancies = {0};

		const ObjectPropertiesSections sections = ResolveObjectPropertiesSections(window);

		EXPECT_TRUE(sections.vacancies);
		EXPECT_FALSE(sections.Empty());
	}

	// A ray down -z through two markers picks the nearer one; a hidden group picks nothing.
	TEST(ObjectPropertiesSelectionTests, VacancyPickTakesTheNearestShownMarker)
	{
		RendererWindowState window;
		window.structure.vacancies = {{glm::vec3(0.0f, 0.0f, -5.0f)}, {glm::vec3(0.0f, 0.1f, -2.0f)},
			{glm::vec3(3.0f, 0.0f, -1.0f)}};
		const glm::vec3 origin(0.0f);
		const glm::vec3 down(0.0f, 0.0f, -1.0f);

		EXPECT_EQ(PickVacancyAlongRay(window, origin, down), std::optional<std::size_t>(1));
		EXPECT_FALSE(PickVacancyAlongRay(window, origin, glm::vec3(0.0f, 0.0f, 1.0f)).has_value());
		window.showVacancies = false;
		EXPECT_FALSE(PickVacancyAlongRay(window, origin, down).has_value());
	}

	// An atom on the ray beats the orbital/plane hit volumes around it (the "All" mode bug).
	TEST(ObjectPropertiesSelectionTests, AtomPickAlongRayFindsNearestVisibleAtom)
	{
		RendererWindowState window;
		RendererAtomData far;
		far.cartesianPosition = glm::vec3(0.0f, 0.0f, -6.0f);
		RendererAtomData nearHidden;
		nearHidden.cartesianPosition = glm::vec3(0.0f, 0.0f, -2.0f);
		nearHidden.visible = false;
		window.structure.atoms = {far, nearHidden};

		EXPECT_EQ(PickAtomAlongRay(window, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -3.0f)), std::optional<std::size_t>(0));
		EXPECT_FALSE(PickAtomAlongRay(window, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)).has_value());
	}
} // namespace DefectStudio::Tests
