#pragma once

#include <chrono>

#include "App/ApplicationState.hpp"

namespace DefectStudio
{
	namespace ApplicationDetail
	{
		void SetCrashStage(const char *stage);
		void AppendCrashMarker(const char *message);
		void InstallCrashFallbackHandlers();
		ApplicationSpecification ParseApplicationArguments(int argc, char **argv);
		void ApplySpecificationOverrides(ApplicationSpecification &specification);

		class StartupStepTimer
		{
		public:
			explicit StartupStepTimer(const char *name);
			~StartupStepTimer();

			bool Finish(bool success);

		private:
			const char *m_Name = "";
			std::chrono::steady_clock::time_point m_Start;
			bool m_Finished = false;
		};
	}
} // namespace DefectStudio
