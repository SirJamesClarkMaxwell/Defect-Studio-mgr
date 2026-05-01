#include "Core/dspch.hpp"

#include "Core/Utils/SpdlogEventSink.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/LogEvents.hpp"

namespace DefectStudio
{
    SpdlogEventSink::SpdlogEventSink(Ref<EventBus> eventBus)
        : m_EventBus(std::move(eventBus))
    {
    }

    void SpdlogEventSink::sink_it_(const spdlog::details::log_msg &msg)
    {
        if (m_EventBus == nullptr)
            return;

        // use payload (already formatted by spdlog)
        std::string message(msg.payload.begin(), msg.payload.end());
        auto lvlView = spdlog::level::to_string_view(msg.level);
        const std::string level(lvlView.data(), lvlView.size());
        const std::string loggerName = (msg.logger_name.size() > 0) ? std::string(msg.logger_name.data(), msg.logger_name.size()) : std::string();
        Time::TimePoint ts = Time::Now();

        m_EventBus->Publish(LogEvent(level, loggerName, message, ts));
    }

    void SpdlogEventSink::flush_()
    {
    }
} // namespace DefectStudio
