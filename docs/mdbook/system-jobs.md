# Job System

## Purpose

`JobSystem` executes background jobs with priorities and delayed scheduling, while preserving a queryable in-memory history of job state.

Primary goal: keep the UI thread responsive and provide deterministic lifecycle visibility (`Queued -> Running -> Completed/Failed/Cancelled`).

## Public API (Entry)

The current backend API lives in `src/Core/JobSystem/JobSystem.hpp`.

Submission and control:

- `JobId Submit(const Ref<IJob>& job, JobPriority priority = JobPriority::Normal)`
- `JobId SubmitAfter(const Ref<IJob>& job, Time::Milliseconds delay, JobPriority priority = JobPriority::Normal)`
- `bool RequestCancel(JobId id)`
- `bool Resume(JobId id)`
- `bool Reset(JobId id)`
- `bool Retry(JobId id, JobPriority priority = JobPriority::Normal)`
- `bool RemoveFromHistory(JobId id)`

Runtime configuration:

- `std::size_t GetThreadCount() const`
- `bool SetThreadCount(std::size_t threadCount)`

Read/query:

- `std::optional<JobSnapshot> GetJob(JobId id) const`
- `std::vector<JobSnapshot> GetAllJobs() const`
- `std::vector<JobSnapshot> GetActiveJobs() const`
- `std::vector<JobSnapshot> GetFinishedJobs() const`
- `std::vector<JobLogEntry> GetLogs(JobId id) const`

Lifecycle:

- `void Shutdown()`

## Functional Capabilities

- Priority scheduling (`Lowest .. Highest`) on worker pool.
- Delayed jobs (`SubmitAfter`) with cancellation support before due time.
- Cooperative cancellation (`RequestCancel`) and restart flows (`Resume`, `Retry`, `Reset`).
- Parent/child relation tracking (`parentId`) when a job submits nested jobs.
- Event publication (`Queued`, `Started`, `Progress`, `Completed/Cancelled/Failed`) for read-model systems.
- Thread-count scaling at runtime (`SetThreadCount`) without dropping records.

## Execution Flow (Step By Step)

### 1. Source of event

Source: caller invokes `Submit` / `SubmitAfter`.

### 2. When it is processed

Processing moment: immediately in `submitInternal`.

### 3. Flow owner

Owner: `JobSystem` (`m_Records`, queue/delayed queue, worker pool).

### 4. Where it is handled

Handling points:

- queue snapshot recorded (`recordQueued`),
- event emitted (`publishQueuedEvent`),
- execution starts in worker (`runJob`),
- state transitions published (`publishStartedEvent`, `publishProgressEvent`, `publishFinishedEvent`).

### 5. Architectural consequence

Consequence: UI and monitoring systems consume immutable snapshots/events instead of touching worker-thread state directly.

## Cancellation/Resume Semantics

- `RequestCancel(id)` marks cancellation requested.
- Running jobs must check cancellation cooperatively via `JobContext`.
- `Resume(id)` clears cancellation flag and, if status is `Cancelled`, re-queues the same record/job id.
- `Retry(id)` re-queues finished jobs and resets runtime fields.
- `Reset(id)` clears finished state to `Queued` without immediate execution.

## Integration Points

- Producer side: any layer/service can submit jobs through `Application::Get().GetJobSystem()`.
- Consumer side: `ProgressTracker` subscribes to job events and builds UI snapshots.
- Presentation side: monitor windows render data from tracker snapshots, not directly from worker threads.

## Known Design Constraints

- Worker-to-worker blocking waits are intentionally guarded to avoid pool starvation/deadlock.
- Parent completion and child completion are separate events; subtree aggregation is a read-model concern.
