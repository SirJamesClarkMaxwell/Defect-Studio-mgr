#include <gtest/gtest.h>

#include "Core/Utils/Logger.hpp"

TEST(LoggerTests, InitializesAndReturnsLogger)
{
	DefectStudio::LoggerOptions options;
	options.level = DefectStudio::LogLevel::Debug;

	DefectStudio::Logger::Initialize(options);

	auto &logger = DefectStudio::Logger::Get();
	ASSERT_TRUE(logger != nullptr);
	EXPECT_EQ(logger->level(), spdlog::level::debug);

	DefectStudio::Logger::Shutdown();
}
