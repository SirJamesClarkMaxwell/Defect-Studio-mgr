#pragma once

#include <string>
#include "Core/Utils/Time.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"

namespace DefectStudio
{
    struct LogEvent final : public BusEvent
    {
        LogEvent(std::string lvl, std::string loggerName, std::string msg, Time::TimePoint ts)
            : level(std::move(lvl)), logger(std::move(loggerName)), message(std::move(msg)), timestamp(ts)
        {
        }

        std::string level;
        std::string logger;
        std::string message;
        Time::TimePoint timestamp;
    };
} // namespace DefectStudio
