#pragma once

#include <mutex>

#include <spdlog/sinks/base_sink.h>

namespace DefectStudio
{
	class LogRegistrySink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		LogRegistrySink() = default;
		~LogRegistrySink() override = default;

	protected:
		void sink_it_(const spdlog::details::log_msg &msg) override;
		void flush_() override;
	};
} // namespace DefectStudio