#include <gtest/gtest.h>

#include "IO/SceneObjectsIO.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState WindowWithAtomAndVacancies()
		{
			RendererWindowState window;
			RendererAtomData atom;
			atom.element = "C";
			atom.cartesianPosition = glm::vec3(1.0f, 0.0f, 0.0f);
			window.structure.atoms = {atom};
			RendererVacancyData boron;
			boron.cartesianPosition = glm::vec3(0.0f, 2.0f, 0.0f);
			boron.label = "V_B";
			RendererVacancyData nitrogen;
			nitrogen.cartesianPosition = glm::vec3(0.0f, 4.0f, 0.0f);
			nitrogen.label = "V_N";
			window.structure.vacancies = {boron, nitrogen};
			return window;
		}

		[[nodiscard]] RendererWindowState::FreeLabel &AddLabel(RendererWindowState &window)
		{
			RendererWindowState::FreeLabel label;
			label.id = window.sceneRegistry.AllocateObjectId();
			label.text = "C_1";
			window.freeLabels.push_back(label);
			return window.freeLabels.back();
		}
	} // namespace

	TEST(SceneFreeLabelAnchorsTests, AnchoredLabelFollowsItsAtomOrVacancy)
	{
		RendererWindowState window = WindowWithAtomAndVacancies();
		{
			RendererWindowState::FreeLabel &label = AddLabel(window);
			label.anchorAtom = 0;
			label.anchorOffset = glm::vec3(0.0f, 0.0f, 0.5f);
		}
		{
			RendererWindowState::FreeLabel &label = AddLabel(window);
			label.anchorVacancy = 1;
		}
		window.structure.atoms[0].cartesianPosition = glm::vec3(3.0f, 0.0f, 0.0f);
		RefreshAnchoredFreeLabels(window);
		EXPECT_EQ(window.freeLabels[0].worldPosition, glm::vec3(3.0f, 0.0f, 0.5f));
		EXPECT_EQ(window.freeLabels[1].worldPosition, glm::vec3(0.0f, 4.0f, 0.0f));
		EXPECT_EQ(ResolveFreeLabelAnchor(window, window.freeLabels[1]), std::optional<glm::vec3>(glm::vec3(0, 4, 0)));
	}

	TEST(SceneFreeLabelAnchorsTests, FreeOrStaleLabelStaysPut)
	{
		RendererWindowState window = WindowWithAtomAndVacancies();
		AddLabel(window).worldPosition = glm::vec3(7.0f);
		RendererWindowState::FreeLabel &stale = AddLabel(window);
		stale.anchorVacancy = 5;
		stale.worldPosition = glm::vec3(8.0f);
		RefreshAnchoredFreeLabels(window);
		EXPECT_EQ(window.freeLabels[0].worldPosition, glm::vec3(7.0f));
		EXPECT_EQ(window.freeLabels[1].worldPosition, glm::vec3(8.0f));
		EXPECT_FALSE(ResolveFreeLabelAnchor(window, window.freeLabels[0]).has_value());
		EXPECT_FALSE(ResolveFreeLabelAnchor(window, window.freeLabels[1]).has_value());
	}

	// G on an anchored label moves its offset; the anchor stays and the label keeps following.
	TEST(SceneFreeLabelAnchorsTests, TranslatingAnAnchoredLabelMovesItsOffset)
	{
		RendererWindowState window = WindowWithAtomAndVacancies();
		RendererWindowState::FreeLabel &label = AddLabel(window);
		label.anchorAtom = 0;
		RefreshAnchoredFreeLabels(window);
		window.selectedFreeLabels = {label.id};

		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(0.0f, 1.0f, 0.0f);
		ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, glm::vec3(1.0f, 0.0f, 0.0f));
		RefreshAnchoredFreeLabels(window);
		EXPECT_EQ(window.freeLabels[0].anchorAtom, std::optional<std::size_t>(0));
		EXPECT_EQ(window.freeLabels[0].anchorOffset, glm::vec3(0.0f, 1.0f, 0.0f));
		EXPECT_EQ(window.freeLabels[0].worldPosition, glm::vec3(1.0f, 1.0f, 0.0f));

		RestoreSceneTransformSelection(window, snapshot);
		RefreshAnchoredFreeLabels(window);
		EXPECT_EQ(window.freeLabels[0].anchorOffset, glm::vec3(0.0f));
		EXPECT_EQ(window.freeLabels[0].worldPosition, glm::vec3(1.0f, 0.0f, 0.0f));
	}

	TEST(SceneFreeLabelAnchorsTests, VacancyLabelsAreAddedOncePerVacancy)
	{
		RendererWindowState window = WindowWithAtomAndVacancies();
		const std::vector<SceneObjectId> added = AddVacancyLabels(window, {});
		ASSERT_EQ(added.size(), 2u);
		ASSERT_EQ(window.freeLabels.size(), 2u);
		EXPECT_EQ(window.freeLabels[0].id, added[0]);
		EXPECT_EQ(window.freeLabels[0].text, "V_B");
		EXPECT_EQ(window.freeLabels[0].anchorVacancy, std::optional<std::size_t>(0));
		EXPECT_EQ(window.freeLabels[0].worldPosition, glm::vec3(0.0f, 2.0f, 0.0f));
		EXPECT_EQ(window.freeLabels[1].text, "V_N");

		EXPECT_TRUE(AddVacancyLabels(window, {}).empty());
		EXPECT_TRUE(AddVacancyLabels(window, {1}).empty());
		EXPECT_EQ(window.freeLabels.size(), 2u);
	}

	TEST(SceneFreeLabelAnchorsTests, AnchorsSurviveSaveAndLoad)
	{
		RendererWindowState window = WindowWithAtomAndVacancies();
		{
			RendererWindowState::FreeLabel &label = AddLabel(window);
			label.anchorAtom = 0;
			label.anchorOffset = glm::vec3(0.0f, 0.0f, 0.5f);
		}
		AddLabel(window).anchorVacancy = 1;
		AddLabel(window).worldPosition = glm::vec3(9.0f);

		SceneObjectsFile file;
		PersistedStructureSceneObjects entry;
		entry.structureKey = "structures/hBN/POSCAR";
		entry.objects = ExtractPersistedSceneObjects(window);
		file.structures.push_back(entry);
		SceneObjectsFile loaded;
		std::vector<StructuredError> parseWarnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, parseWarnings, error)) << error;

		RendererWindowState reopened = WindowWithAtomAndVacancies();
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(reopened, loaded.structures[0].objects, warnings);
		ASSERT_EQ(reopened.freeLabels.size(), 3u);
		EXPECT_EQ(reopened.freeLabels[0].anchorAtom, std::optional<std::size_t>(0));
		EXPECT_EQ(reopened.freeLabels[0].anchorOffset, glm::vec3(0.0f, 0.0f, 0.5f));
		EXPECT_EQ(reopened.freeLabels[1].anchorVacancy, std::optional<std::size_t>(1));
		EXPECT_FALSE(reopened.freeLabels[2].anchorAtom.has_value());
		EXPECT_FALSE(reopened.freeLabels[2].anchorVacancy.has_value());
		EXPECT_EQ(reopened.freeLabels[2].worldPosition, glm::vec3(9.0f));
	}
} // namespace DefectStudio::Tests
