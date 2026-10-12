#include <gtest/gtest.h>

#include "IO/PhononPanelSettingsIO.hpp"

namespace DefectStudio::Tests
{
	TEST(PhononEmbeddingSettingsTests, InvalidNewValuesKeepDefaultsAndWarn)
	{
		PhononPanelSettings settings;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(
			"inputs: {mult: 12x0x1, dcutoff: -.5, bcutoff: .nan, ccdStep: 0}\n",
			Path::FromUtf8("D:/project"), settings, warnings, error)) << error;
		EXPECT_TRUE(settings.embeddingMult.empty());
		EXPECT_FALSE(settings.dcutoffAngstrom);
		EXPECT_FALSE(settings.bcutoffAngstrom);
		EXPECT_DOUBLE_EQ(settings.ccdStep, 0.25);
		EXPECT_EQ(warnings.size(), 4);
	}

	TEST(PhononEmbeddingSettingsTests, OversizedMultiplierIsIgnored)
	{
		PhononPanelSettings settings;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("inputs: {mult: 999999999999999999999x12x1}\n",
			Path::FromUtf8("D:/project"), settings, warnings, error)) << error;
		EXPECT_TRUE(settings.embeddingMult.empty());
		EXPECT_EQ(warnings.size(), 1);
	}

	TEST(PhononEmbeddingSettingsTests, OldSettingsKeepEmbeddingOffAndDefaultCcd)
	{
		PhononPanelSettings settings;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("formatVersion: 1\ninputs: {sigma: 5, gamma: 1}\n",
			Path::FromUtf8("D:/project"), settings, warnings, error)) << error;
		EXPECT_TRUE(settings.bulkForceConstantDirectory.Empty());
		EXPECT_TRUE(settings.embeddingMult.empty());
		EXPECT_FALSE(settings.dcutoffAngstrom);
		EXPECT_FALSE(settings.bcutoffAngstrom);
		EXPECT_TRUE(settings.ccdOutputDirectory.Empty());
		EXPECT_DOUBLE_EQ(settings.ccdStep, 0.25);
		EXPECT_TRUE(warnings.empty());
	}
}
