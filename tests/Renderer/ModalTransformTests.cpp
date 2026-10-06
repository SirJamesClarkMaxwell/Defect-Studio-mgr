#include <gtest/gtest.h>

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Scene/ModalTransform.hpp"

namespace
{
	using namespace DefectStudio;

	constexpr float kEps = 1e-3f;

	void ExpectVec3Near(const glm::vec3 &actual, const glm::vec3 &expected)
	{
		EXPECT_NEAR(actual.x, expected.x, kEps);
		EXPECT_NEAR(actual.y, expected.y, kEps);
		EXPECT_NEAR(actual.z, expected.z, kEps);
	}

	// Orthographic camera on +Z looking at the origin: world (x, y, 0) lands on screen
	// (50 + 10x, 50 - 10y) in a 100x100 viewport.
	ModalTransformView MakeTopView()
	{
		ModalTransformView view;
		view.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		view.projection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 100.0f);
		view.viewportOrigin = glm::vec2(0.0f);
		view.viewportSize = glm::vec2(100.0f);
		return view;
	}

	// Hexagonal cell: a along X, b at 120 degrees in XY, c along Z.
	glm::mat3 MakeHexLattice()
	{
		glm::mat3 lattice(1.0f);
		lattice[0] = glm::vec3(1.0f, 0.0f, 0.0f);
		lattice[1] = glm::vec3(-0.5f, std::sqrt(3.0f) * 0.5f, 0.0f);
		lattice[2] = glm::vec3(0.0f, 0.0f, 1.0f);
		return lattice;
	}

	TransformConstraint Axis(int axis, TransformOrientation space)
	{
		return TransformConstraint{ConstraintKind::Axis, axis, space};
	}

	TransformConstraint Plane(int axis, TransformOrientation space)
	{
		return TransformConstraint{ConstraintKind::Plane, axis, space};
	}

	void ExpectConstraint(const TransformConstraint &actual, ConstraintKind kind, int axis, TransformOrientation space)
	{
		EXPECT_EQ(actual.kind, kind);
		if (kind == ConstraintKind::None)
			return;
		EXPECT_EQ(actual.axis, axis);
		EXPECT_EQ(actual.space, space);
	}

	TEST(ModalTransformTests, CycleGlobalWithoutLocalFrameGoesGlobalThenNone)
	{
		const TransformBases bases;
		TransformConstraint c = CycleConstraint({}, 0, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 0, TransformOrientation::Global);
		c = CycleConstraint(c, 0, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::None, -1, TransformOrientation::Global);
	}

	// Atoms have no Local frame: X X goes to the defect axes when the structure has them.
	TEST(ModalTransformTests, CycleGlobalWithoutLocalFallsBackToDefectAxes)
	{
		TransformBases bases;
		bases.defect = glm::mat3(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), glm::vec3(1, 0, 0));
		TransformConstraint c = CycleConstraint({}, 2, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 2, TransformOrientation::Global);
		c = CycleConstraint(c, 2, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 2, TransformOrientation::Defect);
		EXPECT_EQ(ResolveBasis(TransformOrientation::Defect, bases), *bases.defect);
		EXPECT_EQ(ResolveBasis(TransformOrientation::Defect, TransformBases{}), glm::mat3(1.0f));
	}

	TEST(ModalTransformTests, CycleGlobalWithLocalFrameGoesGlobalLocalNone)
	{
		TransformBases bases;
		bases.local = glm::mat3(1.0f);
		TransformConstraint c = CycleConstraint({}, 1, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 1, TransformOrientation::Global);
		c = CycleConstraint(c, 1, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 1, TransformOrientation::Local);
		c = CycleConstraint(c, 1, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::None, -1, TransformOrientation::Global);
	}

	TEST(ModalTransformTests, CycleLatticeGoesLatticeGlobalNone)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice();
		TransformConstraint c = CycleConstraint({}, 2, false, TransformOrientation::Lattice, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 2, TransformOrientation::Lattice);
		c = CycleConstraint(c, 2, false, TransformOrientation::Lattice, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 2, TransformOrientation::Global);
		c = CycleConstraint(c, 2, false, TransformOrientation::Lattice, bases);
		ExpectConstraint(c, ConstraintKind::None, -1, TransformOrientation::Global);
	}

	TEST(ModalTransformTests, MissingLatticeFallsBackToGlobalPrimary)
	{
		const TransformBases bases;
		TransformConstraint c = CycleConstraint({}, 0, false, TransformOrientation::Lattice, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 0, TransformOrientation::Global);
		c = CycleConstraint(c, 0, false, TransformOrientation::Lattice, bases);
		ExpectConstraint(c, ConstraintKind::None, -1, TransformOrientation::Global);
	}

	TEST(ModalTransformTests, DifferentAxisOrKindRestartsAtPrimary)
	{
		TransformBases bases;
		bases.local = glm::mat3(1.0f);
		TransformConstraint c = CycleConstraint({}, 0, false, TransformOrientation::Global, bases);
		c = CycleConstraint(c, 0, false, TransformOrientation::Global, bases); // Local X
		c = CycleConstraint(c, 1, false, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::Axis, 1, TransformOrientation::Global);
		c = CycleConstraint(c, 1, true, TransformOrientation::Global, bases);
		ExpectConstraint(c, ConstraintKind::Plane, 1, TransformOrientation::Global);
	}

	TEST(ModalTransformTests, ResolveBasisUsesFrameOrIdentity)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice();
		EXPECT_EQ(ResolveBasis(TransformOrientation::Lattice, bases), MakeHexLattice());
		EXPECT_EQ(ResolveBasis(TransformOrientation::Global, bases), glm::mat3(1.0f));
		EXPECT_EQ(ResolveBasis(TransformOrientation::Local, bases), glm::mat3(1.0f));
	}

	TEST(ModalTransformTests, LatticeAxisConstraintMovesAlongCellVector)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice();
		const glm::vec3 bHat = glm::normalize(MakeHexLattice()[1]);
		const glm::vec3 moved =
			ConstrainTranslation(glm::vec3(0.0f, 1.0f, 0.0f), Axis(1, TransformOrientation::Lattice), bases);
		ExpectVec3Near(moved, bHat * glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), bHat));
	}

	TEST(ModalTransformTests, LatticePlaneConstraintSpansTheOtherTwoCellVectors)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice();
		// Shift+X under Lattice = plane of b and c. X-direction input keeps only its b component.
		const glm::vec3 moved =
			ConstrainTranslation(glm::vec3(1.0f, 0.0f, 0.0f), Plane(0, TransformOrientation::Lattice), bases);
		ExpectVec3Near(moved, glm::vec3(0.25f, -std::sqrt(3.0f) * 0.25f, 0.0f));

		const glm::vec3 inPlane = MakeHexLattice()[1] * 2.0f + glm::vec3(0.0f, 0.0f, 0.5f);
		ExpectVec3Near(ConstrainTranslation(inPlane, Plane(0, TransformOrientation::Lattice), bases), inPlane);
	}

	TEST(ModalTransformTests, NoConstraintLeavesTranslationUnchanged)
	{
		const glm::vec3 delta(0.3f, -1.0f, 2.0f);
		ExpectVec3Near(ConstrainTranslation(delta, {}, {}), delta);
	}

	TEST(ModalTransformTests, ScaleMatrixScalesOnlyTheConstrainedCellVector)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice();
		const glm::mat3 m = ConstrainedScaleMatrix(2.0f, Axis(0, TransformOrientation::Lattice), bases);
		ExpectVec3Near(m * MakeHexLattice()[0], MakeHexLattice()[0] * 2.0f);
		ExpectVec3Near(m * MakeHexLattice()[1], MakeHexLattice()[1]);
		ExpectVec3Near(m * MakeHexLattice()[2], MakeHexLattice()[2]);

		const glm::mat3 uniform = ConstrainedScaleMatrix(3.0f, {}, bases);
		ExpectVec3Near(uniform * glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(3.0f, 6.0f, 9.0f));
	}

	TEST(ModalTransformTests, SnappingHelpers)
	{
		EXPECT_NEAR(SnapValue(0.234f, 0.1f), 0.2f, kEps);
		EXPECT_NEAR(SnapValue(-0.26f, 0.1f), -0.3f, kEps);
		EXPECT_NEAR(SnapValue(0.234f, 0.0f), 0.234f, kEps);

		const TransformSnapSteps steps;
		EXPECT_EQ(ResolveSnapStep(ModalTransformOp::Translate, SnapMode::Off, steps), 0.0f);
		EXPECT_NEAR(ResolveSnapStep(ModalTransformOp::Translate, SnapMode::Increment, steps), 0.1f, kEps);
		EXPECT_NEAR(ResolveSnapStep(ModalTransformOp::Rotate, SnapMode::Increment, steps), 5.0f, kEps);
		EXPECT_NEAR(ResolveSnapStep(ModalTransformOp::Rotate, SnapMode::Fine, steps), 0.5f, kEps);
		EXPECT_NEAR(ResolveSnapStep(ModalTransformOp::Scale, SnapMode::Fine, steps), 0.01f, kEps);

		EXPECT_EQ(SnapModeFromModifiers(false, false), SnapMode::Off);
		EXPECT_EQ(SnapModeFromModifiers(false, true), SnapMode::Off);
		EXPECT_EQ(SnapModeFromModifiers(true, false), SnapMode::Increment);
		EXPECT_EQ(SnapModeFromModifiers(true, true), SnapMode::Fine);
	}

	TEST(ModalTransformTests, NumericEntryEditing)
	{
		ModalTransformSession session;
		AppendNumericChar(session, '-');
		EXPECT_EQ(session.numericText, "-");
		EXPECT_FALSE(NumericValue(session).has_value());
		AppendNumericChar(session, '1');
		AppendNumericChar(session, '.');
		AppendNumericChar(session, '.');
		AppendNumericChar(session, '5');
		AppendNumericChar(session, 'x');
		EXPECT_EQ(session.numericText, "-1.5");
		ASSERT_TRUE(NumericValue(session).has_value());
		EXPECT_NEAR(*NumericValue(session), -1.5f, kEps);
		AppendNumericChar(session, '-');
		EXPECT_EQ(session.numericText, "1.5");
		EraseNumericChar(session);
		EXPECT_EQ(session.numericText, "1.");
		EraseNumericChar(session);
		EraseNumericChar(session);
		EraseNumericChar(session);
		EXPECT_TRUE(session.numericText.empty());
	}

	TEST(ModalTransformTests, FreeTranslateFollowsMouseInViewPlane)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Global, {}, glm::vec3(0.0f), {50.0f, 50.0f});
		const TransformDelta delta = EvaluateModalTransform(session, view, {70.0f, 60.0f}, SnapMode::Off, {});
		ExpectVec3Near(delta.translation, glm::vec3(2.0f, -1.0f, 0.0f));
	}

	TEST(ModalTransformTests, AxisTranslateIgnoresPerpendicularMouseMotion)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Global, {}, glm::vec3(0.0f), {50.0f, 50.0f});
		session.constraint = Axis(1, TransformOrientation::Global);
		ExpectVec3Near(EvaluateModalTransform(session, view, {70.0f, 50.0f}, SnapMode::Off, {}).translation, glm::vec3(0.0f));
		session.constraint = Axis(0, TransformOrientation::Global);
		ExpectVec3Near(
			EvaluateModalTransform(session, view, {70.0f, 60.0f}, SnapMode::Off, {}).translation, glm::vec3(2.0f, 0.0f, 0.0f));
	}

	TEST(ModalTransformTests, PlaneTranslateUsesRayPlaneHit)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Global, {}, glm::vec3(0.0f), {50.0f, 50.0f});
		session.constraint = Plane(2, TransformOrientation::Global);
		ExpectVec3Near(
			EvaluateModalTransform(session, view, {70.0f, 60.0f}, SnapMode::Off, {}).translation, glm::vec3(2.0f, -1.0f, 0.0f));
	}

	TEST(ModalTransformTests, TranslateSnapsTotalDeltaPerCoordinate)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Global, {}, glm::vec3(0.0f), {50.0f, 50.0f});
		session.constraint = Axis(0, TransformOrientation::Global);
		ExpectVec3Near(
			EvaluateModalTransform(session, view, {72.34f, 50.0f}, SnapMode::Increment, {}).translation,
			glm::vec3(2.2f, 0.0f, 0.0f));
		ExpectVec3Near(
			EvaluateModalTransform(session, view, {72.34f, 50.0f}, SnapMode::Fine, {}).translation,
			glm::vec3(2.23f, 0.0f, 0.0f));
	}

	TEST(ModalTransformTests, NumericTranslateOverridesMouseAndSnap)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Global, {}, glm::vec3(0.0f), {50.0f, 50.0f});
		session.constraint = Axis(1, TransformOrientation::Global);
		session.numericText = "2.34";
		ExpectVec3Near(
			EvaluateModalTransform(session, view, {90.0f, 10.0f}, SnapMode::Increment, {}).translation,
			glm::vec3(0.0f, 2.34f, 0.0f));

		session.constraint = {};
		ExpectVec3Near(
			EvaluateModalTransform(session, view, {90.0f, 10.0f}, SnapMode::Off, {}).translation, glm::vec3(2.34f, 0.0f, 0.0f));

		session.numericText = "-";
		ExpectVec3Near(EvaluateModalTransform(session, view, {90.0f, 10.0f}, SnapMode::Off, {}).translation, glm::vec3(0.0f));
	}

	TEST(ModalTransformTests, RotateFollowsScreenAngleAndAccumulatesPastHalfTurn)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Rotate, TransformOrientation::Global, {}, glm::vec3(0.0f), {60.0f, 50.0f});
		// Screen right -> screen up is counter-clockwise for the viewer on +Z.
		TransformDelta delta = EvaluateModalTransform(session, view, {50.0f, 40.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));

		delta = EvaluateModalTransform(session, view, {40.0f, 50.0f}, SnapMode::Off, {});
		delta = EvaluateModalTransform(session, view, {50.0f, 60.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(0.0f, -1.0f, 0.0f));
		EXPECT_NEAR(session.accumulatedAngleRadians, glm::radians(270.0f), kEps);
	}

	TEST(ModalTransformTests, RotateAboutAxisPointingAwayKeepsScreenDirection)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Rotate, TransformOrientation::Global, {}, glm::vec3(0.0f), {60.0f, 50.0f});
		TransformBases flipped;
		flipped.lattice = glm::mat3(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
		session.bases = flipped;
		session.constraint = Axis(2, TransformOrientation::Lattice);
		const TransformDelta delta = EvaluateModalTransform(session, view, {50.0f, 40.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	TEST(ModalTransformTests, RotateSnapsDegreesAndNumericIsRightHanded)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Rotate, TransformOrientation::Global, {}, glm::vec3(0.0f), {60.0f, 50.0f});
		session.constraint = Axis(2, TransformOrientation::Global);
		const float angle = glm::radians(7.0f);
		TransformDelta delta = EvaluateModalTransform(
			session, view, {50.0f + 10.0f * std::cos(angle), 50.0f - 10.0f * std::sin(angle)}, SnapMode::Increment, {});
		const glm::vec3 rotated = ApplyTransformDelta(delta, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f));
		ExpectVec3Near(rotated, glm::vec3(std::cos(glm::radians(5.0f)), std::sin(glm::radians(5.0f)), 0.0f));

		session.numericText = "90";
		delta = EvaluateModalTransform(session, view, {10.0f, 90.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	TEST(ModalTransformTests, ScaleUsesRadialRatioConstraintAndNumeric)
	{
		const ModalTransformView view = MakeTopView();
		ModalTransformSession session =
			BeginModalTransform(ModalTransformOp::Scale, TransformOrientation::Global, {}, glm::vec3(0.0f), {60.0f, 50.0f});
		TransformDelta delta = EvaluateModalTransform(session, view, {80.0f, 50.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(3.0f, 3.0f, 0.0f));

		session.constraint = Axis(0, TransformOrientation::Global);
		delta = EvaluateModalTransform(session, view, {80.0f, 50.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(3.0f, 1.0f, 0.0f));

		delta = EvaluateModalTransform(session, view, {72.34f, 50.0f}, SnapMode::Increment, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(2.2f, 0.0f, 0.0f));

		session.numericText = "2";
		delta = EvaluateModalTransform(session, view, {90.0f, 50.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(2.0f, 1.0f, 0.0f));

		session.numericText = "-";
		delta = EvaluateModalTransform(session, view, {90.0f, 50.0f}, SnapMode::Off, {});
		ExpectVec3Near(ApplyTransformDelta(delta, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f)), glm::vec3(1.0f, 1.0f, 0.0f));
	}

	TEST(ModalTransformTests, ApplyDeltaAboutPivot)
	{
		TransformDelta delta;
		delta.linear = glm::mat3(2.0f);
		delta.translation = glm::vec3(0.0f, 0.0f, 1.0f);
		ExpectVec3Near(
			ApplyTransformDelta(delta, glm::vec3(2.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)), glm::vec3(3.0f, 1.0f, 1.0f));
	}

	TEST(ModalTransformTests, PivotModes)
	{
		const std::vector<glm::vec3> positions = {glm::vec3(0.0f), glm::vec3(2.0f, 4.0f, 0.0f)};
		const glm::vec3 cursor(9.0f, 9.0f, 9.0f);
		ExpectVec3Near(ComputeTransformPivot(TransformPivotMode::Median, positions, cursor), glm::vec3(1.0f, 2.0f, 0.0f));
		ExpectVec3Near(
			ComputeTransformPivot(TransformPivotMode::IndividualOrigins, positions, cursor), glm::vec3(1.0f, 2.0f, 0.0f));
		ExpectVec3Near(ComputeTransformPivot(TransformPivotMode::Cursor3D, positions, cursor), cursor);
		ExpectVec3Near(
			ComputeTransformPivot(TransformPivotMode::Cursor3D, positions, std::nullopt), glm::vec3(1.0f, 2.0f, 0.0f));
		ExpectVec3Near(ComputeTransformPivot(TransformPivotMode::Median, {}, std::nullopt), glm::vec3(0.0f));
	}

	TEST(ModalTransformTests, HeaderNamesOpAxisSpaceValueAndSnap)
	{
		ModalTransformSession session;
		session.op = ModalTransformOp::Translate;
		session.constraint = Axis(0, TransformOrientation::Global);
		session.numericText = "2";
		std::string header = FormatModalTransformHeader(session, {}, SnapMode::Off, {});
		EXPECT_NE(header.find("Move"), std::string::npos) << header;
		EXPECT_NE(header.find("X"), std::string::npos) << header;
		EXPECT_NE(header.find("Global"), std::string::npos) << header;
		EXPECT_NE(header.find("2"), std::string::npos) << header;
		EXPECT_EQ(header.find("Snap"), std::string::npos) << header;

		session.op = ModalTransformOp::Rotate;
		session.constraint = Plane(0, TransformOrientation::Lattice);
		session.numericText.clear();
		header = FormatModalTransformHeader(session, {}, SnapMode::Increment, {});
		EXPECT_NE(header.find("Rotate"), std::string::npos) << header;
		EXPECT_NE(header.find("Lattice"), std::string::npos) << header;
		EXPECT_NE(header.find("b"), std::string::npos) << header;
		EXPECT_NE(header.find("c"), std::string::npos) << header;
		EXPECT_NE(header.find("Snap 5"), std::string::npos) << header;

		session.op = ModalTransformOp::Scale;
		session.constraint = {};
		header = FormatModalTransformHeader(session, {}, SnapMode::Fine, {});
		EXPECT_NE(header.find("Scale"), std::string::npos) << header;
		EXPECT_NE(header.find("Snap 0.01"), std::string::npos) << header;
	}
} // namespace
