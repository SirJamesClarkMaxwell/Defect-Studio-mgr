#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Uuid.hpp"
#include "ScientificRuntime/Python/PhononModeVectorJob.hpp"

namespace DefectStudio::Tests
{
	TEST(PhononModeVectorJobTests, CancellationPublishesNothing)
	{
		JobContext context({}, {}, {}, {}, [] { return true; });
		PhononModeVectorJob job({}, {}, 3);
		EXPECT_EQ(job.GetType(), "phonon-mode-vector");
		EXPECT_THROW(job.Execute(context), JobCancelledException);
		EXPECT_FALSE(job.GetResult());
	}

	TEST(PhononModeVectorJobTests, ExistingOutputIsNeverOverwrittenOrRemovedAndErrorIsStructured)
	{
		const auto root = Path::FromResolved(std::filesystem::current_path() / ".task-113g-tests" / ToString(GenerateUuid()));
		std::filesystem::create_directories(root.Native());
		std::ofstream((root / "keep.txt").Native()) << "keep";
		JobContext context({}, {}, {}, {}, [] { return false; });
		PhononModeVectorJob job(root / "missing", root, 3);
		EXPECT_THROW(job.Execute(context), std::runtime_error);
		EXPECT_FALSE(job.GetResult()); ASSERT_TRUE(job.GetError());
		EXPECT_FALSE(job.GetError()->userMessage.empty()); EXPECT_FALSE(job.GetError()->technicalDetails.empty());
		EXPECT_TRUE(std::filesystem::exists((root / "keep.txt").Native()));
		std::filesystem::remove_all(root.Native());
	}
}
