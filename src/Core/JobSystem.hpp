#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

#include <BS_thread_pool.hpp>

namespace DefectStudio
{
	class CancellationToken
	{
	public:
		[[nodiscard]] bool IsCancellationRequested() const;
		[[nodiscard]] bool IsValid() const;

	private:
		explicit CancellationToken(std::shared_ptr<std::atomic_bool> state);

		std::shared_ptr<std::atomic_bool> m_State;

		friend class CancellationSource;
	};

	class CancellationSource
	{
	public:
		CancellationSource();

		[[nodiscard]] CancellationToken GetToken() const;
		void Cancel();

	private:
		std::shared_ptr<std::atomic_bool> m_State;
	};

	class JobSystem
	{
	public:
		using Priority = BS::priority_t;

		explicit JobSystem(std::size_t threadCount = std::thread::hardware_concurrency());

		[[nodiscard]] std::size_t GetThreadCount() const;
		[[nodiscard]] std::size_t GetQueuedJobs() const;
		[[nodiscard]] std::size_t GetRunningJobs() const;
		[[nodiscard]] std::size_t GetTotalJobs() const;

		void Wait();
		void Purge();

		template <typename TCallable>
		[[nodiscard]] auto Submit(TCallable &&callable, Priority priority = BS::pr::normal)
		    -> std::future<std::invoke_result_t<TCallable>>
		{
			using Result = std::invoke_result_t<TCallable>;
			return m_Pool.submit_task(std::forward<TCallable>(callable), priority);
		}

		template <typename TCallable>
		void SubmitDetached(TCallable &&callable, Priority priority = BS::pr::normal)
		{
			static_assert(std::is_void_v<std::invoke_result_t<TCallable>>, "Detached jobs must not return a value");
			m_Pool.detach_task(std::forward<TCallable>(callable), priority);
		}

		template <typename TCallable>
		[[nodiscard]] auto SubmitCancelable(CancellationToken token, TCallable &&callable, Priority priority = BS::pr::normal) -> std::future<std::invoke_result_t<TCallable, const CancellationToken &>>
		{
			using Result = std::invoke_result_t<TCallable, const CancellationToken &>;
			return m_Pool.submit_task(
			    [token = std::move(token), callable = std::forward<TCallable>(callable)]() mutable -> Result
			    {
				    return invokeCancelable<Result>(token, callable);
			    },
			    priority);
		}

	private:
		template <typename TResult>
		static TResult canceledResult()
		{
			if constexpr (std::is_void_v<TResult>)
				return;

			return TResult{};
		}

		template <typename TResult, typename TCallable>
		static TResult invokeCancelable(const CancellationToken &token, TCallable &callable)
		{
			if (token.IsCancellationRequested())
				return canceledResult<TResult>();

			if constexpr (std::is_void_v<TResult>)
			{
				callable(token);
				return;
			}

			return callable(token);
		}

		BS::priority_thread_pool m_Pool;
	};
} // namespace DefectStudio