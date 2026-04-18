# Progress Tracker System

## Purpose

`ProgressTracker` is an event-driven read model for job lifecycle and progress data.

It subscribes to job events and stores thread-safe snapshots that UI panels can render safely.

## Public API (Entry)

The current backend API lives in `src/Core/ProgressTrackingSystem/ProgressTracker.hpp`.

EventBus wiring:

- `explicit ProgressTracker(WeakRef<EventBus> eventBus = {})`
- `void BindEventBus(WeakRef<EventBus> eventBus)`
- `void UnbindEventBus()`

Read/query:

- `std::optional<ProgressEntrySnapshot> GetSnapshot(JobId id) const`
- `std::vector<ProgressEntrySnapshot> GetAllSnapshots() const`
- `std::vector<ProgressEntrySnapshot> GetActiveSnapshots() const`
- `std::vector<ProgressEntrySnapshot> GetFinishedSnapshots() const`
- `bool RemoveEntry(JobId id)`

## Functional Capabilities

- Builds and updates a snapshot per `JobId` from lifecycle events.
- Preserves parent-child relation (`parentId`) for nested jobs.
- Stores stage/message/progress values and terminal error summary.
- Exposes filtered views (`active`, `finished`) for UI and diagnostics.
- Supports manual cleanup (`RemoveEntry`) after backend history deletion.

## Event Flow (Step By Step)

### 1. Source of event

Source: `JobSystem` publishes `JobQueuedEvent`, `JobStartedEvent`, `JobProgressEvent`, `JobCompletedEvent`, `JobCancelledEvent`, `JobFailedEvent`.

### 2. When it is processed

Processing moment: when `EventBus::ProcessQueue()` runs on the main update loop.

### 3. Flow owner

Owner: `ProgressTracker` subscriptions and `m_Entries` map.

### 4. Where it is handled

Handling methods:

- `onQueued` initializes entry and metadata,
- `onStarted` marks running state and start time,
- `onProgress` updates stage/message/work counters,
- `onCompleted` / `onCancelled` / `onFailed` close lifecycle.

### 5. Architectural consequence

Consequence: UI reads consistent snapshots without direct synchronization against worker threads.

## Data Contract (`ProgressEntrySnapshot`)

Each snapshot contains:

- identity: `id`, `parentId`,
- descriptors: `source`, `label`,
- state: `status`, `priority`, `finished`,
- progress: `completedWork`, `totalWork`, `currentStage`, `currentMessage`,
- timing: `createdAt`, `startedAt`, `finishedAt`,
- terminal diagnostics: `errorSummary`.

## Integration Notes

- `ProgressTracker` does not execute jobs; it only reflects observed events.
- Any aggregate subtree semantics should be computed in a read model / presentation layer.
- When backend history is pruned, tracker entries should be pruned as well to avoid stale UI rows.
