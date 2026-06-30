#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/KeyBindingEvents.hpp"
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
} // namespace DefectStudio::Tests
