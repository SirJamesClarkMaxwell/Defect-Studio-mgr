# TODO Detailed – T04 Core Implementation Plan (Copilot Rewrite)

**Status:** rewrite based on current code state  
**Branch:** `task/04-core`  
**Last updated:** 2026-04-12

> This version is written for implementation guidance.  
> It is grounded in the current codebase, not only in the original backlog text.

---

## 0. Goal of this rewrite

Make T04 implementable in small, safe, reviewable steps.

This file tells Copilot:
- what already exists and must be preserved
- what is only partial and must be extended
- what new files/classes should be added
- what order to implement things in
- what tests must exist before moving to the next slice

This plan intentionally avoids vague backlog language.

---

## 1. Current code state snapshot

### 1.1 Already implemented and usable

#### Application shell
- `Application` already has `Create`, `Run`, `Shutdown`, `OnEvent`, `onUpdate`, `onRender`, `mainLoop`, logger setup, GLFW/GLAD/ImGui initialization, and default layer setup.
- OS/window events are already routed through one path: `Window -> Application::OnEvent -> LayerStack`.
- Layer update order is forward.
- Layer event order is reverse.
- Main loop is already deterministic enough for T04 stabilization.

#### Layer system
- `Layer` base class exists.
- `LayerStack` supports push/pop for layers and overlays.
- `OnAttach`/`OnDetach` are already called at the right moments.

#### Low-level event system for window/input
- `Core/Event.hpp` already contains:
  - `Event`
  - `EventDispatcher`
  - window/input event types
- This system is already used by `Application` for OS/input/window routing.

#### Thin publish-subscribe bus exists, but only as a minimal MVP
- `Core/EventBus.hpp` already exists.
- It supports typed subscribe / unsubscribe / publish.
- It does **not** yet support:
  - RAII subscription ownership
  - event priorities
  - safe unsubscribe during dispatch
  - queued main-thread delivery
  - diagnostics queries
  - notification/history integration

#### Job executor exists, but only as a backend wrapper
- `JobSystem` already wraps `BS::priority_thread_pool`.
- `CancellationToken` and `CancellationSource` already exist.
- `Submit`, `SubmitDetached`, `SubmitCancelable`, `Wait`, `Purge`, and job counters already exist.
- Current public API is still tied to `BS::priority_t`, so it is not backend-agnostic yet.
- There is no `JobManager`, no job records, no history, no logs, no snapshots, no event emission.

#### Progress tracking exists, but only as a tiny session-local registry
- `ProgressTracker` already supports `Register`, `Report`, `Finish`, `Snapshot`, `Has`.
- Current model contains only:
  - `id`
  - `label`
  - `totalWork`
  - `completedWork`
  - `finished`
- There is no status enum, timestamps, stage/message, error, tags, source, or integration with jobs.

#### EventQueue foundational implementation exists
- `EventQueue` extracted from `Application` into `src/Core/EventQueue.hpp` + `.cpp`
- Migration completed from `vector<Unique<Event>>` to `std::variant<...>`
  - Benefits: compile-time event safety, better memory locality (inline variant storage vs scattered heap allocations)
  - Design: `EventVariant` contains all 10 closed event types; variant instances are stored directly in vector
  - API currently exposed: `Configure()`, `Add()`, `Drain()`, `Size()`, `Capacity()`, `Empty()`, `SetGrowthStep()`, `Resize()`, `FitToSize()`, `Lock()`, `Unlock()`
  - Migration steps completed: (1) add `EventVariant` to `Core/Event.hpp`, (2) update `EventQueue.hpp` internal types, (3) simplify `EventQueue.cpp`, (4) refactor `Application` to use value-based construction instead of `MakeUnique<>`
  - **Lock/Unlock restored** – explicit locking remains available and uses `recursive_mutex` so nested calls from the same thread do not deadlock
- Once complete, enables memory pooling in future iterations if needed

### 1.3 Completed since commit `0520821`

- `Application::OnEvent<TEvent>` is now the public event entry point and variant visitor calls it directly.
- `Core/Events/` hierarchy is in place and `Core/Event.hpp` serves as an aggregate + `EventVariant` definition.
- EventQueue regression coverage added in `tests/Core/EventQueueTests.cpp`.
- mdBook was reorganized into a start page + section pages (`Build and Validation`, `Reference`, `Runtime Core`, `Architecture`).
- Event system documentation was updated to explicitly separate low-level EventHandling vs EventBus and include practical usage patterns.

#### ConfigManager partially exists
- `ConfigManager.cpp` already supports default document creation, UI settings document creation, JSON/YAML load/save, format detection, and typed getters.
- The config path/document ownership boundary is still not explicit in T04 docs and integration.

### 1.2 Important design fact that must not be blurred

There are **two different event mechanisms** in the codebase and they must stay separate:

1. **Low-level OS/input event routing**
   - current file: `Core/Event.hpp`
   - used by `Application`, `Window`, `LayerStack`
   - purpose: keyboard/mouse/window events

2. **High-level publish-subscribe application bus**
   - current file: `Core/EventBus.hpp`
   - purpose: panel-to-panel and subsystem-to-subsystem communication

Do **not** merge those systems into one giant abstraction during T04.

---

## 2. T04 implementation rules

### 2.1 Preserve what already works
- Do not rewrite the GLFW/GLAD/ImGui shell unless required by tests or lifecycle safety.
- Do not replace `LayerStack`; only stabilize and test it.
- Do not replace the existing window/input event classes just to match the EventBus specification.

### 2.2 Keep T04 small
- No renderer architecture growth.
- No Python execution features.
- No project/domain storage expansion beyond ownership boundaries.
- No deep ECS or scene-system refactors.

### 2.3 Prefer extension over replacement
- Extend `JobSystem` into a usable runtime by adding a `JobManager` on top.
- Extend `ProgressTracker` rather than deleting it.
- Replace the minimal `EventBus` implementation only if the public semantics become clearer after the rewrite.

### 2.4 Every step must compile on its own
For each phase below:
1. add/update tests
2. implement the minimum code
3. build Debug + Release
4. only then move to the next phase

---

## 3. Architecture decisions for T04

### 3.1 Application singleton decision
The current code already behaves as a single application instance, but it is not a classic global singleton.

**Decision for T04:**
- do **not** force a heavy singleton rewrite
- keep one runtime `Application` instance
- only add `Application::Get()` if another already-existing subsystem truly needs it
- if `Application::Get()` is added, it must be a thin accessor, not a service locator

### 3.2 EventBus design decision
The original event-system spec is good, but its `Event` base name collides with the already existing low-level `Core/Event.hpp`.

**Decision for T04:**
- keep low-level `Core/Event.hpp` unchanged as the OS/input event system
- upgrade the publish-subscribe bus separately
- add a dedicated bus-event base type with a non-conflicting name, for example:
  - `Core/Events/BusEvent.hpp`
  - or `Core/Events/AppBusEvent.hpp`
- bus event types such as `JobStartedEvent`, `ProjectOpenedEvent`, `NotificationEvent` derive from that bus-event base

### 3.3 Job runtime design decision
The current `JobSystem` is only a task executor.

**Decision for T04:**
- keep `JobSystem` as the low-level thread-pool wrapper
- add `JobManager` above it for:
  - submission API
  - monitoring snapshots
  - cancellation lookup by job id
  - logs / stage / message / status
  - event emission
- do not move monitoring state into `JobSystem`

### 3.4 Progress ownership decision
The job spec wants job records; the current code already has `ProgressTracker`.

**Decision for T04:**
- `JobManager` owns the canonical job runtime state
- `ProgressTracker` becomes a lightweight task/progress read model fed by `JobManager`
- `ProgressTracker` must not become a second independent source of truth for the same job

### 3.5 Notification decision
Follow the event-system spec direction.

**Decision for T04:**
- implement notifications on top of the upgraded EventBus
- keep live toasts and persistent history separate
- runtime execution failures go to non-blocking notifications
- blocking popups are only for pre-execution validation failures

---

## 4. Target file layout for T04

### 4.1 Keep existing files
- `src/App/Application.hpp`
- `src/App/Application.cpp`
- `src/Core/Event.hpp`
- `src/Core/Layer.hpp`
- `src/Core/Layer.cpp`
- `src/Core/LayerStack.hpp`
- `src/Core/LayerStack.cpp`
- `src/Core/JobSystem.hpp`
- `src/Core/JobSystem.cpp`
- `src/Core/ProgressTracker.hpp`
- `src/Core/ProgressTracker.cpp`
- `src/App/ConfigManager.*`

### 4.2 Add new files for the publish-subscribe bus
- `src/Core/Events/BusEvent.hpp`
- `src/Core/Events/EventPriority.hpp` or keep enum in `BusEvent.hpp`
- `src/Core/Events/SubscriptionHandle.hpp`
- `src/Core/Events/SubscriptionHandle.cpp`
- `src/Core/Events/EventReceiver.hpp`
- `src/Core/EventBus.hpp` (rewritten public bus API)
- `src/Core/EventBus.cpp`

### 4.3 Add bus event payloads
- `src/Events/ApplicationBusEvents.hpp`
- `src/Events/ProjectBusEvents.hpp`
- `src/Events/JobBusEvents.hpp`
- `src/Events/NotificationEvents.hpp`

### 4.4 Add job-monitoring files
- `src/Core/Jobs/JobTypes.hpp`
- `src/Core/Jobs/IJob.hpp`
- `src/Core/Jobs/JobContext.hpp`
- `src/Core/Jobs/JobContext.cpp`
- `src/Core/Jobs/JobRecord.hpp`
- `src/Core/Jobs/JobSnapshot.hpp`
- `src/Core/Jobs/JobManager.hpp`
- `src/Core/Jobs/JobManager.cpp`
- `src/Core/Jobs/JobCancelledException.hpp`
- `src/Core/Jobs/JobLogEntry.hpp`

### 4.5 Add notifications and errors
- `src/Core/Diagnostics/StructuredError.hpp`
- `src/Core/Notifications/Notification.hpp`
- `src/Core/Notifications/Notifier.hpp`
- `src/Core/Notifications/Notifier.cpp`
- `src/Core/Notifications/NotificationCenter.hpp`
- `src/Core/Notifications/NotificationCenter.cpp`
- `src/Core/Notifications/NotificationHistory.hpp`
- `src/Core/Notifications/NotificationHistory.cpp`

### 4.6 Add thread-affinity helpers
- `src/Core/Threading/ThreadAffinity.hpp`
- `src/Core/Threading/ThreadAffinity.cpp`

---

## 5. Implementation phases

## Phase 1 – Stabilize the existing Application shell

### Goal
Finish the lifecycle work that is already mostly present without large refactors.

### Tasks
- [ ] Make `Create()` idempotent or explicitly guarded against double initialization.
- [ ] Make `Shutdown()` idempotent.
- [ ] Ensure destructor safely calls `Shutdown()` when resources still exist.
- [ ] Keep the existing call order explicit and tested:
  1. `PollEvents`
  2. `onUpdate`
  3. `beginImGuiFrame`
  4. `Layer::OnImGuiRender`
  5. main panel drawing
  6. `onRender`
- [ ] Keep event propagation order as reverse `LayerStack` traversal.
- [ ] Add startup and shutdown logs for every major phase.
- [ ] Add `m_Created` or similar state if needed to separate “created” from “running”.
- [ ] Decide whether `Application::Get()` is needed. Default answer: no.

### Files to touch
- `src/App/Application.hpp`
- `src/App/Application.cpp`

### Tests
- [ ] app creates successfully once
- [ ] double shutdown is safe
- [ ] event order is reverse layer order
- [ ] update order is forward layer order
- [ ] window close event stops the loop cleanly

### Exit condition
`Application` shell is stable enough that later work can depend on it without lifecycle surprises.

---

## Phase 2 – Keep low-level events, upgrade the publish-subscribe EventBus

### Goal
Turn the current minimal `EventBus` into the T04 subsystem bus without breaking `Core/Event.hpp`.

### Tasks
- [ ] Introduce a dedicated bus-event base type, e.g. `BusEvent`, with:
  - `handled`
  - `stopPropagation`
- [ ] Add `EventPriority`.
- [ ] Add `SubscriptionHandle` with move-only RAII semantics.
- [ ] Add `EventReceiver` helper for classes that own multiple subscriptions.
- [ ] Rewrite `EventBus` to support:
  - typed subscribe
  - stable listener ids
  - priority ordering
  - deferred unsubscribe during dispatch
  - `ClearAllListeners`
  - queued publishing from worker threads
  - `ProcessQueue`
  - `ClearQueue`
  - diagnostics: subscriber count, queue count, dispatch state
- [ ] Keep `Publish()` working on mutable events so listeners may set `handled` / `stopPropagation`.
- [ ] Add helper `SubscribeMember(...)` if it improves call sites.

### Important rule
Do **not** refactor `Core/Event.hpp` into this new bus system.

### Files to touch
- `src/Core/EventBus.hpp`
- `src/Core/EventBus.cpp`
- `src/Core/Events/BusEvent.hpp`
- `src/Core/Events/SubscriptionHandle.*`
- `src/Core/Events/EventReceiver.hpp`

### Tests
- [ ] publish delivers to all subscribers
- [ ] higher priority runs first
- [ ] stopPropagation stops later listeners
- [ ] unsubscribe during dispatch is safe
- [ ] queued events are delivered only after `ProcessQueue()`
- [ ] destroying `SubscriptionHandle` unsubscribes automatically
- [ ] destroying `EventReceiver` clears all owned subscriptions

### Exit condition
The subsystem bus matches the practical behavior of the event-system specification while coexisting cleanly with the low-level input/window event system.

---

## Phase 3 – Add bus event types and integrate them into Application

### Goal
Make the new EventBus visible in runtime flow.

### Tasks
- [ ] Add application bus events:
  - `AppStartedEvent`
  - `AppClosingEvent`
- [ ] Add project bus events:
  - `ProjectOpenedEvent`
  - `ProjectClosedEvent`
- [ ] Add job bus events:
  - `JobQueuedEvent`
  - `JobStartedEvent`
  - `JobProgressEvent`
  - `JobCompletedEvent`
  - `JobCancelledEvent`
  - `JobFailedEvent`
- [ ] Add `NotificationEvent`.
- [ ] Add `EventBus m_EventBus;` to `Application`.
- [ ] In the main loop, call `m_EventBus.ProcessQueue()` once per frame on the main thread.
- [ ] Publish `AppStartedEvent` after successful initialization and before entering the main loop.
- [ ] Publish `AppClosingEvent` during orderly shutdown.

### Files to touch
- `src/App/Application.hpp`
- `src/App/Application.cpp`
- `src/Events/*.hpp`

### Tests
- [ ] app started event is emitted once
- [ ] app closing event is emitted once
- [ ] queued worker-thread events become visible on the main thread after `ProcessQueue()`

### Exit condition
Runtime has a real subsystem bus and the app loop owns the queue-processing boundary.

---

## Phase 4 – Convert JobSystem from thin wrapper into a two-level runtime

### Goal
Keep the current executor, but add the missing monitoring/runtime layer from the job spec.

### 4.1 First step: clean up the existing `JobSystem`
- [ ] Replace public `using Priority = BS::priority_t` with a project enum, e.g.:
  - `enum class JobPriority { High, Normal, Low };`
- [ ] Map project priority to `BS::priority_t` internally in `.cpp`.
- [ ] Keep `Submit`, `SubmitDetached`, `SubmitCancelable`, `Wait`, `Purge`, counters.
- [ ] Keep `CancellationToken` and `CancellationSource`.
- [ ] Add explicit pre-execution cancellation handling tests.

### 4.2 Add new monitoring/runtime layer
Implement a new `JobManager` above `JobSystem`.

#### `JobManager` responsibilities
- own job ids
- submit jobs
- store runtime records
- own cancellation sources by job id
- expose snapshots
- expose logs
- translate runtime state to bus events
- provide a single application-facing API

#### Add these types
- `JobId`
- `JobStatus`
- `JobLogSeverity`
- `JobLogEntry`
- `JobRecord`
- `JobSnapshot`
- `JobCancelledException`
- `IJob`
- `JobContext`

#### `JobContext` responsibilities
- set progress in `[0, 1]`
- set stage
- set message
- append logs
- check cancellation
- throw `JobCancelledException` when requested

#### `JobRecord` fields must include
- id
- name
- type
- status
- progress
- current stage
- current message
- created / started / finished timestamps
- error message
- logs

### 4.3 Execution flow
When submitting a job:
1. create `JobRecord`
2. create `CancellationSource`
3. store both under the new `JobId`
4. emit `JobQueuedEvent`
5. submit wrapper callable to `JobSystem`
6. inside wrapper:
   - mark `Running`
   - emit `JobStartedEvent`
   - run job inside exception boundary
   - on success mark `Completed`
   - on cancellation mark `Cancelled`
   - on error mark `Failed`
7. queue final bus events back to the main thread when UI-facing delivery is required

### 4.4 Error boundary rules
Wrap job execution exactly once at the manager boundary:
- catch `JobCancelledException`
- catch `std::exception`
- catch `...`

Never let a job exception escape into the thread-pool boundary.

### Files to touch
- `src/Core/JobSystem.hpp`
- `src/Core/JobSystem.cpp`
- add `src/Core/Jobs/*`

### Tests
- [ ] submit returns a valid `JobId`
- [ ] queued -> running -> completed transition works
- [ ] queued cancellation works
- [ ] running cancellation works when job cooperates
- [ ] failed job stores error text
- [ ] logs are retained in the record
- [ ] snapshots reflect progress and stage
- [ ] public API no longer leaks `BS::priority_t`

### Exit condition
The project has a real Job MVP consistent with the job-system specification, while still reusing the existing executor backend.

---

## Phase 5 – Expand ProgressTracker without duplicating JobManager ownership

### Goal
Keep `ProgressTracker`, but make its role explicit and useful.

### Tasks
- [ ] Add a richer `ProgressEntry` model:
  - id
  - source
  - label
  - status
  - priority
  - total work
  - completed work
  - current stage
  - current message
  - timestamps
  - finished flag or status-derived completion
  - optional error summary
- [ ] Add session-stable snapshots safe for UI reads.
- [ ] Feed `ProgressTracker` from `JobManager` lifecycle updates.
- [ ] Do not let UI mutate `ProgressTracker` directly.
- [ ] Keep first version flat; no nested subtask tree yet.

### Recommended rule
- `JobManager` owns canonical live state.
- `ProgressTracker` is the read model for job/task progress panels.

### Files to touch
- `src/Core/ProgressTracker.hpp`
- `src/Core/ProgressTracker.cpp`
- `src/Core/Jobs/JobManager.cpp`

### Tests
- [ ] tracker entry created on job submit
- [ ] progress updates appear in snapshot
- [ ] finish marks entry completed
- [ ] failure and cancellation are visible in snapshot
- [ ] concurrent snapshot calls stay safe

### Exit condition
The app can show a real job/task monitor without querying raw mutable job internals directly.

---

## Phase 6 – Finish ConfigManager ownership boundary

### Goal
Turn the partially implemented config code into an explicit T04 boundary.

### Tasks
- [ ] Add/verify header that exposes `ConfigDocument`, `ConfigLoadResult`, and path helpers clearly.
- [ ] Make `ConfigManager` the only owner of config file IO for:
  - `default.yaml`
  - `ui_settings.yaml`
- [ ] Keep JSON load support only as migration/import compatibility, not as the preferred long-term format.
- [ ] Add fallback-to-default flow when a config file is missing or invalid.
- [ ] Connect application startup to config loading in a minimal way.
- [ ] Do not entangle config parsing with UI code.

### Files to touch
- `src/App/ConfigManager.hpp`
- `src/App/ConfigManager.cpp`
- `src/App/Application.cpp`

### Tests
- [ ] create default document
- [ ] create UI settings document
- [ ] save/load YAML roundtrip
- [ ] save/load JSON roundtrip if migration support is kept
- [ ] invalid file returns structured failure
- [ ] missing file falls back to default document when policy says so

### Exit condition
Config ownership is clear enough that UI settings and runtime defaults can be layered safely later.

---

## Phase 7 – Structured errors and notifications

### Goal
Connect runtime failures to user-visible policy without blocking the UI unnecessarily.

### 7.1 Structured error model
Add a shared error type containing:
- category
- code
- technical context
- user-facing message
- optional source/subsystem

Use it in:
- config failures
- job failures
- notification generation

### 7.2 Notification implementation
- [ ] Add `Notification` data model.
- [ ] Add `Notifier` that publishes `NotificationEvent` and keeps in-memory history.
- [ ] Add `NotificationCenter` for live notifications.
- [ ] Add `NotificationHistory` for session persistence and inspection.
- [ ] Route job runtime failures to notifications.
- [ ] Keep blocking popup policy only for pre-execution validation failures.

### Files to touch
- `src/Core/Diagnostics/StructuredError.hpp`
- `src/Core/Notifications/*`
- `src/Events/NotificationEvents.hpp`
- `src/Core/Jobs/JobManager.cpp`
- `src/App/Application.*`

### Tests
- [ ] error converts to notification
- [ ] notification history stores emitted items
- [ ] severity mapping works
- [ ] blocking vs non-blocking policy is testable

### Exit condition
Background runtime failures are visible in UI policy flow without crashing or freezing the application.

---

## Phase 8 – Thread affinity and main-thread commit rule

### Goal
Protect project-visible state and UI code from worker-thread mutation.

### Tasks
- [ ] Add thread-affinity helper storing the main thread id at startup.
- [ ] Add `ASSERT_MAIN_THREAD`.
- [ ] Assert on critical UI-facing mutation paths.
- [ ] Document the rule:
  - background jobs may compute and publish intent/snapshots
  - only the main thread commits project-visible runtime state
- [ ] Keep EventBus queue processing and notification presentation on the main thread.

### Files to touch
- `src/Core/Threading/ThreadAffinity.*`
- `src/App/Application.cpp`
- job/notification integration files

### Tests / checks
- [ ] safe integration test for main-thread-only commit path
- [ ] debug assertions trigger in development builds where practical

### Exit condition
The boundary between worker execution and UI/runtime mutation is explicit and enforceable.

---

## Phase 9 – Diagnostics UI skeleton

### Goal
Deliver the minimum diagnostics required by T04.

### Tasks
- [ ] Add UIStyleEditor with saving imgui ui settings
- [ ] Add baseline log panel.
- [ ] Add job/task monitor panel backed by `ProgressTracker` snapshots.
- [ ] Add capability snapshot stub if the registry is not yet fully implemented.
- [ ] Add notification history panel.
- [ ] Keep `DebugLayer` privileged and read-mostly.

### Scope rule
This is only a diagnostics skeleton, not the final UX polish pass.

### Files to touch
- `src/Debug/DebugLayer.*`
- related ImGui panels

### Tests
- [ ] debug layer activation policy
- [ ] diagnostics path reachable from app shell

### Exit condition
T04 ends with visible operational diagnostics instead of hidden internal state.

---

## 6. Concrete order for Copilot to implement

### Slice A – stabilize existing runtime
1. write lifecycle tests for `Application`
2. make `Shutdown()` safe and idempotent
3. verify layer ordering tests

### Slice B – upgrade EventBus
1. add bus-event base type
2. add `SubscriptionHandle`
3. add `EventReceiver`
4. rewrite `EventBus`
5. add bus tests
6. integrate `ProcessQueue()` into `Application`

### Slice C – add job runtime layer
1. decouple `JobSystem` public priority from BS types
2. add `JobTypes`, `IJob`, `JobContext`, `JobRecord`, `JobSnapshot`
3. add `JobManager`
4. wire job lifecycle to `EventBus`
5. add fake jobs for tests (`SleepJob`, `FailingJob`)

### Slice D – expand progress tracking
1. extend `ProgressEntry`
2. connect `JobManager` -> `ProgressTracker`
3. add snapshot tests

### Slice E – finish config boundary
1. finalize `ConfigManager` public header/API
2. add fallback and save/load tests
3. connect startup defaults

### Slice F – errors + notifications
1. add `StructuredError`
2. add `Notification`, `Notifier`, history
3. wire job/config failures into notifications

### Slice G – thread affinity + diagnostics
1. add `ASSERT_MAIN_THREAD`
2. add minimal debug/job/log/notification panels
3. add final integration checks

---

## 7. TDD order

1. [ ] Application lifecycle ordering tests
2. [ ] EventBus behavior tests
3. [ ] JobSystem backend wrapper tests
4. [ ] JobManager state transition tests
5. [ ] ProgressTracker snapshot tests
6. [ ] ConfigManager load/save/fallback tests
7. [ ] Structured error and notification tests
8. [ ] final integration tests across app shell + EventBus + JobManager

---

## 8. Acceptance criteria for rewritten T04

T04 is done when all of the following are true:

- [ ] `Application` boots and shuts down safely with no lifecycle ambiguity.
- [ ] `LayerStack` update/event ordering is covered by tests.
- [ ] `Core/Event.hpp` remains the low-level OS/input event system.
- [ ] `EventBus` is a separate, upgraded subsystem bus with RAII subscriptions and queued delivery.
- [ ] `Application` processes queued bus events once per frame on the main thread.
- [ ] `JobSystem` no longer leaks backend-specific priority types in its public API.
- [ ] `JobManager` exists and owns monitoring state, cancellation lookup, logs, and snapshots.
- [ ] `ProgressTracker` is integrated with jobs and usable for diagnostics UI.
- [ ] `ConfigManager` is the explicit owner of config file IO.
- [ ] structured runtime failures are turned into notifications.
- [ ] worker threads do not directly mutate project-visible UI/runtime state.
- [ ] T04 ends with a usable diagnostics skeleton: log panel, job/task monitor, notification history.

---

## 9. Explicit non-goals

- [ ] Do not refactor renderer architecture in T04.
- [ ] Do not merge low-level input/window events with the subsystem EventBus.
- [ ] Do not make `Application` a heavy service-locator singleton.
- [ ] Do not make `ProgressTracker` a second canonical owner of job state.
- [ ] Do not expose `BS::priority_t` in public API after the JobSystem cleanup.
- [ ] Do not add hierarchical subtasks yet.
- [ ] Do not add Python execution features here.
- [ ] Do not add persistent notification/job history on disk yet.

---

## 10. Notes for Copilot

When implementing this file:
- prefer small PR-sized commits
- do not rename working low-level event code unless necessary
- compile after every slice
- do not introduce framework-style abstractions unless a concrete T04 test requires them
- if a choice exists between “clever generic system” and “small explicit class”, choose the small explicit class
