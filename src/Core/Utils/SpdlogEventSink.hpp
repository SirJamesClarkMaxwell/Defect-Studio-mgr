#pragma once

#include <memory>
#include <spdlog/sinks/base_sink.h>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
    class EventBus;

    class SpdlogEventSink : public spdlog::sinks::base_sink<std::mutex>
    {
    public:
        explicit SpdlogEventSink(Ref<EventBus> eventBus);
        ~SpdlogEventSink() override = default;

    protected:
        void sink_it_(const spdlog::details::log_msg &msg) override;
        void flush_() override;

    private:
        Ref<EventBus> m_EventBus;
    };
}
