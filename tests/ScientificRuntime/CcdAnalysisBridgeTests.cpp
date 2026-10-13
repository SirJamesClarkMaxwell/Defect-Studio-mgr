#include <gtest/gtest.h>

#include <limits>
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"

namespace DefectStudio::Tests
{
	TEST(CcdAnalysisBridgeTests, ArgumentsOmitUnsetFlagsAndUseUtf8AndTenSignificantDigits)
	{
		CcdAnalysisRequest request;
		request.outputDirectory = Path::FromUtf8("D:/Żółć/CCD wynik");
		request.sigmaMeV = 1.234567890123;
		request.temperatureK = 12.34567890123;
		EXPECT_EQ(BuildCcdAnalysisArguments(request), (std::vector<std::string>{
			"--sigma", "1.23456789", "--temperature", "12.3456789", "--out-dir", request.outputDirectory.Utf8()}));
		request.gsDir = Path::FromUtf8("D:/gs"); request.esDir = Path::FromUtf8("D:/es");
		request.tripletDir = Path::FromUtf8("D:/T"); request.gsAtEsDir = Path::FromUtf8("D:/gs at es");
		request.esAtGsDir = Path::FromUtf8("D:/es at gs"); request.phononResultDir = Path::FromUtf8("D:/phonons");
		request.zplFormula = "2*E_es - E_T - E_gs"; request.label = "Dimer CCD";
		EXPECT_EQ(BuildCcdAnalysisArguments(request), (std::vector<std::string>{
			"--gs-dir", request.gsDir.Utf8(), "--es-dir", request.esDir.Utf8(), "--triplet-dir", request.tripletDir.Utf8(),
			"--gs-at-es-dir", request.gsAtEsDir.Utf8(), "--es-at-gs-dir", request.esAtGsDir.Utf8(),
			"--phonon-result", request.phononResultDir.Utf8(), "--zpl-formula", request.zplFormula,
			"--label", request.label, "--sigma", "1.23456789", "--temperature", "12.3456789",
			"--out-dir", request.outputDirectory.Utf8()}));
		request = {};
		request.ccdDir = Path::FromUtf8("D:/CCD path");
		EXPECT_EQ(BuildCcdAnalysisArguments(request), (std::vector<std::string>{"--ccd-dir", request.ccdDir.Utf8(),
			"--sigma", "6", "--temperature", "0", "--out-dir", ""}));
	}

	TEST(CcdAnalysisBridgeTests, ParsesLastJsonLineAfterNoise)
	{
		const auto result = ParseCcdAnalysisOutput("log\n{\"ok\":false,\"error\":\"old\"}\n"
			"{\"ok\":true,\"out_dir\":\"D:/ccd\",\"zpl_eV\":4.2}\n");
		ASSERT_TRUE(result);
		EXPECT_EQ(result->outputDirectory, Path::FromUtf8("D:/ccd"));
		EXPECT_DOUBLE_EQ(result->zplEv, 4.2);
	}

	TEST(CcdAnalysisBridgeTests, ScriptFailureAndInvalidJsonHaveStructuredErrors)
	{
		const auto failed = ParseCcdAnalysisOutput(R"({"ok":false,"error":"No curvature"})");
		ASSERT_FALSE(failed);
		EXPECT_EQ(failed.Error().code, "python.ccd.script_failed");
		EXPECT_NE(failed.Error().userMessage.find("No curvature"), std::string::npos);
		for (const char *text : {"", "log only", R"({"ok":true})",
			R"({"ok":true,"out_dir":"","zpl_eV":2})", R"({"ok":true,"out_dir":"ccd","zpl_eV":null})",
			R"({"ok":true,"out_dir":"ccd","zpl_eV":-1})"})
		{
			const auto result = ParseCcdAnalysisOutput(text);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.Error().code, "python.ccd.invalid_json");
		}
	}

	TEST(CcdAnalysisBridgeTests, InvalidRequestsAreRejectedBeforePythonStarts)
	{
		CcdAnalysisRequest request;
		request.outputDirectory = Path::FromUtf8("unused");
		const auto invalid = [&](const CcdAnalysisRequest &input) {
			const auto result = CcdAnalysisBridge{}.Run(input);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.Error().code, "python.ccd.request_invalid");
		};
		invalid(request);
		request.gsDir = Path::FromUtf8("missing"); invalid(request); // pair required
		request.esDir = request.gsDir; request.ccdDir = request.gsDir; invalid(request); // exclusive
		request.ccdDir = Path{}; request.sigmaMeV = 0; invalid(request);
		request.sigmaMeV = 6; request.temperatureK = -1; invalid(request);
		request.temperatureK = std::numeric_limits<double>::quiet_NaN(); invalid(request);
		request.temperatureK = 0; invalid(request); // missing directories
	}
}
