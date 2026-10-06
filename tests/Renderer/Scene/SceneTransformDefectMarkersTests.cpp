#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState WindowWithMarkers()
		{
			RendererWindowState window;
			RendererVacancyData vacancy;
			vacancy.cartesianPosition = glm::vec3(1.0f, 0.0f, 0.0f);
			window.structure.vacancies = {vacancy, vacancy};
			window.structure.defectFrame = DefectFrame{glm::vec3(0.0f), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)};
			RendererAtomData atom;
			window.structure.atoms = {atom};
			window.selectedAtomIndices = {0};
			return window;
		}
	} // namespace

	// A selected vacancy is a G target; the atoms that happen to be selected too are not.
	TEST(SceneTransformDefectMarkersTests, TranslateMovesTheSelectedVacancyOnlyAndRestores)
	{
		RendererWindowState window = WindowWithMarkers();
		window.selectedVacancies = {1};
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		ASSERT_EQ(snapshot.vacancies.size(), 1u);
		EXPECT_TRUE(snapshot.atoms.empty());
		EXPECT_TRUE(HasDefectMarkerTransformTargets(snapshot));

		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(0.0f, 2.0f, 0.0f);
		ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_EQ(window.structure.vacancies[1].cartesianPosition, glm::vec3(1.0f, 2.0f, 0.0f));
		EXPECT_EQ(window.structure.vacancies[0].cartesianPosition, glm::vec3(1.0f, 0.0f, 0.0f));

		RestoreSceneTransformSelection(window, snapshot);
		EXPECT_EQ(window.structure.vacancies[1].cartesianPosition, glm::vec3(1.0f, 0.0f, 0.0f));
	}

	// R turns the axes and keeps them a right-handed orthonormal triad; the local basis is the frame.
	TEST(SceneTransformDefectMarkersTests, RotateTurnsTheSelectedAxes)
	{
		RendererWindowState window = WindowWithMarkers();
		window.defectFrameSelected = true;
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		ASSERT_TRUE(snapshot.defectFrame.has_value());
		ASSERT_TRUE(SceneTransformLocalBasis(snapshot).has_value());

		SceneTransformDelta delta;
		delta.spatial.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Rotate, TransformPivotMode::Median, glm::vec3(0.0f));
		const DefectFrame &frame = *window.structure.defectFrame;
		EXPECT_NEAR(frame.z.y, -1.0f, 1e-5f);
		EXPECT_NEAR(glm::dot(glm::cross(frame.x, frame.y), frame.z), 1.0f, 1e-5f);

		window.showDefectFrame = false;
		EXPECT_FALSE(CaptureSceneTransformSelection(window).defectFrame.has_value());
	}

	// Pinned children ride along with the selected axes and turn about the axes' origin.
	TEST(SceneTransformDefectMarkersTests, PinnedChildrenFollowTheAxesAboutTheirOrigin)
	{
		RendererWindowState window = WindowWithMarkers();
		window.defectFrameSelected = true;
		window.defectFrameChildren.vacancies = {0};
		SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		ASSERT_EQ(snapshot.vacancies.size(), 1u);
		const std::vector<glm::vec3> pivots = SceneTransformPivotPositions(snapshot);
		ASSERT_EQ(pivots.size(), 1u);
		EXPECT_EQ(pivots.front(), glm::vec3(0.0f));
		EXPECT_EQ(*SceneTransformLocalBasis(snapshot), glm::mat3(1.0f));

		SceneTransformDelta delta;
		delta.spatial.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Rotate, TransformPivotMode::Median, pivots.front());
		EXPECT_NEAR(window.structure.vacancies[0].cartesianPosition.y, 1.0f, 1e-5f);

		// Not selected = not carried.
		window.defectFrameSelected = false;
		EXPECT_TRUE(CaptureSceneTransformSelection(window).vacancies.empty());
	}

	// The gizmo stands where the label was drawn (auto-offset), the transform still writes the pin.
	TEST(SceneTransformDefectMarkersTests, LabelGizmoAnchorIsTheDrawnLabel)
	{
		RendererWindowState window;
		RendererWindowState::FreeLabel label;
		label.id = window.sceneRegistry.AllocateObjectId();
		label.worldPosition = glm::vec3(1.0f, 0.0f, 0.0f);
		window.freeLabels = {label};
		window.selectedFreeLabels = {label.id};
		window.labelPickQuads.free = {LabelPickQuad{glm::vec3(1.0f, 0.5f, 0.0f), {}, {}, 0.0f, true}};
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		EXPECT_EQ(SceneTransformAnchorPositions(window, snapshot).front(), glm::vec3(1.0f, 0.5f, 0.0f));
		EXPECT_EQ(SceneTransformPivotPositions(window, snapshot).front(), glm::vec3(1.0f, 0.0f, 0.0f));
	}
} // namespace DefectStudio::Tests
