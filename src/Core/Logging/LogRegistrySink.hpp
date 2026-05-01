#pragma once

#include <mutex>

#include <spdlog/sinks/base_sink.h>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class LogRegistry;

	class LogRegistrySink final : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit LogRegistrySink(Ref<LogRegistry> registry);
		~LogRegistrySink() override = default;

	protected:
		void sink_it_(const spdlog::details::log_msg &msg) override;
		void flush_() override;

	private:
		WeakRef<LogRegistry> m_Registry;
	};
} // namespace DefectStudio
