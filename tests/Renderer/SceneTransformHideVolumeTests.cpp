#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "Presentation/Panels/SceneHideVolumeEditor.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Six atoms on the x axis every 3 A, a 2 A sphere anchored on atom 0 hiding what is outside it.
		RendererWindowState WindowWithVolume()
		{
			RendererWindowState window;
			window.structure.lattice = glm::mat3(40.0f);
			for (int index = 0; index < 6; ++index)
			{
				RendererAtomData atom;
				atom.element = "C";
				atom.cartesianPosition = glm::vec3(3.0f * static_cast<float>(index), 0.0f, 0.0f);
				window.structure.atoms.push_back(atom);
			}
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			SceneSystem::ApplySelectionAndVisibilityToScene(window.sceneRegistry, {0}, {}, {}, {});
			SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
			AddSceneHideVolume(window, HideVolumeKind::Sphere);
			window.sceneHideVolumes.front().halfExtents.x = 2.0f;
			SceneSystem::ApplySelectionAndVisibilityToScene(window.sceneRegistry, {0}, {}, {}, {});
			SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
			return window;
		}

		[[nodiscard]] int VisibleCount(const RendererWindowState &window)
		{
			int count = 0;
			for (const RendererAtomData &atom : window.structure.atoms)
				count += atom.visible ? 1 : 0;
			return count;
		}
	} // namespace

	TEST(SceneTransformHideVolumeTests, TheGizmoStandsOnTheVolumeNotOnTheSelectedAtoms)
	{
		RendererWindowState window = WindowWithVolume();
		window.structure.atoms[0].cartesianPosition = glm::vec3(0.0f);
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);
		ASSERT_EQ(snapshot.hideVolumes.size(), 1u);
		EXPECT_TRUE(snapshot.atoms.empty()); // atom 0 is selected but not dragged along
		EXPECT_TRUE(HasSceneObjectTransformTargets(snapshot));
		const std::vector<glm::vec3> pivots = SceneTransformPivotPositions(window, snapshot);
		ASSERT_EQ(pivots.size(), 1u);
		EXPECT_EQ(pivots[0], glm::vec3(0.0f));
	}

	TEST(SceneTransformHideVolumeTests, MovingTheVolumeHidesAndShowsAtomsWhileDraggingAndCancelRestores)
	{
		RendererWindowState window = WindowWithVolume();
		ASSERT_EQ(VisibleCount(window), 1); // only atom 0 is inside
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(9.0f, 0.0f, 0.0f);
		ApplySceneTransformSelection(window, snapshot, delta, ModalTransformOp::Translate,
			TransformPivotMode::Median, glm::vec3(0.0f));
		const SceneHideVolume &moved = window.sceneHideVolumes.front();
		EXPECT_TRUE(moved.anchorAtoms.empty()); // the anchor would pull it back
		EXPECT_EQ(moved.center, glm::vec3(9.0f, 0.0f, 0.0f));
		EXPECT_FALSE(window.structure.atoms[0].visible);
		EXPECT_TRUE(window.structure.atoms[3].visible);
		EXPECT_EQ(VisibleCount(window), 1);

		RestoreSceneTransformSelection(window, snapshot);
		EXPECT_EQ(window.sceneHideVolumes.front().anchorAtoms, (std::vector<std::size_t>{0}));
		EXPECT_TRUE(window.structure.atoms[0].visible);
		EXPECT_FALSE(window.structure.atoms[3].visible);
	}

	TEST(SceneTransformHideVolumeTests, ScalingGrowsTheRadiusAndRotatingInPlaceKeepsTheAnchor)
	{
		RendererWindowState window = WindowWithVolume();
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		SceneTransformDelta grow;
		grow.spatial.linear = glm::mat3(2.0f);
		grow.scaleFactor = 2.0f;
		ApplySceneTransformSelection(window, snapshot, grow, ModalTransformOp::Scale,
			TransformPivotMode::Median, glm::vec3(0.0f));
		EXPECT_FLOAT_EQ(window.sceneHideVolumes.front().halfExtents.x, 4.0f);
		EXPECT_EQ(VisibleCount(window), 2); // atoms 0 and 1 (3 A) now inside

		SceneTransformDelta turn;
		turn.spatial.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ApplySceneTransformSelection(window, snapshot, turn, ModalTransformOp::Rotate,
			TransformPivotMode::Median, glm::vec3(0.0f));
		const SceneHideVolume &turned = window.sceneHideVolumes.front();
		EXPECT_EQ(turned.anchorAtoms, (std::vector<std::size_t>{0}));
		EXPECT_FLOAT_EQ(turned.halfExtents.x, 2.0f); // each apply starts from the snapshot
		EXPECT_NEAR(turned.orientation[0].y, 1.0f, 1e-6f);
	}
	// A cubic cell whose a points along world y: the fractional volume's axes are not the world's.
	TEST(SceneTransformHideVolumeTests, AFractionalVolumeTurnsAndShowsItsAxesInWorldSpace)
	{
		RendererWindowState window = WindowWithVolume();
		window.structure.lattice = glm::mat3(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(-10.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 10.0f));
		SceneHideVolume &volume = window.sceneHideVolumes.front();
		volume.kind = HideVolumeKind::Cylinder;
		volume.frame = HideVolumeFrame::Fractional;
		volume.anchorAtoms.clear();
		volume.center = glm::vec3(0.5f);
		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(window);

		const std::optional<glm::mat3> local = SceneTransformLocalBasis(snapshot);
		ASSERT_TRUE(local.has_value());
		EXPECT_NEAR((*local)[0].y, 1.0f, 1e-6f); // Local X is the cell's a, world +y

		SceneTransformDelta turn;
		turn.spatial.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		ApplySceneTransformSelection(window, snapshot, turn, ModalTransformOp::Rotate,
			TransformPivotMode::IndividualOrigins, glm::vec3(0.0f));
		const glm::vec3 axis = glm::normalize(window.structure.lattice * window.sceneHideVolumes.front().orientation[2]);
		EXPECT_NEAR(axis.y, -1.0f, 1e-5f); // world z turned about world x goes to -y
		EXPECT_EQ(window.sceneHideVolumes.front().center, glm::vec3(0.5f));
	}
} // namespace DefectStudio::Tests
