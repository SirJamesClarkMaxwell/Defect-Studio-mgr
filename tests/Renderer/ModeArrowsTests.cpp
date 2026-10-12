#include <gtest/gtest.h>

#include <limits>

#include "Renderer/ModeArrows.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStructureData ThreeAtoms()
		{
			RendererStructureData structure;
			for (int i = 0; i < 3; ++i)
			{
				RendererAtomData atom;
				atom.element = "C";
				atom.cartesianPosition = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
				structure.atoms.push_back(atom);
			}
			return structure;
		}

		ModeArrowsState TwoArrows()
		{
			ModeArrowsState state;
			state.arrows = {{0, glm::vec3(0.0f, 2.0f, 0.0f)}, {2, glm::vec3(0.0f, 0.0f, 1.0f)}};
			state.lowColor = glm::vec3(0.0f, 0.0f, 1.0f);
			state.highColor = glm::vec3(1.0f, 0.0f, 0.0f);
			return state;
		}
	} // namespace

	TEST(ModeArrowsTests, SegmentsStartAtTheCurrentAtomPosition)
	{
		RendererStructureData structure = ThreeAtoms();
		structure.atoms[2].cartesianPosition = glm::vec3(5.0f, 5.0f, 5.0f); // e.g. mid-animation
		const std::vector<ArrowSegment> segments = BuildModeArrowSegments(structure, TwoArrows());
		ASSERT_EQ(segments.size(), 2u);
		EXPECT_EQ(segments[0].base, glm::vec3(0.0f, 0.0f, 0.0f));
		EXPECT_EQ(segments[0].tip, glm::vec3(0.0f, 2.0f, 0.0f));
		EXPECT_EQ(segments[1].base, glm::vec3(5.0f, 5.0f, 5.0f));
		EXPECT_EQ(segments[1].tip, glm::vec3(5.0f, 5.0f, 6.0f));
	}

	TEST(ModeArrowsTests, ColourRampsFromLowToHighOnRelativeLength)
	{
		const std::vector<ArrowSegment> segments = BuildModeArrowSegments(ThreeAtoms(), TwoArrows());
		ASSERT_EQ(segments.size(), 2u);
		EXPECT_FLOAT_EQ(segments[0].color.r, 1.0f); // the longest arrow is highColor
		EXPECT_FLOAT_EQ(segments[0].color.b, 0.0f);
		EXPECT_FLOAT_EQ(segments[1].color.r, 0.5f); // half as long -> halfway
		EXPECT_FLOAT_EQ(segments[1].color.b, 0.5f);
		EXPECT_FLOAT_EQ(segments[1].color.a, 1.0f);
	}

	TEST(ModeArrowsTests, HiddenAtomsOutOfRangeAndDegenerateArrowsAreSkipped)
	{
		RendererStructureData structure = ThreeAtoms();
		structure.atoms[0].visible = false;
		ModeArrowsState state = TwoArrows();
		state.arrows.push_back({7, glm::vec3(1.0f)});
		state.arrows.push_back({1, glm::vec3(0.0f)});
		state.arrows.push_back({1, glm::vec3(std::numeric_limits<float>::quiet_NaN())});
		const std::vector<ArrowSegment> segments = BuildModeArrowSegments(structure, state);
		ASSERT_EQ(segments.size(), 1u);
		EXPECT_EQ(segments[0].base, glm::vec3(2.0f, 0.0f, 0.0f));
		// with atom 0 hidden the remaining arrow is the longest drawn one
		EXPECT_FLOAT_EQ(segments[0].color.r, 1.0f);

		state.onlyForVisibleAtoms = false;
		EXPECT_EQ(BuildModeArrowSegments(structure, state).size(), 2u);
	}

	TEST(ModeArrowsTests, InvisibleStateDrawsNothing)
	{
		ModeArrowsState state = TwoArrows();
		state.visible = false;
		EXPECT_TRUE(BuildModeArrowSegments(ThreeAtoms(), state).empty());
	}
} // namespace DefectStudio::Tests
