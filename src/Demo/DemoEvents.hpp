#pragma once

#include <string>

namespace DefectStudio::Demo
{
	struct FileOpenedBusEvent
	{
		std::string filename;
	};

	struct DataProcessedBusEvent
	{
		std::string resultSummary;
	};

	struct PipelineStatusBusEvent
	{
		std::string message;
	};
}
