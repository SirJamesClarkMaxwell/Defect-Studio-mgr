#pragma once

namespace DefectStudio
{
	struct JobsConfig
	{
		int defaultWorkerThreadCount = 1;
		bool reserveUrgentWorker = true;
	};
} // namespace DefectStudio
