#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/KeyBindingEvents.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "IO/IOLayer.hpp"

namespace
{
	[[nodiscard]] DefectStudio::Path CreateTempDirectory()
	{
		const auto stamp = DefectStudio::Time::Now().time_since_epoch().count();
		const DefectStudio::Path directory =
			DefectStudio::Path::FromResolved(FileSystem::TempDirectoryPath()) /
			("DefectStudioKeyBindingIoTests_" + std::to_string(stamp));
		FileSystem::CreateDirectories(directory.Native());
		return directory;
	}

	void RemoveTempDirectory(const DefectStudio::Path &path)
	{
		std::error_code ignored;
		FileSystem::RemoveAll(path.Native(), ignored);
	}

	[[nodiscard]] DefectStudio::Path FindRepoRoot()
	{
		DefectStudio::Path cursor = DefectStudio::Path::FromResolved(FileSystem::CurrentPath());
		for (int depth = 0; depth < 10; ++depth)
		{
			if (FileSystem::Exists((cursor / "pyproject.toml").Native()))
				return cursor;

			const DefectStudio::Path parent = cursor.parent_path();
			if (parent.Empty() || parent == cursor)
				break;
			cursor = parent;
		}

		return DefectStudio::Path::FromResolved(FileSystem::CurrentPath());
	}
} // namespace

namespace DefectStudio::Tests
{
	TEST(KeyBindingIoEventsTests, RendererBindingRoundTripsThroughYamlEvents)
	{
		auto eventBus = CreateRef<EventBus>();
		IOLayer ioLayer;
		ioLayer.BindRuntimeServices(eventBus);

		const Path tempDirectory = CreateTempDirectory();
		const Path keybindingsPath = tempDirectory / Path("keybindings.yaml");
		bool saved = false;
		std::vector<KeyBinding> loadedBindings;
		std::string failure;

		auto savedSubscription = eventBus->Subscribe<AppEvents::Keymap::BindingsSaved>(
			[&saved](const AppEvents::Keymap::BindingsSaved &) {
				saved = true;
			});
		auto loadedSubscription = eventBus->Subscribe<AppEvents::Keymap::BindingsLoaded>(
			[&loadedBindings](const AppEvents::Keymap::BindingsLoaded &event) {
				loadedBindings = event.bindings;
			});
		auto saveFailedSubscription = eventBus->Subscribe<AppEvents::Keymap::BindingsSaveFailed>(
			[&failure](const AppEvents::Keymap::BindingsSaveFailed &event) {
				failure = event.error;
			});
		auto loadFailedSubscription = eventBus->Subscribe<AppEvents::Keymap::BindingsLoadFailed>(
			[&failure](const AppEvents::Keymap::BindingsLoadFailed &event) {
				failure = event.error;
			});

		const KeyChord chord{KeyCode::A, KeyModifiers::Ctrl};
		std::vector<KeyBinding> bindings;
		bindings.push_back(KeyBinding{
			"renderer.align_axis_a",
			chord,
			CommandID("renderer.align_axis_a"),
			ContextExpr("renderer.viewport.focused"),
			KeymapLayer::WindowLocal,
			false});

		AppEvents::Keymap::BindingsSaveRequested saveRequested{bindings, keybindingsPath};
		eventBus->Publish(saveRequested);
		eventBus->ProcessQueue();
		ASSERT_TRUE(failure.empty()) << failure;
		ASSERT_TRUE(saved);

		AppEvents::Keymap::BindingsLoadRequested loadRequested{keybindingsPath};
		eventBus->Publish(loadRequested);
		eventBus->ProcessQueue();
		ASSERT_TRUE(failure.empty()) << failure;
		ASSERT_EQ(loadedBindings.size(), 1u);

		const KeyBinding &loaded = loadedBindings.front();
		EXPECT_EQ(loaded.id, "renderer.align_axis_a");
		EXPECT_EQ(loaded.commandId.value, "renderer.align_axis_a");
		EXPECT_EQ(ToString(loaded.chord), "Ctrl+A");
		EXPECT_EQ(loaded.when.GetExpression(), "renderer.viewport.focused");
		EXPECT_EQ(loaded.layer, KeymapLayer::WindowLocal);
		EXPECT_FALSE(loaded.enabled);

		RemoveTempDirectory(tempDirectory);
	}

	TEST(KeyBindingIoEventsTests, DefaultSceneShortcutBindingsParseWithoutChordCollisions)
	{
		auto eventBus = CreateRef<EventBus>();
		IOLayer ioLayer;
		ioLayer.BindRuntimeServices(eventBus);

		std::vector<KeyBinding> loadedBindings;
		std::string failure;
		auto loadedSubscription = eventBus->Subscribe<AppEvents::Keymap::BindingsLoaded>(
			[&loadedBindings](const AppEvents::Keymap::BindingsLoaded &event) {
				loadedBindings = event.bindings;
			});
		auto loadFailedSubscription = eventBus->Subscribe<AppEvents::Keymap::BindingsLoadFailed>(
			[&failure](const AppEvents::Keymap::BindingsLoadFailed &event) {
				failure = event.error;
			});

		const Path keybindingsPath =
			FindRepoRoot() / "install" / "users" / "default" / "config" / "keybindings.yaml";
		AppEvents::Keymap::BindingsLoadRequested loadRequested{keybindingsPath};
		eventBus->Publish(loadRequested);
		eventBus->ProcessQueue();
		ASSERT_TRUE(failure.empty()) << failure;

		KeymapResolver resolver;
		for (const KeyBinding &binding : loadedBindings)
			ASSERT_TRUE(resolver.RegisterBinding(binding)) << binding.id;

		const auto findBinding = [&loadedBindings](const std::string &id) {
			return std::find_if(
				loadedBindings.begin(), loadedBindings.end(),
				[&id](const KeyBinding &binding) { return binding.id == id; });
		};
		const auto outlinerBinding = findBinding("editor.focus_scene_outliner");
		const auto reverseBinding = findBinding("renderer.scene_arrow.reverse");
		ASSERT_NE(outlinerBinding, loadedBindings.end());
		ASSERT_NE(reverseBinding, loadedBindings.end());
		EXPECT_EQ(ToString(outlinerBinding->chord), "Ctrl+Shift+O");
		EXPECT_EQ(ToString(reverseBinding->chord), "Alt+R");

		const auto chordCount = [&loadedBindings](const KeyChord &chord) {
			return std::count_if(
				loadedBindings.begin(), loadedBindings.end(),
				[&chord](const KeyBinding &binding) { return binding.chord == chord; });
		};
		EXPECT_EQ(chordCount(outlinerBinding->chord), 1);
		EXPECT_EQ(chordCount(reverseBinding->chord), 1);
		EXPECT_TRUE(resolver.GetConflicts().empty());
	}
} // namespace DefectStudio::Tests
