#include "Core/dspch.hpp"

#include <functional>
#include <gtest/gtest.h>

#include "Core/CoreLayer.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/Utils/Input.hpp"
#include "Presentation/Panels/SceneHideVolumeEditor.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		class CountAtomEdit final : public ICommand
		{
		public:
			explicit CountAtomEdit(int &calls) : m_Calls(calls) {}
			Result<void> Execute(CommandContext &) override { ++m_Calls.get(); return {}; }
			std::string Description() const override { return "Atom edit fallback"; }
		private:
			std::reference_wrapper<int> m_Calls;
		};
	}

	class SceneObjectShortcutTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			ASSERT_TRUE(core.InitializeSystems(CreateRef<EventBus>(), JobsConfig{1, false}));
			renderer.BindUndoStack(core.GetUndoStackHandle());
			RendererWindowState window;
			window.windowId = "scene-shortcuts";
			renderer.AddWindow(std::move(window));
			const auto registry = core.GetCommandRegistryHandle().lock();
			ASSERT_TRUE(registry);
			RegisterSceneObjectShortcutCommand(*registry, renderer);
			const char *actions[] = {"copy", "duplicate", "paste", "delete"};
			const KeyCode keys[] = {KeyCode::C, KeyCode::D, KeyCode::V, KeyCode::Delete};
			for (int index = 0; index < 4; ++index)
			{
				const std::string id = std::string("renderer.selection.") + actions[index];
				ASSERT_TRUE(registry->Register({CommandID{id}, id, "Renderer", id, {}, CommandFlags::None},
					[calls = std::ref(atomCalls)](CommandContext &) -> Unique<ICommand> {
						return CreateUnique<CountAtomEdit>(calls.get());
					}));
				ASSERT_TRUE(core.GetKeymapResolver().RegisterBinding({id,
					{keys[index], index == 3 ? KeyModifiers::None : KeyModifiers::Ctrl}, CommandID{id}, {},
					KeymapLayer::Global}));
			}
			Input::SetBackend({[this](KeyCode key) { return ctrlHeld && key == KeyCode::LeftControl; }, {}, {}});
		}
		void TearDown() override
		{
			Input::ResetBackend();
			RendererWindowState atomSelection;
			atomSelection.selectedAtomIndices = {0};
			(void)HandleSceneObjectEditShortcut(atomSelection, SceneObjectEditAction::Copy);
			GetSceneHideVolumeClipboard().clear();
			GetScenePinnedMeasurementClipboard().clear();
			renderer.OnDetach();
			core.ShutdownSystems();
		}
		RendererWindowState &Window() { return renderer.GetWindows().front(); }
		void Press(KeyCode key)
		{
			ctrlHeld = key != KeyCode::Delete;
			KeyPressedEvent event(static_cast<int>(key));
			core.OnEvent(event);
			EXPECT_TRUE(event.handled);
		}
		CoreLayer core;
		RendererLayer renderer{[] { RendererStartupConfig config; config.loadDefaultScene = false; return config; }()};
		int atomCalls = 0;
		bool ctrlHeld = false;
	};

	TEST_F(SceneObjectShortcutTests, HideVolumeCopyDuplicateDeleteAndPasteNeverExecuteTheAtomCommand)
	{
		AddSceneHideVolume(Window(), HideVolumeKind::Sphere);
		core.GetUndoStack().Clear();
		const auto original = Window().sceneHideVolumes.front().id;
		Press(KeyCode::C);
		Press(KeyCode::D);
		ASSERT_EQ(Window().sceneHideVolumes.size(), 2u);
		EXPECT_NE(Window().sceneHideVolumes.back().id, original);
		Press(KeyCode::Delete);
		ASSERT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_EQ(Window().sceneHideVolumes.front().id, original);
		EXPECT_TRUE(Window().selectedSceneHideVolumes.empty());
		Press(KeyCode::V); // No selection: the most recently copied scene clipboard owns Paste.
		ASSERT_EQ(Window().sceneHideVolumes.size(), 2u);
		EXPECT_NE(Window().sceneHideVolumes.back().id, original);
		EXPECT_EQ(atomCalls, 0);
		EXPECT_EQ(core.GetUndoStack().GetUndoDepth(), 3u);
		ASSERT_TRUE(core.GetUndoStack().Undo());
		EXPECT_EQ(Window().sceneHideVolumes.size(), 1u);
	}

	TEST_F(SceneObjectShortcutTests, EmptySceneClipboardIsConsumedWhileAVolumeIsSelected)
	{
		GetSceneHideVolumeClipboard().clear();
		AddSceneHideVolume(Window(), HideVolumeKind::Sphere);
		Press(KeyCode::V);
		EXPECT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_EQ(atomCalls, 0);
	}

	TEST_F(SceneObjectShortcutTests, NonrepeatableSceneEditsIgnoreOsKeyRepeat)
	{
		AddSceneHideVolume(Window(), HideVolumeKind::Sphere);
		ctrlHeld = true;
		KeyRepeatedEvent event(static_cast<int>(KeyCode::D));
		core.OnEvent(event);
		EXPECT_FALSE(event.handled);
		EXPECT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_EQ(atomCalls, 0);
	}

	TEST_F(SceneObjectShortcutTests, AtomAndBondSelectionKeepTheOriginalCommandsAndAtomCopySupersedesSceneCopy)
	{
		AddSceneHideVolume(Window(), HideVolumeKind::Sphere);
		Press(KeyCode::C);
		Window().selectedAtomIndices = {0};
		for (const auto key : {KeyCode::C, KeyCode::D, KeyCode::V, KeyCode::Delete}) Press(key);
		EXPECT_EQ(atomCalls, 4);
		EXPECT_EQ(Window().sceneHideVolumes.size(), 1u);
		Window().selectedAtomIndices.clear();
		Window().selectedSceneHideVolumes.clear();
		Press(KeyCode::V);
		EXPECT_EQ(atomCalls, 5);
		Window().selectedBondIndices = {0};
		Press(KeyCode::Delete);
		EXPECT_EQ(atomCalls, 6);
	}

	TEST_F(SceneObjectShortcutTests, PinnedMeasurementUsesItsSharedCopyDuplicatePasteAndDeleteActions)
	{
		RendererWindowState::PinnedMeasurement pin;
		pin.id = Window().sceneRegistry.AllocateObjectId();
		Window().structure.atoms = {{"C", {0, 0, 0}}, {"C", {1, 0, 0}}};
		pin.atomIndices = {0, 1};
		Window().pinnedMeasurements = {pin};
		Window().selectedPinnedMeasurements = {pin.id};
		Press(KeyCode::C);
		ASSERT_EQ(GetScenePinnedMeasurementClipboard().size(), 1u);
		Press(KeyCode::D);
		ASSERT_EQ(Window().pinnedMeasurements.size(), 2u);
		Press(KeyCode::Delete);
		ASSERT_EQ(Window().pinnedMeasurements.size(), 1u);
		Press(KeyCode::V);
		EXPECT_EQ(Window().pinnedMeasurements.size(), 2u);
		EXPECT_EQ(atomCalls, 0);
	}
}
