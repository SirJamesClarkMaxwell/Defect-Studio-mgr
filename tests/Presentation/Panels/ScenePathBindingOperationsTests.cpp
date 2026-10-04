#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <variant>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Panels/ScenePathBindingOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathSystem.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Nodes at x = -1, 1, 3 joined by Lines; node 0 and node 2 are the endpoints.
		ScenePath ThreeNodePath()
		{
			ScenePath path;
			path.id = SceneObjectId{1};
			for (const float x : {-1.0f, 1.0f, 3.0f})
				path.nodes.push_back(PathNode{AllocateElementId(path), glm::vec3(x, 0.0f, 0.0f), {}});
			for (int segment = 0; segment < 2; ++segment)
				path.segments.push_back(PathSegment{AllocateElementId(path), LineSegmentData{}});
			return path;
		}

		RendererStartupConfig EmptyRendererConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}

		RendererAtomData Atom(const glm::vec3 position)
		{
			RendererAtomData atom;
			atom.element = "C";
			atom.cartesianPosition = position;
			return atom;
		}

		bool NearlyEqual(const glm::vec3 &a, const glm::vec3 &b)
		{
			return glm::length(a - b) < 1e-4f;
		}
	} // namespace

	class ScenePathBindingOperationsTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undoStack);
		}

		void TearDown() override
		{
			renderer.OnDetach();
		}

		// Two atoms; the path is open in Edit Mode with node `activeNode` selected and active.
		RendererWindowState &MakeWindow(std::size_t activeNode, ScenePath path = ThreeNodePath())
		{
			RendererWindowState window;
			window.windowId = "path-binding";
			window.structure.atoms = {Atom({4.0f, 5.0f, 6.0f}), Atom({8.0f, 5.0f, 6.0f})};
			pathId = SceneSystem::AppendScenePath(window, std::move(path));
			window.selectedScenePaths = {pathId};
			window.pathEdit.Enter(pathId);
			const ScenePath *stored = window.paths->Store().Find(pathId);
			window.pathEdit.SetSelection({stored->nodes[activeNode].id});
			renderer.AddWindow(std::move(window));
			return renderer.GetWindows().front();
		}

		[[nodiscard]] const ScenePath &Stored(const RendererWindowState &window) const
		{
			return *window.paths->Store().Find(pathId);
		}

		Ref<UndoStack> undoStack = CreateRef<UndoStack>();
		RendererLayer renderer{EmptyRendererConfig()};
		SceneObjectId pathId;
	};

	TEST_F(ScenePathBindingOperationsTests, OneSelectedAtomBindsCopyPositionInOneUndo)
	{
		RendererWindowState &window = MakeWindow(0);
		window.selectedAtomIndices = {1};

		ASSERT_TRUE(BindActiveScenePathNodeToSelectedAtoms(window));

		const auto *binding = std::get_if<PathBinding::CopyPosition>(&Stored(window).nodes[0].binding.value);
		ASSERT_NE(binding, nullptr);
		EXPECT_EQ(binding->atomIndex, 1u);
		EXPECT_EQ(binding->offset, glm::vec3(0.0f));
		EXPECT_EQ(binding->buffer, 0.0f);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(ScenePathBindingOperationsTests, TwoSelectedAtomsBindBondMidpointInSelectionOrder)
	{
		RendererWindowState &window = MakeWindow(1);
		window.selectedAtomIndices = {1, 0};

		ASSERT_TRUE(BindActiveScenePathNodeToSelectedAtoms(window));

		const auto *binding = std::get_if<PathBinding::BondMidpoint>(&Stored(window).nodes[1].binding.value);
		ASSERT_NE(binding, nullptr);
		EXPECT_EQ(binding->atomA, 1u);
		EXPECT_EQ(binding->atomB, 0u);
	}

	TEST_F(ScenePathBindingOperationsTests, WrongSelectedAtomCountIsRejectedAndChangesNothing)
	{
		RendererWindowState &window = MakeWindow(0);
		for (const std::vector<std::size_t> &atoms : {std::vector<std::size_t>{}, std::vector<std::size_t>{0, 1, 0}})
		{
			window.selectedAtomIndices = atoms;
			const Result<void> result = BindActiveScenePathNodeToSelectedAtoms(window);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.Error().code, "path.binding_atoms_required");
		}
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(Stored(window).nodes[0].binding.value));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathBindingOperationsTests, SelectedAtomMissingFromTheStructureIsRejected)
	{
		RendererWindowState &window = MakeWindow(0);
		window.selectedAtomIndices = {5};

		const Result<void> result = BindActiveScenePathNodeToSelectedAtoms(window);

		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "path.binding_atom_out_of_range");
	}

	TEST_F(ScenePathBindingOperationsTests, NoActiveNodeIsRejected)
	{
		RendererWindowState &window = MakeWindow(0);
		window.pathEdit.ClearSelection();
		window.selectedAtomIndices = {0};

		const Result<void> result = BindActiveScenePathNodeToSelectedAtoms(window);

		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "path.edit_node_required");
		EXPECT_FALSE(ResolveActiveScenePathNodeBinding(window));
	}

	TEST_F(ScenePathBindingOperationsTests, BufferIsAcceptedOnAnEndpoint)
	{
		RendererWindowState &window = MakeWindow(2);

		ASSERT_TRUE(SetActiveScenePathNodeBinding(
			window, PathBinding{PathBinding::CopyPosition{0, glm::vec3(0.0f), 0.3f}}));

		const Result<PathBinding> stored = ResolveActiveScenePathNodeBinding(window);
		ASSERT_TRUE(stored);
		ASSERT_TRUE(std::holds_alternative<PathBinding::CopyPosition>(stored.Value().value));
		EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(stored.Value().value).buffer, 0.3f);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
	}

	TEST_F(ScenePathBindingOperationsTests, BufferOnAnInteriorNodeIsRejectedAndChangesNothing)
	{
		RendererWindowState &window = MakeWindow(1);

		const Result<void> result = SetActiveScenePathNodeBinding(
			window, PathBinding{PathBinding::CopyPosition{0, glm::vec3(0.0f), 0.3f}});

		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "path.binding_buffer_endpoint_only");
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(Stored(window).nodes[1].binding.value));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathBindingOperationsTests, SelectedObjectOriginBindsAndDetachesWithOneUndoPerCall)
	{
		RendererWindowState &window = MakeWindow(0);
		RendererWindowState::FreeLabel label;
		label.worldPosition = {7, 8, 9};
		window.freeLabels.push_back(label);
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		const SceneObjectId labelId = window.freeLabels[0].id;
		window.selectedFreeLabels = {labelId};
		ASSERT_TRUE(ResolveSelectedScenePathBindingObject(window));
		ASSERT_TRUE(BindActiveScenePathNodeToSelectedObjectOrigin(window));
		ASSERT_TRUE(std::holds_alternative<PathBinding::ObjectOrigin>(Stored(window).nodes[0].binding.value));
		EXPECT_EQ(std::get<PathBinding::ObjectOrigin>(Stored(window).nodes[0].binding.value).object, labelId);
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
		ASSERT_TRUE(DetachActiveScenePathNodeKeepingPosition(window));
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(Stored(window).nodes[0].binding.value));
		EXPECT_EQ(Stored(window).nodes[0].position, label.worldPosition);
		EXPECT_EQ(undoStack->GetUndoDepth(), 2u);
		ASSERT_TRUE(undoStack->Undo());
		ASSERT_TRUE(std::holds_alternative<PathBinding::ObjectOrigin>(Stored(window).nodes[0].binding.value));
		EXPECT_EQ(std::get<PathBinding::ObjectOrigin>(Stored(window).nodes[0].binding.value).object, labelId);
		ASSERT_TRUE(undoStack->Undo());
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(Stored(window).nodes[0].binding.value));
		EXPECT_EQ(Stored(window).nodes[0].position, glm::vec3(-1, 0, 0));
		ASSERT_TRUE(undoStack->Redo());
		EXPECT_TRUE(std::holds_alternative<PathBinding::ObjectOrigin>(Stored(window).nodes[0].binding.value));
		ASSERT_TRUE(undoStack->Redo());
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(Stored(window).nodes[0].binding.value));
		EXPECT_EQ(Stored(window).nodes[0].position, label.worldPosition);
	}

	TEST_F(ScenePathBindingOperationsTests, WrongSelectedObjectCountIsRejectedWithoutUndo)
	{
		RendererWindowState &window = MakeWindow(0);
		EXPECT_FALSE(BindActiveScenePathNodeToSelectedObjectOrigin(window));
		window.freeLabels.resize(2);
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		window.selectedFreeLabels = {window.freeLabels[0].id, window.freeLabels[1].id};
		EXPECT_FALSE(ResolveSelectedScenePathBindingObject(window));
		EXPECT_FALSE(BindActiveScenePathNodeToSelectedObjectOrigin(window));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathBindingOperationsTests, SelectedPlaneOriginBindsAndFollowsWithoutAnEntityMirror)
	{
		RendererWindowState &window = MakeWindow(0);
		RendererWindowState::ScenePlane plane;
		plane.id = window.sceneRegistry.AllocateObjectId();
		plane.center = {7, 8, 9};
		window.scenePlanes.push_back(plane);
		window.selectedScenePlanes = {plane.id};
		ASSERT_TRUE(BindActiveScenePathNodeToSelectedObjectOrigin(window));
		EXPECT_EQ(undoStack->GetUndoDepth(), 1u);
		EXPECT_EQ(ResolveNodePositions(Stored(window), SceneSystem::MakePathBindingContext(window)).positions[0], plane.center);
		window.scenePlanes[0].center += glm::vec3(1, 0, 0);
		EXPECT_EQ(ResolveNodePositions(Stored(window), SceneSystem::MakePathBindingContext(window)).positions[0], window.scenePlanes[0].center);
	}

	TEST_F(ScenePathBindingOperationsTests, MissingObjectOriginIsRejectedWithoutUndo)
	{
		RendererWindowState &window = MakeWindow(0);

		const Result<void> result = SetActiveScenePathNodeBinding(
			window, PathBinding{PathBinding::ObjectOrigin{SceneObjectId{42}, glm::vec3(0.0f)}});

		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "path.binding_object_unresolved");
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathBindingOperationsTests, PathObjectOriginIsRejectedWithoutUndo)
	{
		RendererWindowState &window = MakeWindow(0);
		const Result<void> result = SetActiveScenePathNodeBinding(
			window, PathBinding{PathBinding::ObjectOrigin{pathId}});
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "path.object_origin_targets_path");
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	TEST_F(ScenePathBindingOperationsTests, DetachWritesTheResolvedPositionBackSoTheNodeDoesNotJump)
	{
		RendererWindowState &window = MakeWindow(0);
		ASSERT_TRUE(SetActiveScenePathNodeBinding(
			window, PathBinding{PathBinding::CopyPosition{0, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f}}));
		const std::size_t depthBefore = undoStack->GetUndoDepth();

		ASSERT_TRUE(DetachActiveScenePathNodeKeepingPosition(window));

		const PathNode &node = Stored(window).nodes[0];
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(node.binding.value));
		EXPECT_TRUE(NearlyEqual(node.position, glm::vec3(4.0f, 6.0f, 6.0f)));
		EXPECT_EQ(undoStack->GetUndoDepth(), depthBefore + 1);
	}

	TEST_F(ScenePathBindingOperationsTests, DetachUnderAnObjectTransformStoresTheLocalPosition)
	{
		ScenePath path = ThreeNodePath();
		path.transform.position = glm::vec3(10.0f, 0.0f, 0.0f);
		path.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		path.transform.scale = glm::vec3(2.0f);
		path.nodes[0].binding.value = PathBinding::CopyPosition{0, glm::vec3(0.0f), 0.0f};
		RendererWindowState &window = MakeWindow(0, std::move(path));
		const glm::vec3 worldBefore =
			ResolveNodePositions(Stored(window), SceneSystem::MakePathBindingContext(window)).positions[0];
		ASSERT_TRUE(NearlyEqual(worldBefore, glm::vec3(4.0f, 5.0f, 6.0f)));

		ASSERT_TRUE(DetachActiveScenePathNodeKeepingPosition(window));

		const glm::vec3 worldAfter = ResolveNodePositions(Stored(window), BindingContext{}).positions[0];
		EXPECT_TRUE(NearlyEqual(worldAfter, worldBefore));
	}

	TEST_F(ScenePathBindingOperationsTests, DetachOnAFreeNodeIsANoOpWithoutUndo)
	{
		RendererWindowState &window = MakeWindow(0);

		ASSERT_TRUE(DetachActiveScenePathNodeKeepingPosition(window));

		EXPECT_EQ(Stored(window).nodes[0].position, glm::vec3(-1.0f, 0.0f, 0.0f));
		EXPECT_EQ(undoStack->GetUndoDepth(), 0u);
	}

	// G on a bound node moves its binding OFFSET: the node follows the cursor and stays bound. Moving
	// the authored position instead would change nothing on screen - the binding wins.
	TEST_F(ScenePathBindingOperationsTests, TranslatingABoundNodeMovesItsOffsetAndRestoreUndoesIt)
	{
		RendererWindowState &window = MakeWindow(0);
		window.selectedAtomIndices = {0};
		ASSERT_TRUE(BindActiveScenePathNodeToSelectedAtoms(window));
		window.selectedAtomIndices.clear();
		const glm::vec3 authoredBefore = Stored(window).nodes[0].position;

		const SceneTransformSelectionSnapshot snapshot =
			CaptureSceneTransformSelectionForOperation(window, ModalTransformOp::Translate);
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(1.0f, 0.0f, 0.0f);
		ApplySceneTransformSelection(
			window, snapshot, delta, ModalTransformOp::Translate, TransformPivotMode::Median, glm::vec3(0.0f));

		const PathNode &moved = Stored(window).nodes[0];
		const auto *binding = std::get_if<PathBinding::CopyPosition>(&moved.binding.value);
		ASSERT_NE(binding, nullptr);
		EXPECT_TRUE(NearlyEqual(binding->offset, glm::vec3(1.0f, 0.0f, 0.0f)));
		EXPECT_EQ(moved.position, authoredBefore);
		EXPECT_TRUE(NearlyEqual(
			ResolveNodePositions(Stored(window), SceneSystem::MakePathBindingContext(window)).positions[0],
			glm::vec3(5.0f, 5.0f, 6.0f)));

		RestoreSceneTransformSelection(window, snapshot);
		const auto *restored = std::get_if<PathBinding::CopyPosition>(&Stored(window).nodes[0].binding.value);
		ASSERT_NE(restored, nullptr);
		EXPECT_EQ(restored->offset, glm::vec3(0.0f));
	}
} // namespace DefectStudio::Tests
