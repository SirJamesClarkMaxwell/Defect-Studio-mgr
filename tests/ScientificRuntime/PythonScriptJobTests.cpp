#include <gtest/gtest.h>

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PythonScriptJob.hpp"

namespace DefectStudio::Tests
{
	TEST(PythonScriptJobTests, MarksJobAsPythonRuntimeConsumer)
	{
		PythonScriptJob job(
			"smoke",
			Path("scripts/python/examples/script_runner_smoke.py"),
			{},
			Path::FromResolved(FileSystem::CurrentPath()));

		EXPECT_EQ(job.GetType(), "PythonScriptJob");
		EXPECT_TRUE(job.UsesPythonRuntime());
	}
} // namespace DefectStudio::Tests
