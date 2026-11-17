#include <gtest/gtest.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

namespace DefectStudio::Tests
{
	TEST(PhononServerJobSessionTests, UsesActiveInputsAndBulkWithoutEnablingLocalEmbedding)
	{
		PhononPanelSession session;
		auto &s = session.Settings();
		s.forceConstantDirectory = Path::FromUtf8("D:/defect FC");
		s.bulkForceConstantDirectory = Path::FromUtf8("D:/bulk FC");
		s.groundStateDirectory = Path::FromUtf8("D:/GS");
		s.excitedStateDirectory = Path::FromUtf8("D:/ES");
		s.hrMethod = "approximate_forces";
		s.hrCutoffAngstrom = 4;
		s.dcutoffAngstrom = 5;
		s.zplFormula = "3.452";
		auto &controls = session.ServerJobControls();
		controls.nMin = 11; controls.nMax = 17;
		controls.output = Path::FromUtf8("D:/prepared job");
		controls.templates = Path::FromUtf8("D:/my assets");
		const auto request = session.ServerJobInputs();
		EXPECT_EQ(request.inputs.forceConstantDirectory, s.forceConstantDirectory);
		EXPECT_EQ(request.inputs.bulkForceConstantDirectory, s.bulkForceConstantDirectory);
		EXPECT_FALSE(request.inputs.embeddingMult);
		EXPECT_EQ(request.inputs.hrCutoffAngstrom, s.hrCutoffAngstrom);
		EXPECT_EQ(request.inputs.dcutoffAngstrom, s.dcutoffAngstrom);
		EXPECT_EQ(request.inputs.zplFormula, "3.452");
		EXPECT_EQ(request.nMin, 11); EXPECT_EQ(request.nMax, 17);
		EXPECT_EQ(request.inputs.outputDirectory, controls.output);
		EXPECT_EQ(request.templatesDirectory, controls.templates);
	}

	TEST(PhononServerJobSessionTests, TransientControlsArePerCaseAndClearedOnProjectChange)
	{
		PhononPanelSession session;
		const auto first = session.AddCase("first");
		session.ServerJobControls().nMax = 23;
		const auto second = session.AddCase("second");
		EXPECT_EQ(session.ServerJobControls().nMax, 50);
		ASSERT_TRUE(session.ActivateCase(first));
		EXPECT_EQ(session.ServerJobControls().nMax, 23);
		session.SetPersistedState({}, {});
		EXPECT_EQ(session.ServerJobControls().nMax, 50);
	}

	TEST(PhononServerJobSessionTests, DisabledReasonsIdentifyMissingInputs)
	{
		const auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
		PhononPanelSession session(jobs);
		EXPECT_NE(session.ServerJobBlocker().find("FC defektu"), std::string::npos);
		session.Settings().forceConstantDirectory = Path::FromResolved(FileSystem::CurrentPath());
		EXPECT_NE(session.ServerJobBlocker().find("FC bulku"), std::string::npos);
		session.Settings().bulkForceConstantDirectory = session.Settings().forceConstantDirectory;
		EXPECT_NE(session.ServerJobBlocker().find("GS i ES"), std::string::npos);
	}
}
