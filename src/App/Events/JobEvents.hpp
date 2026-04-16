#pragma once

#include <string>
#include <utility>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Time.hpp"

namespace DefectStudio
{
	struct JobQueuedEvent final : public BusEvent
	{
		JobQueuedEvent(
			JobId newId,
			std::string newName,
			std::string newType,
			Time::TimePoint newCreatedAt,
			JobId newParentId = 0,
			JobPriority newPriority = JobPriority::Normal)
			: id(newId), name(std::move(newName)), type(std::move(newType)), createdAt(newCreatedAt), parentId(newParentId), priority(newPriority)
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
		Time::TimePoint createdAt{};
		JobId parentId = 0;
		JobPriority priority = JobPriority::Normal;
	};

	struct JobStartedEvent final : public BusEvent
	{
		JobStartedEvent(JobId newId, std::string newName, std::string newType, Time::TimePoint newStartedAt)
			: id(newId), name(std::move(newName)), type(std::move(newType)), startedAt(newStartedAt)
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
		Time::TimePoint startedAt{};
	};

	struct JobProgressEvent final : public BusEvent
	{
		JobProgressEvent(
			JobId newId,
			float newCompletedWork,
			float newTotalWork,
			std::string newStage,
			std::string newMessage,
			Time::TimePoint newUpdatedAt)
			: id(newId),
			  completedWork(newCompletedWork),
			  totalWork(newTotalWork),
			  stage(std::move(newStage)),
			  message(std::move(newMessage)),
			  updatedAt(newUpdatedAt)
		{
		}

		JobId id = 0;
		float completedWork = 0.0f;
		float totalWork = 0.0f;
		std::string stage;
		std::string message;
		Time::TimePoint updatedAt{};
	};

	struct JobCompletedEvent final : public BusEvent
	{
		JobCompletedEvent(JobId newId, std::string newName, std::string newType, Time::TimePoint newFinishedAt)
			: id(newId), name(std::move(newName)), type(std::move(newType)), finishedAt(newFinishedAt)
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
		Time::TimePoint finishedAt{};
	};

	struct JobCancelledEvent final : public BusEvent
	{
		JobCancelledEvent(JobId newId, std::string newName, std::string newType, Time::TimePoint newFinishedAt)
			: id(newId), name(std::move(newName)), type(std::move(newType)), finishedAt(newFinishedAt)
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
		Time::TimePoint finishedAt{};
	};

	struct JobFailedEvent final : public BusEvent
	{
		JobFailedEvent(
			JobId newId,
			std::string newName,
			std::string newType,
			std::string newErrorMessage,
			Time::TimePoint newFinishedAt)
			: id(newId),
			  name(std::move(newName)),
			  type(std::move(newType)),
			  errorMessage(std::move(newErrorMessage)),
			  finishedAt(newFinishedAt)
		{
		}

		JobId id = 0;
		std::string name;
		std::string type;
		std::string errorMessage;
		Time::TimePoint finishedAt{};
	};
} // namespace DefectStudio
