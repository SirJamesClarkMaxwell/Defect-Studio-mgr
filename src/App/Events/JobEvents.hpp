#pragma once

#include <string>
#include <utility>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"

namespace DefectStudio
{
	struct JobStartedEvent final : public BusEvent
	{
		JobStartedEvent(JobId newId, std::string newName, std::string newType)
			: id(newId), name(std::move(newName)), type(std::move(newType))
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
	};

	struct JobCompletedEvent final : public BusEvent
	{
		JobCompletedEvent(JobId newId, std::string newName, std::string newType)
			: id(newId), name(std::move(newName)), type(std::move(newType))
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
	};

	struct JobCancelledEvent final : public BusEvent
	{
		JobCancelledEvent(JobId newId, std::string newName, std::string newType)
			: id(newId), name(std::move(newName)), type(std::move(newType))
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
	};

	struct JobFailedEvent final : public BusEvent
	{
		JobFailedEvent(JobId newId, std::string newName, std::string newType, std::string newErrorMessage)
			: id(newId), name(std::move(newName)), type(std::move(newType)), errorMessage(std::move(newErrorMessage))
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
		std::string errorMessage;
	};
} // namespace DefectStudio
