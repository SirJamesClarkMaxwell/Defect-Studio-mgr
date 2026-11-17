#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <fstream>
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Uuid.hpp"
#include "ScientificRuntime/Python/ServerJobBridge.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	TEST(ServerJobBridgeTests, ArgumentsReusePhononInputsWithoutLocalEmbeddingOrSubmission)
	{
		ServerJobRequest request;
		request.templatesDirectory = Path::FromUtf8("D:/user templates");
		request.nMin = 12; request.nMax = 20;
		auto &s = request.inputs;
		s.forceConstantDirectory = Path::FromUtf8("D:/FC");
		s.groundStateDirectory = Path::FromUtf8("D:/GS");
		s.excitedStateDirectory = Path::FromUtf8("D:/ES");
		s.bulkForceConstantDirectory = Path::FromUtf8("D:/bulk FC");
		s.embeddingMult = std::array<int, 3>{24, 24, 1};
		s.outputDirectory = Path::FromUtf8("D:/new job");
		s.hrMethod = "approximate_forces";
		s.hrCutoffAngstrom = 4.5;
		s.sigmaMeV = 2; s.sigmaMaxMeV = 8; s.gammaMeV = .5;
		s.experimentalZplEv = 3.45;
		s.dcutoffAngstrom = 5;
		const std::vector<std::string> expected = {"--templates-dir", request.templatesDirectory.Utf8(),
			"--n-min", "12", "--n-max", "20", "--fc-dir", s.forceConstantDirectory.Utf8(),
			"--gs-dir", s.groundStateDirectory.Utf8(), "--es-dir", s.excitedStateDirectory.Utf8(),
			"--zpl", "3450", "--sigma", "2", "--gamma", "0.5", "--bulk-fc-dir", s.bulkForceConstantDirectory->Utf8(),
			"--dcutoff", "5", "--hr-method", "approximate_forces", "--hr-cutoff", "4.5", "--sigma-max", "8",
			"--out-dir", s.outputDirectory.Utf8()};
		EXPECT_EQ(BuildServerJobArguments(request), expected);
	}

	TEST(ServerJobBridgeTests, ParsesLastJsonAndKeepsUsefulScriptErrors)
	{
		auto result = ParseServerJobOutput("progress\n{\"ok\":false,\"error\":\"earlier\"}\n{\"ok\":true,\"out_dir\":\"job folder\"}\n");
		ASSERT_TRUE(result);
		EXPECT_EQ(result->Utf8(), "job folder");
		result = ParseServerJobOutput("{\"ok\":false,\"error\":\"Missing templates: assets\"}\n");
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.server_job.script_failed");
		EXPECT_NE(result.Error().userMessage.find("assets"), std::string::npos);
		for (const std::string payload : {"", "noise", "{}", "{\"ok\":true,\"out_dir\":\"\"}", "{\"ok\":true,\"out_dir\":42}"})
		{
			result = ParseServerJobOutput(payload);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.Error().code, "python.server_job.invalid_json");
		}
	}

	TEST(ServerJobBridgeTests, InvalidInputsFailBeforePython)
	{
		ServerJobRequest request;
		request.templatesDirectory = FindRepoRoot();
		auto &s = request.inputs;
		s.forceConstantDirectory = s.groundStateDirectory = s.excitedStateDirectory = FindRepoRoot();
		s.bulkForceConstantDirectory = FindRepoRoot();
		s.outputDirectory = FindRepoRoot() / "never-written-server-job";
		for (const auto bounds : {std::pair{0, 10}, std::pair{10, 9}})
		{
			request.nMin = bounds.first; request.nMax = bounds.second;
			const auto result = ServerJobBridge{}.Run(request);
			ASSERT_FALSE(result);
			EXPECT_EQ(result.Error().code, "python.server_job.request_invalid");
		}
		request.nMin = 10; request.nMax = 12;
		s.hrCutoffAngstrom = std::numeric_limits<double>::quiet_NaN();
		auto result = ServerJobBridge{}.Run(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.server_job.request_invalid");
		s.hrCutoffAngstrom.reset();
		s.bulkForceConstantDirectory.reset();
		result = ServerJobBridge{}.Run(request);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.Error().code, "python.server_job.request_invalid");
	}

	TEST(ServerJobBridgeTests, JobReportsValidationFailureAndHonoursCancellation)
	{
		JobContext context({}, {}, {}, {}, [] { return false; });
		ServerJob job(ServerJobRequest{});
		EXPECT_THROW(job.Execute(context), std::runtime_error);
		ASSERT_TRUE(job.GetError());
		EXPECT_FALSE(job.GetResult());
		JobContext cancelled({}, {}, {}, {}, [] { return true; });
		ServerJob cancelledJob(ServerJobRequest{});
		EXPECT_ANY_THROW(cancelledJob.Execute(cancelled));
		EXPECT_FALSE(cancelledJob.GetResult());
		EXPECT_FALSE(cancelledJob.GetError());
	}

	TEST(ServerJobBridgeTests, ReferenceJobProducesFolderUsingOriginalTinyTemplates)
	{
		const auto root = FindRepoRoot();
		const auto system = root / "test-directory/sjcm_mgr/PL-spectra/dimer/CH0/R2SCAN/9x9x1";
		const auto bulk = root / "test-directory/sjcm_mgr/hBN/bulk/phonons/vasp_661";
		if (!FileSystem::IsDirectory((system / "dispersion").Native()) || !FileSystem::IsDirectory(bulk.Native()))
			GTEST_SKIP() << "local reference FC fixtures not present";
		const auto scratch = root.Join(".task-113d-cpp-" + ToString(GenerateUuid()));
		struct Cleanup
		{
			Path directory;
			~Cleanup() { std::error_code error; std::filesystem::remove_all(directory.Native(), error); }
		} cleanup{scratch};
		const auto templates = scratch / "templates";
		ASSERT_TRUE(FileSystem::CreateDirectories(templates.Native()));
		for (const std::string name : {"embeding.py", "hr_factors.py", "lineshape.py"})
			std::ofstream((templates / name).Native()) << "from pathlib import Path\nHERE = Path(__file__).parent\n# ===== CONFIG =====\nSYSTEM = None\n# ==================\n";
		std::ofstream((templates / "run_embeding_series.py").Native()) << "N_MIN = 1\nN_MAX = 2\n";
		for (const std::string name : {"run.job", "run_embeding.job"})
			std::ofstream((templates / name).Native()) << "# independent test asset\n";
		ServerJobRequest request;
		request.templatesDirectory = templates;
		request.inputs.forceConstantDirectory = system / "dispersion";
		request.inputs.bulkForceConstantDirectory = bulk;
		request.inputs.groundStateDirectory = system / "relax";
		request.inputs.excitedStateDirectory = system / "excited/try_1_singlet";
		request.inputs.experimentalZplEv = 3.452;
		request.inputs.outputDirectory = scratch / "job";
		JobContext context({}, {}, {}, {}, [] { return false; });
		ServerJob job(request);
		ASSERT_NO_THROW(job.Execute(context));
		ASSERT_TRUE(job.GetResult());
		EXPECT_EQ(*job.GetResult(), request.inputs.outputDirectory);
		for (const auto name : {"job.json", "README.txt", "inputs/gs/CONTCAR_aligned", "inputs/es/CONTCAR_aligned"})
			EXPECT_TRUE(FileSystem::Exists((*job.GetResult() / name).Native()));
	}
}
