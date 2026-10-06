#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/KeyBindingEvents.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeyBinding.hpp"
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
		EXPECT_EQ(reverseBinding->commandId.value, "renderer.scene_path.reverse");

		const auto chordCount = [&loadedBindings](const KeyChord &chord) {
			return std::count_if(
				loadedBindings.begin(), loadedBindings.end(),
				[&chord](const KeyBinding &binding) { return binding.chord == chord; });
		};
		EXPECT_EQ(chordCount(outlinerBinding->chord), 1);
		EXPECT_EQ(chordCount(reverseBinding->chord), 1);
		EXPECT_TRUE(resolver.GetConflicts().empty());
	}

	// Path Edit Mode shares E, V, Delete and 1/2/3 with viewport-wide commands. The shipped keymap
	// must hand those keys to Edit Mode exactly while its context is active, and leave them alone
	// otherwise - one key press must never fire both.
	TEST(KeyBindingIoEventsTests, PathEditBindingsWinOnlyWhileEditModeIsActive)
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
		AppEvents::Keymap::BindingsLoadRequested loadRequested{
			FindRepoRoot() / "install" / "users" / "default" / "config" / "keybindings.yaml"};
		eventBus->Publish(loadRequested);
		eventBus->ProcessQueue();
		ASSERT_TRUE(failure.empty()) << failure;

		KeymapResolver resolver;
		for (const KeyBinding &binding : loadedBindings)
			ASSERT_TRUE(resolver.RegisterBinding(binding)) << binding.id;

		ContextManager contexts;
		contexts.SetActive("renderer.viewport.focused", true);
		const auto resolved = [&](const char *chordText) -> std::string {
			const std::optional<KeyChord> chord = ParseKeyChord(chordText);
			EXPECT_TRUE(chord.has_value()) << chordText;
			if (!chord)
				return {};
			const std::optional<KeyBinding> binding = resolver.Resolve(*chord, contexts);
			return binding ? binding->commandId.value : std::string{};
		};

		struct Expectation
		{
			const char *chord;
			const char *inEditMode;
			const char *outsideEditMode;
		};
		const Expectation expectations[] = {
			{"Tab", "renderer.path_edit.toggle", "renderer.path_edit.toggle"},
			{"Escape", "renderer.path_edit.leave", ""},
			{"1", "renderer.path_edit.mode_nodes", "renderer.align_axis_a"},
			{"2", "renderer.path_edit.mode_segments", "renderer.align_axis_b"},
			{"3", "renderer.path_edit.mode_whole", "renderer.align_axis_c"},
			{"E", "renderer.path_edit.extend", "renderer.roll_right"},
			{"V", "renderer.path_edit.handle_type_menu", "renderer.view.cycle_next"},
			{"Delete", "renderer.path_edit.delete_nodes", "renderer.selection.delete"},
		};

		contexts.SetActive("renderer.path_edit.active", true);
		for (const Expectation &expectation : expectations)
			EXPECT_EQ(resolved(expectation.chord), expectation.inEditMode) << expectation.chord;

		contexts.SetActive("renderer.path_edit.active", false);
		for (const Expectation &expectation : expectations)
			EXPECT_EQ(resolved(expectation.chord), expectation.outsideEditMode) << expectation.chord;
	}
} // namespace DefectStudio::Tests
