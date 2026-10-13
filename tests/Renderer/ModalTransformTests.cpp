#include <gtest/gtest.h>

#include <cmath>
#include <limits>

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

	TEST(ModalTransformTests, FractionEntryGuardsSlashAndAllowsDecimalsOnBothSides)
	{
		ModalTransformSession session;
		for (const auto text : {"", "-", "1."})
		{
			session.numericText = text;
			AppendNumericChar(session, '/');
			EXPECT_EQ(session.numericText, text);
		}
		session.numericText.clear();
		for (char ch : std::string("0.5/2.0")) AppendNumericChar(session, ch);
		AppendNumericChar(session, '/');
		AppendNumericChar(session, '.');
		EXPECT_EQ(session.numericText, "0.5/2.0");
		AppendNumericChar(session, '-');
		EXPECT_FLOAT_EQ(*NumericValue(session), -0.25f);
		EraseNumericChar(session);
		EXPECT_EQ(session.numericText, "-0.5/2.");
	}

	TEST(ModalTransformTests, FractionsRemainDegreesAndFactorsForRotateAndScale)
	{
		ModalTransformSession session;
		session.bases.lattice = MakeHexLattice() * 5.43f;
		session.constraint = Axis(2, TransformOrientation::Lattice);
		session.op = ModalTransformOp::Rotate;
		session.numericText = "90/2";
		const auto rotation = EvaluateModalTransform(session, MakeTopView(), {}, SnapMode::Increment, {});
		ExpectVec3Near(rotation.rotation * glm::vec3(1, 0, 0), {std::sqrt(0.5f), std::sqrt(0.5f), 0});
		session.op = ModalTransformOp::Scale;
		session.numericText = "1/8";
		const auto scaled = EvaluateModalTransform(session, MakeTopView(), {}, SnapMode::Fine, {});
		ExpectVec3Near(scaled.linear * glm::vec3(1), {1, 1, 0.125f});
	}

	TEST(ModalTransformTests, LatticeHeaderShowsTypedTextFractionsDecimalsLengthAndFineSnap)
	{
		ModalTransformSession session;
		session.bases.lattice = MakeHexLattice() * 5.43f;
		session.constraint = Axis(0, TransformOrientation::Lattice);
		session.numericText = "1/8";
		const auto delta = EvaluateModalTransform(session, MakeTopView(), {}, SnapMode::Off, {});
		EXPECT_EQ(FormatModalTransformHeader(session, delta, SnapMode::Increment, {}), "Move  a (Lattice)  D 1/8  +1/8 a  = 0.679 A  Snap 1/8");
		session.numericText.clear();
		session.constraint = Plane(2, TransformOrientation::Lattice);
		TransformDelta moved;
		moved.translation = *session.bases.lattice * glm::vec3(-0.75f, 0.137f, 0.0f);
		const auto header = FormatModalTransformHeader(session, moved, SnapMode::Fine, {});
		for (const auto text : {"-3/4 a", "+0.1370 b", " A", "Snap 1/24"}) EXPECT_NE(header.find(text), std::string::npos) << header;
		EXPECT_EQ(header.find(" c"), std::string::npos) << header;
	}

	TEST(ModalTransformTests, LatticeMouseSnapUsesFractionGridAndKeepsConstraints)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice();
		(*bases.lattice)[0] *= 5.43f;
		(*bases.lattice)[1] *= 3.0f;
		(*bases.lattice)[2] = glm::vec3(0.5f, 0.25f, 2.0f);
		for (const auto constraint : {TransformConstraint{}, Axis(1, TransformOrientation::Lattice), Plane(2, TransformOrientation::Lattice)})
		{
			auto session = BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Lattice, bases, {}, {50, 50});
			session.constraint = constraint;
			const auto unsnapped = EvaluateModalTransform(session, MakeTopView(), {72.34f, 61.23f}, SnapMode::Off, {}).translation;
			for (const auto mode : {SnapMode::Increment, SnapMode::Fine})
			{
				const float step = mode == SnapMode::Increment ? 1.0f / 8.0f : 1.0f / 24.0f;
				const auto coordinates = glm::inverse(*bases.lattice) * EvaluateModalTransform(session, MakeTopView(), {72.34f, 61.23f}, mode, {}).translation;
				const glm::vec3 expected = constraint.kind == ConstraintKind::Axis
					? glm::vec3(0, mode == SnapMode::Increment ? -0.75f : -17.0f / 24.0f, 0)
					: mode == SnapMode::Increment ? glm::vec3(0.25f, -0.375f, 0) : glm::vec3(7.0f / 24.0f, -10.0f / 24.0f, 0);
				ExpectVec3Near(coordinates, expected);
				for (int axis = 0; axis < 3; ++axis)
				{
					EXPECT_NEAR(coordinates[axis] / step, std::round(coordinates[axis] / step), 1e-5f);
					if ((constraint.kind == ConstraintKind::Axis && axis != constraint.axis) ||
						(constraint.kind == ConstraintKind::Plane && axis == constraint.axis))
					{
						EXPECT_NEAR(coordinates[axis], 0.0f, 1e-5f);
					}
				}
			}
			session.latticeUnits = false;
			ExpectVec3Near(EvaluateModalTransform(session, MakeTopView(), {72.34f, 61.23f}, SnapMode::Off, {}).translation, unsnapped);
		}
	}

	TEST(ModalTransformTests, LatticeNumericTranslationUsesRawColumnsForAxisFreeAndPlane)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice();
		(*bases.lattice)[0] *= 5.43f;
		(*bases.lattice)[1] *= 3.0f;
		auto session = BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Lattice, bases, {}, {50, 50});
		session.numericText = "1/8";
		for (const auto constraint : {Axis(0, TransformOrientation::Lattice), TransformConstraint{}, Plane(1, TransformOrientation::Lattice)})
		{
			session.constraint = constraint;
			EXPECT_NEAR(EvaluateModalTransform(session, MakeTopView(), {90, 10}, SnapMode::Increment, {}).translation.x, 0.67875f, 1e-6f);
		}
		for (float length : {1.0f, 3.0f})
		{
			(*session.bases.lattice)[1] = MakeHexLattice()[1] * length;
			for (const auto constraint : {Axis(1, TransformOrientation::Lattice), Plane(0, TransformOrientation::Lattice)})
			{
				session.constraint = constraint;
				ExpectVec3Near(EvaluateModalTransform(session, MakeTopView(), {}, SnapMode::Off, {}).translation, MakeHexLattice()[1] * (length / 8.0f));
			}
		}
	}

	TEST(ModalTransformTests, LatticeUnitsFollowEffectiveFrameAndRejectMissingOrSingularBases)
	{
		ModalTransformSession session;
		session.orientation = TransformOrientation::Lattice;
		EXPECT_FALSE(UsesLatticeUnits(session));
		session.bases.lattice = MakeHexLattice();
		EXPECT_TRUE(UsesLatticeUnits(session));
		session.constraint = Axis(0, TransformOrientation::Global);
		EXPECT_FALSE(UsesLatticeUnits(session));
		session.orientation = TransformOrientation::Global;
		session.constraint = Axis(0, TransformOrientation::Lattice);
		EXPECT_TRUE(UsesLatticeUnits(session));
		session.latticeUnits = false;
		EXPECT_FALSE(UsesLatticeUnits(session));
		session.latticeUnits = true;
		(*session.bases.lattice)[2] = (*session.bases.lattice)[1];
		EXPECT_FALSE(UsesLatticeUnits(session));
		(*session.bases.lattice)[2].z = std::numeric_limits<float>::infinity();
		EXPECT_FALSE(UsesLatticeUnits(session));
	}

	TEST(ModalTransformTests, LatticeUnitsOffPreservesAngstromNumericSnapAndHeader)
	{
		TransformBases bases;
		bases.lattice = MakeHexLattice() * 5.43f;
		auto session = BeginModalTransform(ModalTransformOp::Translate, TransformOrientation::Lattice, bases, {}, {50, 50});
		session.constraint = Axis(0, TransformOrientation::Lattice);
		session.latticeUnits = false;
		session.numericText = "1/8";
		ExpectVec3Near(EvaluateModalTransform(session, MakeTopView(), {}, SnapMode::Off, {}).translation, {0.125f, 0, 0});
		EXPECT_EQ(FormatModalTransformHeader(session, {}, SnapMode::Increment, {}), "Move  a (Lattice)  D 1/8  Snap 0.1");
		session.numericText.clear();
		ExpectVec3Near(EvaluateModalTransform(session, MakeTopView(), {72.34f, 50}, SnapMode::Increment, {}).translation, {2.2f, 0, 0});
		ExpectVec3Near(EvaluateModalTransform(session, MakeTopView(), {72.34f, 50}, SnapMode::Fine, {}).translation, {2.23f, 0, 0});
	}

	TEST(ModalTransformTests, NumericFractionsParseAndRejectIncompleteOrInvalidValues)
	{
		ModalTransformSession session;
		for (const auto &[text, value] : {std::pair{"1/8", 0.125f}, {"-3/4", -0.75f}, {"0.5/2", 0.25f}, {"1.5/0.5", 3.0f}})
		{
			session.numericText = text;
			ASSERT_TRUE(NumericValue(session)) << text;
			EXPECT_FLOAT_EQ(*NumericValue(session), value);
		}
		for (const auto text : {"1/0", "1/", "/", "1/2/3", "-/2", "nan/2", "1/inf", "3e38/0.01"})
		{
			session.numericText = text;
			EXPECT_FALSE(NumericValue(session)) << text;
		}
	}

	TEST(ModalTransformTests, SimpleFractionDisplayChoosesNearestAndRejectsUnsafeValues)
	{
		EXPECT_EQ(NearestSimpleFraction(0.3333333f), (std::pair{1, 3}));
		EXPECT_EQ(NearestSimpleFraction(-0.75f), (std::pair{-3, 4}));
		EXPECT_EQ(NearestSimpleFraction(0.0f), (std::pair{0, 1}));
		EXPECT_EQ(NearestSimpleFraction(1.0f / 24.0f), (std::pair{1, 24}));
		for (float value : {0.137f, 1.0f / 25.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::max()})
			EXPECT_FALSE(NearestSimpleFraction(value));
	}
} // namespace
