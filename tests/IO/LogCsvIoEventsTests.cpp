#include <gtest/gtest.h>

#include "App/Events/LogExportEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "IO/IOLayer.hpp"
#include "IO/LogCsvIO.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio::Tests
{
	TEST(LogCsvIoEventsTests, EscapesCsvFields)
	{
		LogEntry entry;
		entry.category = LogCategory::Export;
		entry.level = LogLevel::Warn;
		entry.loggerName = "test,logger";
		entry.message = "quoted \"value\"";

		const std::string csv = LogCsvIO::Serialize(std::span<const LogEntry>(&entry, 1));

		EXPECT_NE(csv.find("\"test,logger\""), std::string::npos);
		EXPECT_NE(csv.find("\"quoted \"\"value\"\"\""), std::string::npos);
	}

	TEST(LogCsvIoEventsTests, IOLayerExportsLogsThroughEventBus)
	{
		Ref<EventBus> eventBus = CreateRef<EventBus>();
		IOLayer ioLayer;
		ioLayer.BindRuntimeServices(eventBus);
		ioLayer.OnAttach();

		Path targetPath = Path::FromResolved(FileSystem::TempDirectoryPath()) /
			Path("defectstudio-log-export-test.csv");

		bool completed = false;
		std::size_t byteCount = 0;
		SubscriptionHandle completedSubscription = eventBus->Subscribe<AppEvents::Logs::ExportCompleted>(
			[&](const AppEvents::Logs::ExportCompleted &event) {
				if (event.targetPath.String() == targetPath.String())
				{
					completed = true;
					byteCount = event.bytes;
				}
			});

		LogEntry entry;
		entry.category = LogCategory::Export;
		entry.level = LogLevel::Info;
		entry.message = "exported";

		AppEvents::Logs::ExportRequested request;
		request.targetPath = targetPath;
		request.entries.push_back(entry);
		eventBus->Queue(request);
		eventBus->ProcessQueue();
		eventBus->ProcessQueue();

		EXPECT_TRUE(completed);
		EXPECT_GT(byteCount, 0u);

		std::string text;
		std::string error;
		ASSERT_TRUE(TextFileIO::Load(targetPath, text, error)) << error;
		EXPECT_NE(text.find("exported"), std::string::npos);

		std::error_code removeError;
		FileSystem::Remove(targetPath.Native(), removeError);
		ioLayer.OnDetach();
	}
} // namespace DefectStudio::Tests
