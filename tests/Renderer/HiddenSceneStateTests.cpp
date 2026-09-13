#include <gtest/gtest.h>

#include "Renderer/Scene/HiddenSceneState.hpp"

namespace
{
	using namespace DefectStudio;

	RendererStructureData MakeChain(std::size_t atomCount)
	{
		RendererStructureData structure;
		structure.atoms.resize(atomCount);
		for (std::size_t index = 0; index + 1 < atomCount; ++index)
		{
			RendererBondData bond;
			bond.firstAtomIndex = static_cast<std::uint32_t>(index + 1); // deliberately (max, min)
			bond.secondAtomIndex = static_cast<std::uint32_t>(index);
			structure.bonds.push_back(bond);
		}
		return structure;
	}

	TEST(HiddenSceneStateTests, CapturesHiddenAtomsAndNormalizedBondEndpoints)
	{
		RendererStructureData structure = MakeChain(5);
		structure.atoms[1].visible = false;
		structure.atoms[4].visible = false;
		structure.bonds[2].visible = false; // atoms 3-2

		const HiddenSceneState state = CaptureHiddenSceneState(structure);
		EXPECT_EQ(state.atomIndices, (std::vector<std::size_t>{1, 4}));
		EXPECT_EQ(state.bondEndpoints, (std::vector<std::pair<std::size_t, std::size_t>>{{2, 3}}));
	}

	TEST(HiddenSceneStateTests, RemovalShiftsLaterAtomsAndDropsRemovedOnes)
	{
		HiddenSceneState state;
		state.atomIndices = {0, 2, 3, 6, 9};
		const HiddenSceneState remapped = RemapHiddenSceneStateAfterAtomRemoval(state, {5, 3, 3});
		// 3 removed; 6 -> 4 (3 and 5 below it); 9 -> 7; 0 and 2 untouched.
		EXPECT_EQ(remapped.atomIndices, (std::vector<std::size_t>{0, 2, 4, 7}));
	}

	TEST(HiddenSceneStateTests, RemovalRemapsBondsAndDropsBondsOnRemovedAtoms)
	{
		HiddenSceneState state;
		state.bondEndpoints = {{0, 1}, {2, 3}, {4, 7}, {6, 8}};
		const HiddenSceneState remapped = RemapHiddenSceneStateAfterAtomRemoval(state, {3});
		EXPECT_EQ(remapped.bondEndpoints, (std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}, {3, 6}, {5, 7}}));
	}

	TEST(HiddenSceneStateTests, NoRemovalIsIdentity)
	{
		HiddenSceneState state;
		state.atomIndices = {1, 4};
		state.bondEndpoints = {{0, 2}};
		EXPECT_EQ(RemapHiddenSceneStateAfterAtomRemoval(state, {}), state);
	}

	TEST(HiddenSceneStateTests, ResolvesBondIndicesByEndpointsInEitherOrder)
	{
		const RendererStructureData structure = MakeChain(5); // bonds: (1,0) (2,1) (3,2) (4,3)
		EXPECT_EQ(
			ResolveHiddenBondIndices(structure, {{2, 3}, {0, 1}, {7, 8}}),
			(std::vector<std::size_t>{0, 2}));
	}

	// The user-visible bug: hide atoms, delete an atom before them, the hide must stay on the same
	// physical atoms (now one index lower), not jump to their neighbours.
	TEST(HiddenSceneStateTests, DeleteBeforeHiddenAtomsKeepsThemHiddenOnTheSameAtoms)
	{
		RendererStructureData before = MakeChain(6);
		before.atoms[4].visible = false;
		before.atoms[5].visible = false;

		const HiddenSceneState carried =
			RemapHiddenSceneStateAfterAtomRemoval(CaptureHiddenSceneState(before), {2});
		EXPECT_EQ(carried.atomIndices, (std::vector<std::size_t>{3, 4}));
	}
} // namespace
