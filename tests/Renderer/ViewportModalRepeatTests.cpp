#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/DomainLayer.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/ViewportModalCommit.hpp"
#include "Presentation/Panels/ViewportModalTransform.hpp"
#include "Renderer/Commands/RendererCommandRegistration.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStartupConfig RepeatTestConfig()
		{
			RendererStartupConfig config;
			config.loadDefaultScene = false;
			return config;
		}
	}

	class ViewportModalRepeatTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			CrystalStructure structure;
			structure.atoms = {AtomSite{"C", glm::vec3(0), glm::vec3(0), 0},
				AtomSite{"Si", glm::vec3(10, 0, 0), glm::vec3(10, 0, 0), 1},
				AtomSite{"O", glm::vec3(12, 0, 0), glm::vec3(12, 0, 0), 2}};
			record = domain->Workspace().Structures().Add(std::move(structure));
			RendererWindowState window;
			window.windowId = "repeat-test";
			window.camera = CreateUnique<RendererViewCamera>();
			window.structure = BuildRendererStructureData(record->structure, {}, "test", {}, ToString(record->id));
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			renderer->AddWindow(std::move(window));
			renderer->BindUndoStack(stack);
			registry->SetUndoStack(stack);
			RegisterRendererCommands(*registry, CreateRef<EventBus>(), domain, renderer, {}, {});
			RegisterSceneObjectShortcutCommand(*registry, *renderer);
		}

		void TearDown() override { renderer->OnDetach(); }
		RendererWindowState &Window() { return renderer->GetWindows().front(); }
		bool Confirm(const SceneTransformDelta &delta)
		{
			const auto &session = *Window().modalTransform;
			ApplySceneTransformSelection(Window(), Window().modalTransformSelection, delta, session.op,
				Window().transformPivotMode, session.pivot);
			const bool committed = ConfirmViewportModalTransform(Window(), *renderer, registry, delta, "Test transform");
			ResetViewportModalTransform(Window());
			return committed;
		}

		Ref<DomainLayer> domain = CreateRef<DomainLayer>();
		Ref<RendererLayer> renderer = CreateRef<RendererLayer>(RepeatTestConfig());
		Ref<CommandRegistry> registry = CreateRef<CommandRegistry>();
		Ref<UndoStack> stack = CreateRef<UndoStack>();
		Ref<const StructureRecord> record;
	};

	TEST_F(ViewportModalRepeatTests, ConfirmedDuplicateMoveRepeatsCopiesWithOneUndoStepPerPress)
	{
		Window().selectedAtomIndices = {0};
		BeginInPlaceDuplicateMove(Window(), *renderer, registry, glm::vec2(0));
		ASSERT_TRUE(Window().modalTransform);
		Window().modalTransform->constraint = {ConstraintKind::Axis, 0, TransformOrientation::Global};
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(0.25f, 0, 0);
		ASSERT_TRUE(Confirm(delta));
		ASSERT_TRUE(Window().lastConfirmedTransform);
		EXPECT_TRUE(Window().lastConfirmedTransform->duplicated);
		EXPECT_EQ(Window().lastConfirmedTransform->constraint.kind, ConstraintKind::Axis);
		EXPECT_EQ(stack->GetUndoDepth(), 1u);
		ASSERT_TRUE(RepeatViewportModalTransform(Window(), *renderer, registry));
		ASSERT_TRUE(RepeatViewportModalTransform(Window(), *renderer, registry));
		ASSERT_EQ(record->structure.atoms.size(), 6u);
		EXPECT_EQ(record->structure.atoms[3].position, glm::vec3(0.25f, 0, 0));
		EXPECT_EQ(record->structure.atoms[4].position, glm::vec3(0.5f, 0, 0));
		EXPECT_EQ(record->structure.atoms[5].position, glm::vec3(0.75f, 0, 0));
		EXPECT_EQ(stack->GetUndoDepth(), 3u);
		ASSERT_TRUE(stack->Undo());
		ASSERT_EQ(record->structure.atoms.size(), 5u);
		EXPECT_EQ(record->structure.atoms.back().position, glm::vec3(0.5f, 0, 0));
		EXPECT_EQ(Window().selectedAtomIndices, (std::vector<std::size_t>{4}));
		ASSERT_TRUE(stack->Redo());
		ASSERT_EQ(record->structure.atoms.size(), 6u);
		EXPECT_EQ(record->structure.atoms.back().position, glm::vec3(0.75f, 0, 0));
		EXPECT_FALSE(stack->HasActiveGroup());
	}

	TEST_F(ViewportModalRepeatTests, RepeatRotationUsesTheCurrentSelectionPivotAndSavedPolicy)
	{
		Window().selectedAtomIndices = {0};
		BeginViewportModalTransform(Window(), ModalTransformOp::Rotate, glm::vec2(0));
		SceneTransformDelta delta;
		delta.rotationRadians = glm::radians(90.0f);
		delta.spatial.rotation = glm::angleAxis(delta.rotationRadians, glm::vec3(0, 0, 1));
		ASSERT_TRUE(Confirm(delta));
		Window().selectedAtomIndices = {1, 2};
		Window().transformPivotMode = TransformPivotMode::Cursor3D;
		Window().cursor3DPlaced = true;
		Window().cursor3DPosition = glm::vec3(-20, 0, 0);
		ASSERT_TRUE(RepeatViewportModalTransform(Window(), *renderer, registry));
		EXPECT_LT(glm::distance(record->structure.atoms[1].position, glm::vec3(11, -1, 0)), 1e-5f);
		EXPECT_LT(glm::distance(record->structure.atoms[2].position, glm::vec3(11, 1, 0)), 1e-5f);
		EXPECT_EQ(Window().transformPivotMode, TransformPivotMode::Cursor3D);
		EXPECT_EQ(Window().lastConfirmedTransform->pivotMode, TransformPivotMode::Median);
		ASSERT_TRUE(stack->Undo());
		EXPECT_EQ(record->structure.atoms[1].position, glm::vec3(10, 0, 0));
		EXPECT_EQ(record->structure.atoms[2].position, glm::vec3(12, 0, 0));
	}

	TEST_F(ViewportModalRepeatTests, MissingOperationEmptySelectionAndCancelledPreviewPreserveLastConfirmation)
	{
		Window().selectedAtomIndices = {0};
		EXPECT_FALSE(RepeatViewportModalTransform(Window(), *renderer, registry));
		EXPECT_EQ(stack->GetUndoDepth(), 0u);
		BeginViewportModalTransform(Window(), ModalTransformOp::Translate, glm::vec2(0));
		SceneTransformDelta delta;
		delta.spatial.translation = glm::vec3(1, 0, 0);
		ASSERT_TRUE(Confirm(delta));
		BeginViewportModalTransform(Window(), ModalTransformOp::Scale, glm::vec2(0));
		ASSERT_TRUE(Window().modalTransform);
		RestoreSceneTransformSelection(Window(), Window().modalTransformSelection);
		ResetViewportModalTransform(Window());
		ASSERT_TRUE(Window().lastConfirmedTransform);
		EXPECT_EQ(Window().lastConfirmedTransform->op, ModalTransformOp::Translate);
		EXPECT_EQ(Window().lastConfirmedTransform->delta.spatial.translation, delta.spatial.translation);
		Window().selectedAtomIndices.clear();
		EXPECT_FALSE(RepeatViewportModalTransform(Window(), *renderer, registry));
		EXPECT_EQ(stack->GetUndoDepth(), 1u);
	}

	TEST_F(ViewportModalRepeatTests, RegisteredRepeatCommandRequestsTheSameDeferredViewportRoute)
	{
		CommandContext context;
		context.Set("renderer.window_id", Window().windowId);
		ASSERT_TRUE(registry->Execute(CommandID{"renderer.gizmo.repeat_last"}, context));
		EXPECT_TRUE(Window().repeatLastTransformRequested);
		EXPECT_EQ(stack->GetUndoDepth(), 0u);
		Window().repeatLastTransformRequested = false;
		Window().selectedAtomIndices = {0};
		BeginViewportModalTransform(Window(), ModalTransformOp::Translate, glm::vec2(0));
		ASSERT_TRUE(registry->Execute(CommandID{"renderer.gizmo.repeat_last"}, context));
		EXPECT_FALSE(Window().repeatLastTransformRequested);
		ResetViewportModalTransform(Window());
	}
} // namespace DefectStudio::Tests
