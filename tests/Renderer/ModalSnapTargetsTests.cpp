#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/RendererTypes.hpp"
#include "Renderer/Scene/ModalSnapTargets.hpp"

namespace
{
	using namespace DefectStudio;

	RendererStructureData MakeStructure()
	{
		RendererStructureData structure;
		structure.atoms = {{"Cu", glm::vec3(0, 0, 0)}, {"S", glm::vec3(2, 0, 0)}};
		structure.bonds = {{0, 1}};
		structure.lattice = glm::mat3(2.0f);
		structure.periodic = true;
		return structure;
	}

	ModalTransformView MakeTopView()
	{
		return {glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0), glm::vec3(0, 1, 0)),
			glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 100.0f), glm::vec2(0), glm::vec2(100)};
	}

	TEST(ModalSnapTargetsTests, CollectsAtomsBondAndAllTwentySixCellPoints)
	{
		const auto targets = CollectSnapTargets(MakeStructure(), {}, true);
		EXPECT_EQ(targets.size(), 29u);
		for (const auto [kind, count] : std::array{
			std::pair{SnapTargetKind::Atom, 2}, std::pair{SnapTargetKind::BondMidpoint, 1},
			std::pair{SnapTargetKind::CellCorner, 8}, std::pair{SnapTargetKind::CellEdgeMidpoint, 12},
			std::pair{SnapTargetKind::CellFaceCentre, 6}})
		{
			EXPECT_EQ(std::count_if(targets.begin(), targets.end(), [kind](const SnapTarget &target) {
				return target.kind == kind;
			}), count);
		}
		EXPECT_EQ(targets[2].position, glm::vec3(1, 0, 0));
		EXPECT_EQ(targets[2].atomIndex, 0);
		EXPECT_EQ(targets[2].secondAtomIndex, 1);
	}

	TEST(ModalSnapTargetsTests, ExcludesMovedAtomsAndIncidentBonds)
	{
		const std::array<std::size_t, 1> moved = {0};
		const auto targets = CollectSnapTargets(MakeStructure(), moved, true);
		ASSERT_EQ(targets.size(), 27u);
		EXPECT_EQ(targets[0].atomIndex, 1);
		EXPECT_EQ(targets[0].position, glm::vec3(2, 0, 0));
		EXPECT_EQ(CollectSnapTargets(MakeStructure(), moved, false).size(), 1u);
	}

	TEST(ModalSnapTargetsTests, UsesDisplayedPeriodicBondAndNeverSynthesizesAtoms)
	{
		auto structure = MakeStructure();
		structure.bonds[0].secondAtomPeriodicOffset = glm::vec3(-4, 0, 0);
		const auto targets = CollectSnapTargets(structure, {}, false);
		ASSERT_EQ(targets.size(), 3u);
		EXPECT_EQ(targets[2].position, glm::vec3(-1, 0, 0));
		EXPECT_EQ(targets[0].image, glm::ivec3(0));
		EXPECT_EQ(targets[1].image, glm::ivec3(0));
	}

	TEST(ModalSnapTargetsTests, RejectsHiddenInvalidGeometryAndMeaninglessCell)
	{
		auto structure = MakeStructure();
		structure.periodic = false;
		EXPECT_EQ(CollectSnapTargets(structure, {}, true).size(), 3u);
		structure.periodic = true;
		structure.lattice[2] = structure.lattice[1];
		EXPECT_EQ(CollectSnapTargets(structure, {}, true).size(), 3u);
		structure.atoms[0].visible = false;
		structure.bonds.push_back({0, 99});
		EXPECT_EQ(CollectSnapTargets(structure, {}, true).size(), 1u);
		structure.atoms[1].cartesianPosition.x = std::numeric_limits<float>::quiet_NaN();
		EXPECT_TRUE(CollectSnapTargets(structure, {}, true).empty());
	}

	TEST(ModalSnapTargetsTests, PicksNearestInPixelsAndBreaksTiesTowardCamera)
	{
		std::array targets = {SnapTarget{SnapTargetKind::Atom, glm::vec3(1, 0, 0), 0},
			SnapTarget{SnapTargetKind::Atom, glm::vec3(2, 0, 0), 1}};
		auto view = MakeTopView();
		auto hit = PickSnapTarget(targets, view, glm::vec2(69, 50), 12.0f);
		ASSERT_TRUE(hit);
		EXPECT_EQ(hit->atomIndex, 1);
		EXPECT_FALSE(PickSnapTarget(targets, view, glm::vec2(90, 50), 12.0f));
		EXPECT_TRUE(PickSnapTarget(targets, view, glm::vec2(82, 50), 12.0f));
		targets[1].position = glm::vec3(1, 0, 2);
		hit = PickSnapTarget(targets, view, glm::vec2(60, 50), 12.0f);
		ASSERT_TRUE(hit);
		EXPECT_EQ(hit->atomIndex, 1);
		view.viewportOrigin = glm::vec2(100, 200);
		EXPECT_TRUE(PickSnapTarget(targets, view, glm::vec2(160, 250), 12.0f));
		EXPECT_FALSE(PickSnapTarget(targets, view, glm::vec2(60, 50), 12.0f));
	}

	TEST(ModalSnapTargetsTests, RejectsClippedNonFiniteAndInvalidPickInputs)
	{
		const std::array targets = {SnapTarget{SnapTargetKind::Atom, glm::vec3(0, 0, 11)},
			SnapTarget{SnapTargetKind::Atom, glm::vec3(0, 0, -100)},
			SnapTarget{SnapTargetKind::Atom, glm::vec3(std::numeric_limits<float>::quiet_NaN())}};
		auto view = MakeTopView();
		EXPECT_FALSE(PickSnapTarget(targets, view, glm::vec2(50), 12));
		EXPECT_FALSE(PickSnapTarget({}, view, glm::vec2(50), 12));
		EXPECT_FALSE(PickSnapTarget(targets, view, glm::vec2(50), -1));
		view.viewportSize.x = 0;
		EXPECT_FALSE(PickSnapTarget(targets, view, glm::vec2(50), 12));
	}

	TEST(ModalSnapTargetsTests, TranslatesExactlyOrProjectsThroughTheAnchor)
	{
		const TransformBases bases;
		EXPECT_EQ(TranslationOntoTarget(glm::vec3(0), glm::vec3(1, 2, 0), {}, bases), glm::vec3(1, 2, 0));
		const TransformConstraint axis{ConstraintKind::Axis, 0, TransformOrientation::Global};
		const TransformConstraint plane{ConstraintKind::Plane, 2, TransformOrientation::Global};
		EXPECT_EQ(TranslationOntoTarget(glm::vec3(0), glm::vec3(1, 2, 0), axis, bases), glm::vec3(1, 0, 0));
		EXPECT_EQ(TranslationOntoTarget(glm::vec3(0), glm::vec3(1, 2, 3), plane, bases), glm::vec3(1, 2, 0));
		EXPECT_EQ(TranslationOntoTarget(glm::vec3(3, 4, 5), glm::vec3(4, 6, 8), plane, bases), glm::vec3(1, 2, 0));
	}

	TEST(ModalSnapTargetsTests, HeadersNameTheTargetAndShowConstraintResidual)
	{
		const auto structure = MakeStructure();
		const SnapTarget bond{SnapTargetKind::BondMidpoint, glm::vec3(1, 0, 0), 0, glm::ivec3(0), 1};
		EXPECT_EQ(FormatSnapTargetHeader(bond, structure, bond.position), "  → środek wiązania Cu1–S2");
		EXPECT_EQ(FormatSnapTargetHeader({SnapTargetKind::Atom, glm::vec3(0), 1}, structure, glm::vec3(0.37f, 0, 0)),
			"  → atom S2 (odległość od celu 0.37 Å)");
		EXPECT_EQ(FormatSnapTargetHeader({SnapTargetKind::CellCorner, glm::vec3(0)}, structure, glm::vec3(0)),
			"  → narożnik komórki");
	}

	TEST(ModalSnapTargetsTests, AxisPickingRequiresTwoDistinctPositionsAndRetainsThePivot)
	{
		PickedAxisState picking{true, std::nullopt, {}};
		ModalTransformSession session;
		session.pivot = glm::vec3(3, 4, 5);
		const auto structure = MakeStructure();
		picking.Pick(structure, 0, session);
		EXPECT_TRUE(picking.active);
		picking.Pick(structure, 0, session);
		EXPECT_TRUE(picking.active);
		picking.Pick(structure, 1, session);
		EXPECT_FALSE(picking.active);
		EXPECT_EQ(session.constraint.space, TransformOrientation::Picked);
		EXPECT_EQ(session.pickedAxisLabel, "Cu1→S2");
		EXPECT_EQ(session.pivot, glm::vec3(3, 4, 5));
		ASSERT_TRUE(session.bases.picked);
		EXPECT_EQ((*session.bases.picked)[0], glm::vec3(1, 0, 0));
	}
}
