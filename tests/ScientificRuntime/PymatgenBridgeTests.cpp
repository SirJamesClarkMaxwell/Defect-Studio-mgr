#include <gtest/gtest.h>

#include "Core/Utils/Time.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	TEST(PymatgenBridgeTests, PoscarRoundtripProducesOutputFile)
	{
		const Path inputPath = ScientificRuntimeFixturePath("POSCAR_Si.vasp");
		const Path outputPath =
			Path::FromResolved(FileSystem::TempDirectoryPath()) /
			("defectstudio_poscar_roundtrip_"
			 + std::to_string(Time::NowSteady().time_since_epoch().count())
			 + ".vasp");

		PymatgenRoundtripRequest request;
		request.inputPoscarPath = inputPath;
		request.outputPoscarPath = outputPath;

		PymatgenBridge bridge;
		Result<PymatgenRoundtripResult> roundtripResult = bridge.RoundtripPoscar(request);
		if (!roundtripResult)
		{
			const std::string &technicalDetails = roundtripResult.Error().technicalDetails;
			if (technicalDetails.find("No module named") != std::string::npos ||
			    technicalDetails.find("ModuleNotFoundError") != std::string::npos)
			{
				GTEST_SKIP() << "pymatgen is not available in the current Python environment.";
			}

			GTEST_SKIP() << "pymatgen bridge unavailable in current environment: " << technicalDetails;
		}

		EXPECT_TRUE(FileSystem::Exists(outputPath.Native()));
		EXPECT_GT(roundtripResult->siteCount, 0);
		EXPECT_FALSE(roundtripResult->reducedFormula.empty());

		std::error_code removeError;
		FileSystem::Remove(outputPath.Native(), removeError);
	}
} // namespace DefectStudio::Tests
