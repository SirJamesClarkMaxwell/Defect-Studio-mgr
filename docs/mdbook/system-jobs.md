# Job System

## What It Is

`JobSystem` is a thin wrapper over `BS::priority_thread_pool`.

Its purpose is simple: run CPU work in background threads so the UI thread stays responsive.

## Core API

- `Submit(...)`: run work and get `std::future<T>`.
- `SubmitDetached(...)`: fire-and-forget execution.
- `SubmitCancelable(...)`: cooperative cancellation with `CancellationToken`.
- `WaitIdle()`: wait until queued/running jobs complete.

## Cancellation Model

`CancellationSource` owns cancellation state.

- Call `source.GetToken()` and pass token into the job.
- Call `source.Cancel()` to request cancellation.
- Job code must periodically check `token.IsCancellationRequested()`.

Cancellation is cooperative, not preemptive.

## Example Without Lambdas (std::bind)

```cpp
class ImportWorker
{
public:
    int Execute(const DefectStudio::CancellationToken& token)
    {
        for (int i = 0; i < 100; ++i)
        {
            if (token.IsCancellationRequested())
                return -1;

            DoOneStep();
        }
        return 0;
    }

private:
    void DoOneStep();
};

DefectStudio::JobSystem jobs;
DefectStudio::CancellationSource source;
ImportWorker worker;

auto future = jobs.SubmitCancelable<int>(
    source.GetToken(),
        std::bind(&ImportWorker::Execute, &worker, std::placeholders::_1),
        BS::pr::normal);
```

## Object Lifetime Diagram

```text
Application
    owns JobSystem (global)
    owns ProgressTracker (global)
    owns LayerStack
        owns DemoLayer
            owns JobSystemDemo
                owns TaskEntry list
                    each TaskEntry owns future + cancel source + progress atomic + progressId
```

## Submit Sequence (Step By Step)

```text
UI button click
    -> JobSystemDemo::EnqueueTask()
    -> Application::Get().GetJobSystem().SubmitCancelable(...)
    -> task enqueued in BS::priority_thread_pool queue
    -> worker thread executes bound callable
    -> worker updates task progress atomic
    -> main thread polls future in SyncTasks()
    -> main thread writes ProgressTracker::Report/Finish

Batch jobs from any subsystem go to the same global pool:

- `Start Batch Of 3 Jobs` uses the same `Application::Get().GetJobSystem()` instance,
- all those tasks are visible in the shared `ProgressTracker` snapshot,
- multiple windows can render the same global queue state.
```

## Where Data Is Stored

- Queue and workers: inside `JobSystem::m_Pool`.
- Per-task runtime state: `JobSystemDemo::TaskEntry` vector.
- Cancellation flags: shared atomic bool owned by each `CancellationSource`.
- UI-safe progress state: `ProgressTracker::m_Entries` (mutex-protected map).

## How To Access State

- Core services through `Application` API:
    - `Application::Get().GetJobSystem()`
    - `Application::Get().GetProgressTracker()`
- UI snapshot via `ProgressTracker::Snapshot()`.

## EventBus Registration Lifecycle (Practical Rule)

For UI layers and windows, subscription should usually happen in lifecycle entry:

1. subscribe in constructor or `OnAttach`,
2. unsubscribe in destructor or `OnDetach`.

Why:

- guarantees handlers exist before events start flowing,
- avoids stale subscriptions after a panel is removed,
- allows many windows to subscribe to one event type (`vector<Subscription>` fan-out).

## Synchronization Strategy (Practical)

Recommended frame-loop pattern:

1. Worker thread computes data only.
2. UI/main thread polls state in `OnUpdate`.
3. Main thread performs final commit to application state.

This respects the "main-thread commit" architectural rule and avoids race-prone direct mutation from worker threads.

## Should Application Update Job Status In Main Loop?

Yes, application-level status transitions should be observed and finalized in the main loop (or in a dedicated main-thread runtime coordinator called by the loop).

Reason:

- rendering and UI consume main-thread state,
- domain-facing commits must be deterministic,
- worker threads should only produce intermediate/output artifacts.

## Priority Guidance

- `High`: latency-sensitive tasks required for immediate interaction.
- `Normal`: default background work.
- `Low`: opportunistic/non-critical tasks.

Priority misuse can starve low-priority work. Keep high-priority usage rare and explicit.

Current demo focus:

- demonstrates multi-task enqueue,
- does not expose custom user-defined priority enum yet,
- uses thread-pool built-in priority type (`BS::priority_t`).

## Common Pitfalls

- Blocking on `future.get()` from UI thread during rendering.
- Ignoring cancellation checks inside long-running loops.
- Writing directly to shared app/domain state from worker threads.
