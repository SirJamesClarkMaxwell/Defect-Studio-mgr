#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "App/ApplicationInternals.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ApplicationSpecification Parse(std::vector<const char *> arguments)
		{
			arguments.insert(arguments.begin(), "DefectStudio.exe");
			return ApplicationDetail::ParseApplicationArguments(
				static_cast<int>(arguments.size()), const_cast<char **>(arguments.data()));
		}
	} // namespace

	// Spec criterion 5: `--project=<directory>` names the project to open at startup.
	TEST(ApplicationArgumentsTests, ProjectArgumentSetsTheStartupProjectDirectory)
	{
		const ApplicationSpecification specification = Parse({"--project=D:/thesis/bevel-gallery"});

		ASSERT_TRUE(specification.startupProjectDirectory.has_value());
		EXPECT_EQ(*specification.startupProjectDirectory, Path::FromResolved(std::string("D:/thesis/bevel-gallery")));
	}

	TEST(ApplicationArgumentsTests, NoProjectArgumentLeavesTheRecentsRuleInCharge)
	{
		EXPECT_FALSE(Parse({}).startupProjectDirectory.has_value());
		EXPECT_FALSE(Parse({"--reset-layout"}).startupProjectDirectory.has_value());
	}

	// An empty value is not a directory; it must not turn into "open the working directory".
	TEST(ApplicationArgumentsTests, EmptyProjectArgumentIsIgnored)
	{
		EXPECT_FALSE(Parse({"--project="}).startupProjectDirectory.has_value());
	}

	TEST(ApplicationArgumentsTests, ProjectArgumentCombinesWithOtherFlags)
	{
		const ApplicationSpecification specification = Parse({"--reset-layout", "--project=C:/p", "--trace-events"});

		EXPECT_TRUE(specification.resetLayout);
		EXPECT_TRUE(specification.traceEvents);
		ASSERT_TRUE(specification.startupProjectDirectory.has_value());
	}
} // namespace DefectStudio::Tests
