#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Presentation/Panels/SceneObjectMultiSelection.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneOutlinerSelectionTests, PlainClickReplacesTheSelection)
	{
		const std::vector<std::size_t> rows = {10, 20, 30, 40};

		const std::vector<std::size_t> selected = ApplySceneOutlinerSelection<std::size_t>(
			{10, 20}, rows, 30, std::optional<std::size_t>{20}, SceneOutlinerSelectionModifier::Replace);

		EXPECT_EQ(selected, std::vector<std::size_t>({30}));
	}

	TEST(SceneOutlinerSelectionTests, CtrlClickTogglesWithoutDroppingOtherRows)
	{
		const std::vector<std::size_t> rows = {10, 20, 30, 40};

		const std::vector<std::size_t> added = ApplySceneOutlinerSelection<std::size_t>(
			{10, 20}, rows, 30, std::optional<std::size_t>{20}, SceneOutlinerSelectionModifier::Toggle);
		const std::vector<std::size_t> removed = ApplySceneOutlinerSelection<std::size_t>(
			added, rows, 20, std::optional<std::size_t>{30}, SceneOutlinerSelectionModifier::Toggle);

		EXPECT_EQ(added, std::vector<std::size_t>({10, 20, 30}));
		EXPECT_EQ(removed, std::vector<std::size_t>({10, 30}));
	}

	TEST(SceneOutlinerSelectionTests, ShiftClickSelectsTheInclusiveAnchorRange)
	{
		const std::vector<std::size_t> rows = {10, 20, 30, 40};

		const std::vector<std::size_t> selected = ApplySceneOutlinerSelection<std::size_t>(
			{10}, rows, 40, std::optional<std::size_t>{20}, SceneOutlinerSelectionModifier::Range);

		EXPECT_EQ(selected, std::vector<std::size_t>({20, 30, 40}));
	}

	TEST(SceneOutlinerSelectionTests, ShiftClickWithoutAnAnchorBehavesLikePlainClick)
	{
		const std::vector<std::size_t> rows = {10, 20, 30, 40};

		const std::vector<std::size_t> selected = ApplySceneOutlinerSelection<std::size_t>(
			{10, 20}, rows, 40, std::nullopt, SceneOutlinerSelectionModifier::Range);

		EXPECT_EQ(selected, std::vector<std::size_t>({40}));
	}

	TEST(SceneObjectMultiSelectionTests, AppliesOnlyTheEditedOrbitalFieldToSelectedObjects)
	{
		std::vector<RendererWindowState::SceneOrbital> orbitals(3);
		orbitals[0].id = SceneObjectId{10};
		orbitals[1].id = SceneObjectId{20};
		orbitals[2].id = SceneObjectId{30};
		orbitals[0].shell = 4;
		orbitals[1].shell = 2;
		orbitals[2].shell = 3;
		orbitals[0].scale = 1.0f;
		orbitals[1].scale = 7.0f;

		ApplySelectedSceneObjectField(
			orbitals, {SceneObjectId{10}, SceneObjectId{20}}, 0,
			&RendererWindowState::SceneOrbital::shell);

		EXPECT_EQ(orbitals[0].shell, 4);
		EXPECT_EQ(orbitals[1].shell, 4);
		EXPECT_EQ(orbitals[2].shell, 3);
		EXPECT_FLOAT_EQ(orbitals[1].scale, 7.0f);
	}

	TEST(SceneObjectMultiSelectionTests, FindsFirstValidSelectionAndIgnoresMissingPlaneIds)
	{
		std::vector<RendererWindowState::ScenePlane> planes(2);
		planes[0].id = SceneObjectId{5};
		planes[1].id = SceneObjectId{6};
		planes[1].alpha = 0.8f;
		const std::vector<SceneObjectId> selection = {SceneObjectId{99}, SceneObjectId{6}};

		const std::size_t representative = FirstSelectedSceneObjectIndex(planes, selection);
		ApplySelectedSceneObjectField(
			planes, selection, representative, &RendererWindowState::ScenePlane::alpha);

		ASSERT_EQ(representative, 1u);
		EXPECT_FLOAT_EQ(planes[0].alpha, 0.35f);
		EXPECT_FLOAT_EQ(planes[1].alpha, 0.8f);
	}
} // namespace DefectStudio::Tests
