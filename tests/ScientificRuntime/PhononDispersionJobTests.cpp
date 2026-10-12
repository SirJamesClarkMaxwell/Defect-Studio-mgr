#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononDispersionIO.hpp"
#include "ScientificRuntime/Python/PhononDispersionJob.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	TEST(PhononDispersionJobTests, InvalidRequestFailsBeforePythonStarts)
	{
		JobContext context({}, {}, {}, {}, [] { return false; });
		PhononDispersionJob job(FindRepoRoot(), {}, true, 1);
		EXPECT_THROW(job.Execute(context), std::runtime_error);
		ASSERT_TRUE(job.GetError());
		EXPECT_EQ(job.GetError()->code, "python.phonon_dispersion.request_invalid");
		EXPECT_FALSE(job.GetResult());
	}

	TEST(PhononDispersionJobTests, CancellationPreventsExecution)
	{
		JobContext context({}, {}, {}, {}, [] { return true; });
		PhononDispersionJob job(FindRepoRoot(), {}, true, 101);
		EXPECT_ANY_THROW(job.Execute(context));
		EXPECT_FALSE(job.GetResult());
		EXPECT_FALSE(job.GetError());
	}

	// Same opt-in-by-fixture convention as PhononAnalysisBridgeTests; never writes into the input tree.
	TEST(PhononDispersionJobTests, ReferenceOutputLoadsThroughIO)
	{
		const Path bulk = FindRepoRoot() / "test-directory" / "sjcm_mgr" / "hBN" / "bulk" / "phonons" / "vasp_661";
		if (!FileSystem::IsDirectory(bulk.Native())) GTEST_SKIP() << "reference bulk FC data not present";
		const Path output = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_dispersion_e2e_" + ToString(GenerateUuid())));
		JobContext context({}, {}, {}, {}, [] { return false; });
		PhononDispersionJob job(bulk, output, true, 5);
		try
		{
			job.Execute(context);
			EXPECT_TRUE(job.GetResult());
			if (!job.GetResult()) throw std::runtime_error("Dispersion job completed without a result");
			PhononDispersion result;
			std::string error;
			EXPECT_TRUE(PhononDispersionIO::Load(*job.GetResult(), result, error)) << error;
			EXPECT_GT(result.bandCount, 0);
			EXPECT_GT(result.distance.size(), 1u);
			EXPECT_TRUE(result.inPlane);
		}
		catch (const std::exception &exception) { ADD_FAILURE() << exception.what(); }
		std::filesystem::remove_all(output.Native());
	}
}
