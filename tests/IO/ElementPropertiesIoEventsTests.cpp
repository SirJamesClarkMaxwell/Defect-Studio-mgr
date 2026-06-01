#include <gtest/gtest.h>

#include <string>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementPropertiesEvents.hpp"
#include "IO/IOLayer.hpp"

namespace
{
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
	TEST(ElementPropertiesIoEventsTests, LoadsElementPropertiesThroughIOLayerEvent)
	{
		const Path sourcePath = FindRepoRoot() / "install" / "app" / "data" / "elements" / "element_properties.yaml";

		Ref<EventBus> bus = CreateRef<EventBus>();
		IOLayer layer;
		layer.BindRuntimeServices(bus);

		bool loaded = false;
		bool failed = false;
		Path loadedPath;
		std::size_t loadedEntries = 0;
		ElementProperties silicon;

		const SubscriptionHandle loadedSubscription = bus->Subscribe<DomainEvents::Crystal::ElementPropertiesLoaded>(
			[&](const DomainEvents::Crystal::ElementPropertiesLoaded &event) {
				loaded = true;
				loadedPath = event.sourcePath;
				loadedEntries = event.entries.size();
				const auto found = event.entries.find("Si");
				if (found != event.entries.end())
					silicon = found->second;
			});
		const SubscriptionHandle failedSubscription = bus->Subscribe<DomainEvents::Crystal::ElementPropertiesLoadFailed>(
			[&](const DomainEvents::Crystal::ElementPropertiesLoadFailed &) {
				failed = true;
			});

		(void)loadedSubscription;
		(void)failedSubscription;

		bus->Publish(DomainEvents::Crystal::ElementPropertiesLoadRequested{sourcePath});
		bus->ProcessQueue();

		EXPECT_TRUE(loaded);
		EXPECT_FALSE(failed);
		EXPECT_EQ(loadedPath.String(), sourcePath.String());
		EXPECT_GT(loadedEntries, 20u);
		EXPECT_EQ(silicon.atomicNumber, 14);
		EXPECT_NEAR(silicon.covalentRadius, 1.11f, 1e-3f);
	}

	TEST(ElementPropertiesIoEventsTests, EmitsLoadFailedEventForMissingFile)
	{
		const Path missingPath = FindRepoRoot() / "install" / "app" / "data" / "elements" / "missing-element-properties.yaml";

		Ref<EventBus> bus = CreateRef<EventBus>();
		IOLayer layer;
		layer.BindRuntimeServices(bus);

		bool loaded = false;
		bool failed = false;
		Path failedPath;
		std::string error;

		const SubscriptionHandle loadedSubscription = bus->Subscribe<DomainEvents::Crystal::ElementPropertiesLoaded>(
			[&](const DomainEvents::Crystal::ElementPropertiesLoaded &) {
				loaded = true;
			});
		const SubscriptionHandle failedSubscription = bus->Subscribe<DomainEvents::Crystal::ElementPropertiesLoadFailed>(
			[&](const DomainEvents::Crystal::ElementPropertiesLoadFailed &event) {
				failed = true;
				failedPath = event.sourcePath;
				error = event.error;
			});

		(void)loadedSubscription;
		(void)failedSubscription;

		bus->Publish(DomainEvents::Crystal::ElementPropertiesLoadRequested{missingPath});
		bus->ProcessQueue();

		EXPECT_FALSE(loaded);
		EXPECT_TRUE(failed);
		EXPECT_EQ(failedPath.String(), missingPath.String());
		EXPECT_FALSE(error.empty());
	}
} // namespace DefectStudio::Tests
