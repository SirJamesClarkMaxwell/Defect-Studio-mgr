#pragma once

#include <string>
#include <utility>

#include "Core/Events/Event.hpp"

namespace DefectStudio::Demo
{
	struct FileOpenedBusEvent : public BusEvent
	{
		std::string filename;

		FileOpenedBusEvent() = default;
		explicit FileOpenedBusEvent(std::string value) : filename(std::move(value))
		{
		}
	};

	struct DataProcessedBusEvent : public BusEvent
	{
		std::string resultSummary;

		DataProcessedBusEvent() = default;
		explicit DataProcessedBusEvent(std::string value) : resultSummary(std::move(value))
		{
		}
	};

	struct PipelineStatusBusEvent : public BusEvent
	{
		std::string message;

		PipelineStatusBusEvent() = default;
		explicit PipelineStatusBusEvent(std::string value) : message(std::move(value))
		{
		}
	};
}
