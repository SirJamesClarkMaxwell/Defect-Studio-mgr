#include <gtest/gtest.h>

#include <limits>

#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	TEST(PhononEmbeddingRequestTests, CutoffsRequireEmbeddingAndPositiveFiniteValues)
	{
		PhononAnalysisRequest request;
		request.forceConstantDirectory = FindRepoRoot();
		request.groundStateDirectory = FindRepoRoot();
		request.excitedStateDirectory = FindRepoRoot();
		request.outputDirectory = FindRepoRoot() / "phonon-never-written";
		request.dcutoffAngstrom = 6.0;
		auto result = PhononAnalysisBridge{}.Run(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.phonon.request_invalid");

		request.bulkForceConstantDirectory = FindRepoRoot();
		request.embeddingMult = std::array<int, 3>{12, 12, 1};
		for (const double cutoff : {0.0, -1.0, std::numeric_limits<double>::infinity(),
			std::numeric_limits<double>::quiet_NaN()})
		{
			request.dcutoffAngstrom = cutoff;
			request.bcutoffAngstrom.reset();
			result = PhononAnalysisBridge{}.Run(request);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.Error().code, "python.phonon.request_invalid");
			request.dcutoffAngstrom.reset();
			request.bcutoffAngstrom = cutoff;
			result = PhononAnalysisBridge{}.Run(request);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.Error().code, "python.phonon.request_invalid");
		}
	}
}
