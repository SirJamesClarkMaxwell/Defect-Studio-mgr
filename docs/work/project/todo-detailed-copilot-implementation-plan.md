# TODO Detailed – T04 Core Implementation Plan

## 0. Locked architectural decisions

These decisions are final for T04. Do not revisit without a formal ADR.

### 0.1 Two separate event systems

| System               | Purpose                                                   | Location                    |
| -------------------- | --------------------------------------------------------- | --------------------------- |
| Platform events      | OS/input events (keyboard, mouse, window)                 | `src/Core/Platform/Events/` |
| Subsystem bus events | Application-level communication between subsystems and UI | `src/Core/Events/`          |

Platform events use the Hazel-style macro system (`DS_EVENT_CLASS_TYPE`, `DS_EVENT_CLASS_CATEGORY`) and `EventVariant` for queue storage. This system stays untouched — only moved to a new path.

Subsystem bus events use a minimal `Event` base class (no macros, no category flags) and are dispatched through `EventBus`. These are two fully unrelated class hierarchies.

### 0.2 Ownership boundaries

```
Application
├── EventBus                  ← application-lifetime communication bus
├── Notifier                  ← emits NotificationEvents, keeps append-only session history
└── CoreLayer
    ├── JobSystem              ← worker thread pool, job lifecycle
    └── ProgressTracker        ← read model for job/task progress UI
```

`Application` owns `EventBus` and `Notifier` because their lifetime equals the application lifetime and `ProcessQueue()` belongs in the main loop. `CoreLayer` retains `JobSystem` and `ProgressTracker` because they are runtime services that benefit from layer-managed lifecycle.

### 0.3 Canonical directory structure

```
src/
  Core/
    Events/                               ← subsystem bus (NEW)
      Event.hpp                           ← minimal base: handled, stopPropagation
      EventPriority.hpp                   ← enum: Highest → Lowest
      SubscriptionHandle.hpp/.cpp         ← RAII subscription lifetime
      EventReceiver.hpp                   ← base for UI panels and subsystems
      EventBus.hpp/.cpp                   ← rewritten bus with queue + priority
    Platform/
      Events/                             ← OS/input events (MOVED)
        PlatformEventBase.hpp             ← was Core/Events/EventBase.hpp
        PlatformEvent.hpp                 ← was Core/Events/Event.hpp (EventVariant)
        WindowEvents.hpp
        KeyboardEvents.hpp
        MouseEvents.hpp
        TouchpadEvents.hpp
    EventQueue.hpp/.cpp                   ← stays, references PlatformEvent.hpp
    Notifications/
      Notification.hpp                    ← NotificationSeverity, NotificationCategory, Notification struct
      Notifier.hpp/.cpp                   ← emits + append-only session history
      NotificationCenter.hpp/.cpp         ← live hub, fan-out to UI listeners
      NotificationHistory.hpp/.cpp        ← session-scoped filtering + inspection
    Jobs/
      IJob.hpp
      JobContext.hpp/.cpp
      JobRecord.hpp                       ← internal mutable state (private to JobSystem)
      JobSnapshot.hpp                     ← immutable UI-facing read model
      JobLogEntry.hpp
      JobStatus.hpp
      JobTypes.hpp                        ← JobId, JobCancelledException
      JobSystem.hpp/.cpp                  ← rewritten: owns pool, queue, records
      TestJobs/
        SleepJob.hpp/.cpp
        FailingJob.hpp/.cpp
    Threading/
      ThreadAffinity.hpp/.cpp             ← main thread id, ASSERT_MAIN_THREAD
    Diagnostics/
      StructuredError.hpp                 ← shared error type: category, code, technicalContext, userMessage, source
    Capabilities/
      Capability.hpp                      ← enum or string-keyed capability identifiers + ErrorCategory enum
      CapabilityRegistry.hpp/.cpp         ← named capability registration, read-only after startup
      CapabilityService.hpp/.cpp          ← IsAvailable(), Require() — application-facing query API
  Events/                                 ← domain bus event types
    ApplicationEvents.hpp                 ← AppStartedEvent, AppClosingEvent
    ProjectEvents.hpp                     ← ProjectOpenedEvent, ProjectClosedEvent
    JobEvents.hpp                         ← JobStartedEvent, JobCompletedEvent, JobFailedEvent, JobCancelledEvent
    NotificationEvents.hpp                ← NotificationEvent
  App/
    Application.hpp/.cpp                  ← adds EventBus + Notifier ownership, ProcessQueue in loop
    ConfigManager.hpp/.cpp
  Debug/
    DebugLayer.hpp/.cpp
    Panels/
      LogPanel.hpp/.cpp
      JobMonitorPanel.hpp/.cpp
      NotificationHistoryPanel.hpp/.cpp
      UIStyleEditor.hpp/.cpp
      CapabilitySnapshotPanel.hpp/.cpp    ← read-only capability registry view (DebugLayer only)
```

### 0.4 JobSystem naming

The class stays named `JobSystem`. The internal implementation is rewritten from scratch per the MVP spec. The old BS-wrapper internals are removed entirely. `CoreLayer::GetJobSystem()` stays as-is — no change to the CoreLayer public API.

### 0.5 EventBus upgrade contract

The current `EventBus.hpp` is a prototype. It is replaced in full. The new EventBus:

- returns `SubscriptionHandle` (RAII) from `Subscribe<TEvent>()`
- supports `EventPriority` (Highest → Lowest); listeners sorted with `stable_sort` per priority
- supports deferred removal via `pendingRemoval` flag (safe unsubscribe during active dispatch)
- has a thread-safe internal queue: `Queue<TEvent>()` + `ProcessQueue()` (main thread only)
- is non-copyable, non-movable
- provides: `Publish`, `PublishNew<TEvent>(args...)`, `ClearAllListeners`, `ClearQueue`
- provides diagnostics: `HasSubscribers<T>`, `GetSubscriberCount<T>`, `GetTotalSubscriberCount`, `GetQueuedEventCount`, `IsDispatching`

### 0.6 Notification model

Three-class design:

| Class                 | Role                                                                                               |
| --------------------- | -------------------------------------------------------------------------------------------------- |
| `Notifier`            | Emit `NotificationEvent` to `EventBus` + keep append-only in-memory session history                |
| `NotificationCenter`  | Live UI hub — subscribes to `NotificationEvent` on `EventBus`, fans out to registered UI listeners |
| `NotificationHistory` | Session-scoped read model with filtering by severity, category, source, time range                 |

`Notifier` is owned by `Application`. `NotificationCenter` and `NotificationHistory` are owned by the diagnostics UI layer.

### 0.7 CapabilityRegistry / CapabilityService

`CapabilityRegistry` is a T04 subsystem. It is **not** deferred to later tasks.

| Class                | Role                                                                                                                    |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `CapabilityRegistry` | Central registry of named capabilities; populated at startup from build-time flags, runtime detection, and policy rules |
| `CapabilityService`  | Application-facing query API; answers `IsAvailable(capability)`, `Require(capability)`                                  |

Capabilities are categorized as:
- **Build-time** — compiled in or out (e.g. `PythonBridge`, `TracyProfiling`)
- **Runtime-detected** — checked at startup (e.g. Python venv present, GPU compute available)
- **Policy/access** — controlled by configuration or future licensing logic (e.g. `AdvancedAnalysis`)

The registry is read-only after startup. No runtime mutation outside of the initialization phase. `DebugLayer` may display a capability snapshot; it must not write to the registry.

`CapabilityService` is owned by `Application` and initialized before any other subsystem.

### 0.8 Structured Error model

`StructuredError` is the shared runtime error type. It is **not** just a string. Every subsystem that produces a failure uses it.

Required fields:
- `ErrorCategory category` — enum: `Config`, `Job`, `IO`, `Python`, `Validation`, `Internal`
- `std::string code` — machine-readable identifier (e.g. `"config.missing_file"`)
- `std::string technicalContext` — for logs and developers
- `std::string userMessage` — for UI display
- `std::string source` — subsystem or component name

`StructuredError` feeds:
- `Notifier` (runtime failures → non-blocking notification)
- Blocking popup policy (pre-execution validation failures only)

Rule: background job failures → `Notifier.Error(...)`. Pre-execution validation failures (e.g. missing required file before a job is submitted) → blocking modal popup via `Application`. These two paths are distinct and must not be conflated.

### 0.9 ProjectWorkspace as runtime source of truth

T04 establishes the rule; full implementation is deferred to T10/T11.

**Rule:** `ProjectWorkspace` is the single runtime source of truth for all open project and domain state. `EditorContext` and all UI panels are read-only observers — they hold references or snapshots, never ownership. No subsystem initialized in T04 stores project-domain objects directly; they operate on events, ids, and snapshots.

This rule must be visible in the T04 codebase as a documented constraint even before `ProjectWorkspace` itself exists. A `TODO(T10): ProjectWorkspace` comment in `Application` is sufficient for now.

### 0.10 Library usage — entt

The main project TODO lists `entt` under T04 libraries with the annotation `(EventBus)`. This is a naming collision. Clarification:

- **`entt`** is an Entity Component System library. It is used for the scene/data model starting from T08. It is **not** used for `EventBus`.
- **`EventBus`** in T04 is a handwritten subsystem bus (see 0.5). It does not use entt internals.
- `entt::dispatcher` exists but is not used here — our `EventBus` is purpose-built for the architecture described in this document.

---

## 1. File change manifest

### New files

`Core/Events/`:
- `Event.hpp`
- `EventPriority.hpp`
- `SubscriptionHandle.hpp`, `SubscriptionHandle.cpp`
- `EventReceiver.hpp`
- `EventBus.hpp`, `EventBus.cpp`

`Core/Platform/Events/` (content moved, not new):
- `PlatformEventBase.hpp` ← `Core/Events/EventBase.hpp`
- `PlatformEvent.hpp` ← `Core/Events/Event.hpp`
- `WindowEvents.hpp`, `KeyboardEvents.hpp`, `MouseEvents.hpp`, `TouchpadEvents.hpp`

`Core/Notifications/`:
- `Notification.hpp`
- `Notifier.hpp`, `Notifier.cpp`
- `NotificationCenter.hpp`, `NotificationCenter.cpp`
- `NotificationHistory.hpp`, `NotificationHistory.cpp`

`Core/Jobs/`:
- `IJob.hpp`
- `JobContext.hpp`, `JobContext.cpp`
- `JobRecord.hpp`
- `JobSnapshot.hpp`
- `JobLogEntry.hpp`
- `JobStatus.hpp`
- `JobTypes.hpp`
- `JobSystem.hpp`, `JobSystem.cpp` (rewritten)
- `TestJobs/SleepJob.hpp`, `SleepJob.cpp`
- `TestJobs/FailingJob.hpp`, `FailingJob.cpp`

`Core/Threading/`:
- `ThreadAffinity.hpp`, `ThreadAffinity.cpp`

`Core/Diagnostics/`:
- `StructuredError.hpp`

`Core/Capabilities/`:
- `Capability.hpp`
- `CapabilityRegistry.hpp`, `CapabilityRegistry.cpp`
- `CapabilityService.hpp`, `CapabilityService.cpp`

`Events/` (domain bus event types):
- `ApplicationEvents.hpp`
- `ProjectEvents.hpp`
- `JobEvents.hpp`
- `NotificationEvents.hpp`

`Debug/Panels/`:
- `LogPanel.hpp/.cpp`
- `JobMonitorPanel.hpp/.cpp`
- `NotificationHistoryPanel.hpp/.cpp`
- `UIStyleEditor.hpp/.cpp`

### Modified files

- `Core/CoreLayer.hpp/.cpp` — remove `EventBus` field and `GetEventBus()`; keep `JobSystem` + `ProgressTracker`
- `Core/ProgressTracker.hpp/.cpp` — extend `ProgressEntry` model, add snapshot API
- `Core/EventQueue.hpp/.cpp` — update include path to `Core/Platform/Events/PlatformEvent.hpp`
- `App/Application.hpp/.cpp` — add `EventBus`, `Notifier` fields; `ProcessQueue()` in main loop
- `App/ConfigManager.hpp/.cpp` — finalize ownership boundary
- `Debug/DebugLayer.hpp/.cpp` — wire new panels

### Deleted / superseded

- `Core/Events/EventBus.hpp` (old prototype) — replaced by new `Core/Events/EventBus.hpp/.cpp`
- `Core/Events/EventBase.hpp` — moved to `Core/Platform/Events/PlatformEventBase.hpp`
- `Core/Events/Event.hpp` (EventVariant) — moved to `Core/Platform/Events/PlatformEvent.hpp`

---

## 2. Process model

### Phase 0 — Lock invariants

**One-time. Done before any code is written.**

This is section 0 of this document. All architectural decisions listed there are locked. No implementation work begins until section 0 is agreed upon and stable. This is the only phase that correctly precedes everything else.

**Exit condition:** Section 0 is signed off. No open questions about ownership, directory structure, naming, or threading contracts.

---

### Per-slice cycle — applies to every Slice A through J

Each slice follows the same internal cycle. Do not skip steps. Do not batch steps across slices.

```
1. Contract sketch
   Write down the public API surface for this slice only — not the whole system.
   A header with method signatures and a two-line comment per method is enough.
   This takes 15–30 minutes, not days.

2. Tests first
   Write failing tests against the contract sketch before writing any implementation.
   Tests must compile. They are expected to fail at this point.

3. Implementation
   Write the minimum code to make the tests pass.
   Do not implement things not covered by a test in this slice.

4. Local documentation
   Add doc-comments to public headers.
   Add a one-paragraph entry to the relevant mdBook page (or create a stub if the page does not exist yet).
   Do not write full reference documentation yet — that comes in Phase N-1.

5. Gate
   All tests for this slice must pass.
   The project must compile in Debug and Release.
   Only then move to the next slice.
```

**Why this order matters:** The contract sketch for a slice is small enough that it rarely needs revision after implementation. If it does need revision, updating it costs minutes, not days. Writing full multi-page documentation before any code exists means rewriting documentation when the code reveals something the paper contract missed.

---

### Test catalog

All planned tests across all slices are listed here as a single reference. Tests are written per-slice (step 2 of the cycle above), not as a single pre-implementation block.

**Application lifecycle (Slice A):**
- [x] `Application` boots, runs one frame, and shuts down without assertions
- [x] `Shutdown()` called twice is a no-op on the second call
- [x] Layer `OnAttach` → `OnUpdate` → `OnDetach` ordering is correct
- [x] `LayerStack` delivers events to layers in the correct order

**EventBus (Slice B):**
- [x] `Subscribe` returns a valid `SubscriptionHandle`
- [x] `SubscriptionHandle` destructor triggers `Unsubscribe`
- [x] Move-assigning `SubscriptionHandle` transfers ownership correctly
- [x] `Publish` delivers to all active subscribers
- [x] Subscribers are invoked in priority order (Highest first)
- [x] Equal-priority subscribers are invoked in subscription order (`stable_sort`)
- [x] `stopPropagation = true` stops delivery to remaining subscribers
- [x] `handled = true` does not stop propagation on its own
- [x] Unsubscribe during dispatch defers removal; removed listener is not invoked in the current cycle
- [x] Subscriber added during dispatch is not invoked in the same cycle
- [x] `ClearAllListeners` during dispatch defers all removals
- [x] `Queue<TEvent>()` is callable from a non-main thread without data races
- [x] `ProcessQueue()` delivers all queued events in submission order
- [x] `ProcessQueue()` with empty queue is a no-op
- [x] `IsDispatching()` returns `true` only inside a dispatch call
- [x] `GetQueuedEventCount()` reflects queued but unprocessed events

**JobSystem (Slice C):**
- [ ] `Submit` returns a valid `JobId`
- [ ] Submitted job transitions: `Queued → Running → Completed`
- [ ] `Failed` job stores exception message in snapshot
- [ ] `Cancelled` job is marked `Cancelled` (cooperative — not forced)
- [ ] `GetJob(id)` returns a populated snapshot with correct timestamps
- [ ] `GetActiveJobs()` includes `Running` and `Queued`
- [ ] `GetFinishedJobs()` includes `Completed`, `Failed`, and `Cancelled`
- [ ] `GetLogs(id)` returns log entries in submission order
- [ ] `SleepJob` reports progress increments and supports cancellation
- [ ] `FailingJob` produces `Failed` status with the expected error message
- [ ] `Shutdown()` waits for in-flight jobs to finish cleanly
- [ ] `Shutdown()` is idempotent (second call is a no-op)
- [ ] Concurrent `Submit` calls from multiple threads do not corrupt job records

**ProgressTracker (Slice D):**
- [ ] Tracker entry is created when a job is submitted
- [ ] Progress updates appear in snapshot after `SetProgress()` call
- [ ] Finishing a job marks the entry as completed in the snapshot
- [ ] Failure and cancellation are visible in snapshot with correct status
- [ ] Concurrent snapshot reads are safe while job is running

**ConfigManager (Slice E):**
- [ ] Default document is created when config file is missing
- [ ] YAML save/load roundtrip preserves all fields
- [ ] Invalid config file returns `ConfigLoadResult` with failure reason and falls back to default
- [ ] `ui_settings.yaml` save/load roundtrip preserves style settings
- [ ] `ConfigManager` is the sole writer — no other code touches config files directly

**Notification system (Slice F):**
- [ ] `Notifier.Info(...)` appends to history and publishes `NotificationEvent`
- [ ] `Notifier.Error(...)` uses `NotificationSeverity::Error`
- [ ] `Notifier.Success(...)` uses `NotificationSeverity::Success`
- [ ] `Notifier.Debug(...)` uses `NotificationSeverity::Debug`
- [ ] `NotificationHistory` filters by severity level
- [ ] `NotificationHistory` filters by `NotificationCategory`
- [ ] `NotificationHistory` filters by source string
- [ ] `NotificationHistory` filters by time range
- [ ] `NotificationCenter` receives `NotificationEvent` via `EventBus`
- [ ] History is in-memory only (no filesystem interaction)

**Thread affinity (Slice G):**
- [ ] `ASSERT_MAIN_THREAD` fires on a known worker thread in Debug builds
- [ ] `ProcessQueue()` called from a worker thread triggers the assertion
- [ ] `ProgressTracker` snapshot write path asserts main thread

**UI style system (Slice H):**
- [ ] Style preset save/load roundtrip via `ConfigManager` preserves selected preset name
- [ ] Loading a preset on startup restores the correct ImGui style before first render

**CapabilityRegistry / CapabilityService (Slice I):**
- [ ] Build-time capability registered at startup is queryable via `IsAvailable()`
- [ ] Runtime-detected capability registered after detection is queryable
- [ ] Policy capability with `false` value returns `false` from `IsAvailable()`
- [ ] `Require()` throws `CapabilityNotAvailableException` for unavailable capability
- [ ] Registry is read-only after `LockAfterStartup()` — late registration is rejected
- [ ] Unknown capability identifier returns `false`, not undefined behavior

**StructuredError + blocking popup (Slice J):**
- [ ] `StructuredError` constructed with all required fields holds values correctly
- [ ] `ToNotification()` produces a `Notification` with correct severity and category
- [ ] Job failure path produces `StructuredError{ErrorCategory::Job}`
- [ ] Config load failure path produces `StructuredError{ErrorCategory::Config}`
- [ ] Pre-execution validation path routes to blocking popup, not `Notifier`
- [ ] Runtime failure path routes to `Notifier`, not blocking popup

**Integration (final):**
- [ ] `Application` boots with all slices integrated; no lifecycle assertions
- [ ] `SleepJob` submitted, progresses, completes — `ProgressTracker` snapshot reflects it, `NotificationCenter` receives `JobCompletedEvent`
- [ ] `FailingJob` submitted — snapshot shows `Failed`, `Notifier` emits error notification
- [ ] `CapabilityService.Require()` for unavailable capability triggers `ShowBlockingError` on main thread

---

### Phase N-1 — Documentation consolidation

**Runs after all Slices A → J compile and all tests pass.**

At this point the API is stable and examples can be verified to compile. Write full reference documentation now, not before.

**mdBook pages to write or complete:**
- `event-system-api.md` — full EventBus API reference, `SubscriptionHandle` lifecycle, `EventReceiver` usage guide, `Queue` / `ProcessQueue` threading contract, platform vs subsystem event distinction
- `job-system-api.md` — full `JobSystem` API, `IJob` / `JobContext` / `JobSnapshot` contracts, job state machine diagram, exception boundary rule, threading model
- `notification-api.md` — `Notifier` / `NotificationCenter` / `NotificationHistory` data flow diagram, routing rules
- `capability-system-api.md` — capability categories, initialization contract, `CapabilityService` query API, read-only-after-startup rule
- `structured-error-api.md` — `StructuredError` fields, `ErrorCategory` enum, routing decision table (job runtime → Notifier, pre-execution validation → blocking popup)
- Step-by-step: adding a new platform (OS/input) event type
- Step-by-step: adding a new subsystem bus event type
- Step-by-step: implementing `IJob`
- Example: `EventReceiver` usage in a UI panel — with working, compiled code
- Example: routing a job failure through `StructuredError` → `Notifier`
- Example: `CapabilityService` guard before feature execution

**Exit condition:** Every example in the documentation compiles against the actual implementation. No placeholder examples.

---

### Phase N — Full review

**Runs after Phase N-1.**

**Checklist:**
- [ ] API names match documentation
- [ ] Platform events and subsystem `EventBus` are fully separate class hierarchies
- [ ] `Application` owns `EventBus`, `Notifier`, `CapabilityService`; `CoreLayer` owns `JobSystem`, `ProgressTracker`
- [ ] `CapabilityService` is initialized before any other subsystem; registry locked before main loop
- [ ] `StructuredError` is used by all subsystems that produce failures; no raw string errors
- [ ] Job failure path: `StructuredError` → `Notifier.Error()` (non-blocking)
- [ ] Pre-execution validation failure path: `StructuredError` → `ShowBlockingError` (blocking)
- [ ] `ProcessQueue()` is called exactly once per frame on the main thread
- [ ] No worker thread calls `EventBus::Publish()` directly — only `Queue()`
- [ ] No worker thread writes project-visible UI or runtime state directly
- [ ] `SubscriptionHandle` RAII verified in all tested scenarios
- [ ] `JobSystem::Shutdown()` waits for in-flight jobs cleanly
- [ ] `JobRecord` is not exposed outside `JobSystem`; UI reads `JobSnapshot` only
- [ ] Every documentation example compiles without modification
- [ ] No backend-specific types leak through `JobSystem` public API

---

## 3. Slice breakdown for Copilot

Slices are meant to be small enough to compile and test after each one. Do not start a later slice before the previous one compiles and its tests pass.

---

### Slice A — Stabilize Application lifecycle + platform event path rename

1. Write lifecycle tests for `Application` (boot → main loop → shutdown ordering)
2. Make `Application::Shutdown()` safe and idempotent
3. Move `Core/Events/EventBase.hpp` → `Core/Platform/Events/PlatformEventBase.hpp`
4. Move `Core/Events/Event.hpp` (EventVariant) → `Core/Platform/Events/PlatformEvent.hpp`
5. Move `WindowEvents.hpp`, `KeyboardEvents.hpp`, `MouseEvents.hpp`, `TouchpadEvents.hpp` to `Core/Platform/Events/`
6. Update all includes that reference old paths
7. Verify `Core/EventQueue.hpp/.cpp` compiles with new include path
8. Verify layer ordering tests pass

**Files touched:** `App/Application.*`, `Core/Platform/Events/*`, `Core/EventQueue.*`, all files that included old paths

---

### Slice B — New subsystem EventBus

1. Add `Core/Events/EventPriority.hpp` — `EventPriority` enum (Highest → Lowest)
2. Add `Core/Events/Event.hpp` — minimal base (`bool handled`, `bool stopPropagation`, no macros)
3. Add `Core/Events/SubscriptionHandle.hpp/.cpp` — move-only RAII handle; `Reset()`, `IsValid()`
4. Add `Core/Events/EventReceiver.hpp` — `AddSubscription`, `ClearSubscriptions`, `SetEnabled`, `IsEnabled`
5. Rewrite `Core/Events/EventBus.hpp/.cpp` per spec (priority ordering, deferred removal, internal queue, diagnostics)
6. Add free function `SubscribeMember<TObject, TEvent>` in `EventBus.hpp`
7. Add `Events/ApplicationEvents.hpp` — `AppStartedEvent`, `AppClosingEvent`
8. Add `Events/ProjectEvents.hpp` — `ProjectOpenedEvent`, `ProjectClosedEvent`
9. Move `EventBus` ownership from `CoreLayer` to `Application`; remove `GetEventBus()` from `CoreLayer`
10. Add `m_EventBus.ProcessQueue()` to the `Application` main loop
11. Write Phase 1 EventBus tests

**Files touched:** `Core/Events/*`, `Events/ApplicationEvents.hpp`, `Events/ProjectEvents.hpp`, `App/Application.*`, `Core/CoreLayer.*`

---

### Slice C — JobSystem rewrite

1. Add `Core/Jobs/JobTypes.hpp` — `using JobId = std::uint64_t`; `JobCancelledException`
2. Add `Core/Jobs/JobStatus.hpp` — `enum class JobStatus { Queued, Running, Completed, Failed, Cancelled }`
3. Add `Core/Jobs/JobLogEntry.hpp` — log level enum, message, timestamp
4. Add `Core/Jobs/IJob.hpp` — `GetName()`, `GetType()`, `Execute(JobContext&)`
5. Add `Core/Jobs/JobContext.hpp/.cpp` — `SetProgress`, `SetStage`, `SetMessage`, `LogInfo/Warning/Error/Debug`, `IsCancellationRequested`, `ThrowIfCancellationRequested`
6. Add `Core/Jobs/JobRecord.hpp` — full mutable runtime state (internal, not exported)
7. Add `Core/Jobs/JobSnapshot.hpp` — immutable copy for UI reads; matches `JobRecord` fields
8. Rewrite `Core/Jobs/JobSystem.hpp/.cpp`:
   - `BS::thread_pool` is the **private** executor backend — no BS:: type appears in the public API
   - `JobSystem` submits work to the pool via `BS::thread_pool::submit_task()`; no custom thread or queue management needed
   - `JobRecord` is created by `JobSystem` before submission and updated by the worker through `JobContext`; ownership stays inside `JobSystem`
   - Cancellation token lives in `JobRecord` and is checked cooperatively inside `IJob::Execute()` — BS::thread_pool does not provide this; it remains our concern
   - exception boundary per section 9 of the MVP spec wraps the `Execute()` call inside the submitted lambda
   - `Submit(shared_ptr<IJob>)` → `JobId`
   - `RequestCancel(JobId)` → `bool`
   - `GetJob`, `GetAllJobs`, `GetActiveJobs`, `GetFinishedJobs`
   - `GetLogs(JobId)` → `vector<JobLogEntry>`
   - `Shutdown()` — idempotent, calls `BS::thread_pool::wait()` then destroys the pool
9. Add `Events/JobEvents.hpp` — `JobStartedEvent`, `JobCompletedEvent`, `JobFailedEvent`, `JobCancelledEvent`
10. Wire job lifecycle events: worker threads call `EventBus::Queue()` (never `Publish()`)
11. Add `Core/Jobs/TestJobs/SleepJob.hpp/.cpp` — periodic progress updates, cancellation support
12. Add `Core/Jobs/TestJobs/FailingJob.hpp/.cpp` — throws `std::runtime_error`, appears as `Failed`
13. Write Phase 1 JobSystem tests

**Files touched:** `Core/Jobs/*`, `Events/JobEvents.hpp`, `Core/CoreLayer.*`

---

### Slice D — ProgressTracker upgrade

1. Extend `ProgressEntry` model:
   - `JobId id`
   - `std::string source`
   - `std::string label`
   - `JobStatus status`
   - `float totalWork`, `float completedWork`
   - `std::string currentStage`, `std::string currentMessage`
   - `std::chrono::system_clock::time_point createdAt`, `startedAt`, `finishedAt`
   - `std::string errorSummary`
   - `bool finished` (derived from status)
2. Add session-stable snapshot API — `GetSnapshot(JobId)`, `GetAllSnapshots()`, safe for UI reads without holding internal locks
3. Subscribe `ProgressTracker` to `JobEvents` on `EventBus` (read from `Application`-owned bus via reference passed at construction)
4. Rule: `JobSystem` owns canonical live state; `ProgressTracker` is the derived read model
5. Write Phase 1 ProgressTracker tests

**Files touched:** `Core/ProgressTracker.*`, integration wiring in `App/Application.*` or `Core/CoreLayer.*`

---

### Slice E — Config boundary

1. Finalize `ConfigManager` public header: `ConfigDocument`, `ConfigLoadResult`, path helper methods
2. Make `ConfigManager` the sole owner of config file IO: `default.yaml`, `ui_settings.yaml`
3. Add fallback-to-default flow: missing or invalid config file returns `ConfigLoadResult` with failure reason and falls back to a default document
4. Keep JSON load support as migration/import compatibility only (not the primary long-term format)
5. Connect `Application` startup to config loading — minimal wiring, no UI entanglement
6. Write Phase 1 ConfigManager tests

**Files touched:** `App/ConfigManager.*`, `App/Application.*`

---

### Slice F — Notification system

1. Add `Core/Notifications/Notification.hpp`:
   - `enum class NotificationSeverity { Debug, Info, Success, Warning, Error }`
   - `enum class NotificationCategory { General, Project, Import, Export, JobSystem, Parsing, UI, Scripting, Rendering }`
   - `struct Notification` — severity, category, title, message, source, timestamp, timeoutMs
2. Add `Events/NotificationEvents.hpp` — `NotificationEvent` wrapping `Notification`
3. Add `Core/Notifications/Notifier.hpp/.cpp`:
   - constructor takes `EventBus&`
   - `Notify(Notification)` — stamps timestamp if missing, appends to history, publishes `NotificationEvent`
   - convenience: `Debug`, `Info`, `Success`, `Warning`, `Error` (message, category, source)
   - `GetHistory()` → `const vector<Notification>&`
   - `ClearHistory()`
4. Add `Core/Notifications/NotificationCenter.hpp/.cpp`:
   - subscribes to `NotificationEvent` on `EventBus` (RAII via `SubscriptionHandle`)
   - fan-out to registered UI listener callbacks
   - `RegisterListener` / `UnregisterListener`
5. Add `Core/Notifications/NotificationHistory.hpp/.cpp`:
   - constructed from or fed by `Notifier` history
   - filtering: `FilterBySeverity`, `FilterByCategory`, `FilterBySource`, `FilterByTimeRange`
   - returns `vector<Notification>` snapshots (copies, safe for UI reads)
6. Move `Notifier` ownership to `Application` (constructed with `m_EventBus`)
7. Route `JobSystem` failures to `Notifier`: on `JobFailedEvent`, emit `Notifier.Error(...)` with `NotificationCategory::JobSystem`
8. Route `ConfigManager` load failures to `Notifier`
9. Write Phase 1 Notification tests

**Files touched:** `Core/Notifications/*`, `Events/NotificationEvents.hpp`, `App/Application.*`, `Core/Jobs/JobSystem.cpp`, `App/ConfigManager.cpp`

---

### Slice G — Thread affinity + main-thread commit rule

1. Add `Core/Threading/ThreadAffinity.hpp/.cpp` — stores main thread id at `Application` startup
2. Add `ASSERT_MAIN_THREAD` macro (active in Debug builds)
3. Assert in `EventBus::ProcessQueue()` — must be called on main thread
4. Assert in `ProgressTracker` snapshot write path
5. Assert in `NotificationCenter` fan-out path
6. Document the rule in code comments and mdbook: worker threads compute and `Queue` events; only the main thread commits project-visible state
7. Write a safe integration test for the main-thread commit path

**Files touched:** `Core/Threading/ThreadAffinity.*`, `Core/Events/EventBus.cpp`, `Core/ProgressTracker.cpp`, `Core/Notifications/NotificationCenter.cpp`, `App/Application.cpp`

---

### Slice H — Diagnostics UI skeleton + UI style system

1. Add `Debug/Panels/UIStyleEditor.hpp/.cpp`:
   - ImGui style customization panel
   - Hazel-inspired preset themes: Dark, Light, Classic — selectable at runtime
   - `SaveStyle(path)` / `LoadStyle(path)` — persisted to `ui_settings.yaml` via `ConfigManager`
   - User style overrides layered on top of the selected preset
2. Add user settings persistence:
   - `ConfigManager` stores and loads the active style preset name and per-field overrides
   - Restore selected preset on application startup before first render
3. Add `Debug/Panels/LogPanel.hpp/.cpp` — reads from `Notifier` history; filters by severity; auto-scroll with toggle
4. Add `Debug/Panels/JobMonitorPanel.hpp/.cpp` — backed by `ProgressTracker` snapshots; shows status, progress bar, current stage, timestamps, cancel button
5. Add `Debug/Panels/NotificationHistoryPanel.hpp/.cpp` — backed by `NotificationHistory`; shows all session notifications with severity filter and source filter
6. Add `Debug/Panels/CapabilitySnapshotPanel.hpp/.cpp` — read-only table of registered capabilities, their category, and current availability status (DebugLayer only)
7. Keep `DebugLayer` privileged and read-mostly — no mutation of runtime state through debug panels
8. Write tests: debug layer activation policy, panel paths reachable from app shell, style preset save/load roundtrip

**Files touched:** `Debug/DebugLayer.*`, `Debug/Panels/*`, `App/ConfigManager.*`

---

### Slice I — CapabilityRegistry / CapabilityService

1. Add `Core/Capabilities/Capability.hpp`:
   - `enum class CapabilityCategory { BuildTime, RuntimeDetected, Policy }`
   - `struct CapabilityEntry` — name, category, available flag, description
   - Named capability string constants: `Capabilities::PythonBridge`, `Capabilities::TracyProfiling`, `Capabilities::GpuCompute`, `Capabilities::AdvancedAnalysis`
2. Add `Core/Capabilities/CapabilityRegistry.hpp/.cpp`:
   - `Register(name, category, available, description)` — adds entry; asserts if called after startup lock
   - `LockAfterStartup()` — called by `Application` after init phase; blocks further registration
   - `GetAll()` → `vector<CapabilityEntry>` — for DebugLayer snapshot panel
   - Internal storage: `unordered_map<string, CapabilityEntry>`
3. Add `Core/Capabilities/CapabilityService.hpp/.cpp`:
   - constructor takes `CapabilityRegistry&`
   - `IsAvailable(string_view name)` → `bool`
   - `Require(string_view name)` → throws `CapabilityNotAvailableException` if not available
   - Used as a guard before feature execution, not as a runtime toggle
4. Move `CapabilityService` ownership to `Application`; initialize it before `CoreLayer::OnAttach()`
5. Register build-time capabilities in `Application` using compile-time defines from CMake:
   - `DS_PYTHON_BRIDGE` → `Capabilities::PythonBridge`
   - `DS_TRACY_PROFILING` → `Capabilities::TracyProfiling`
6. Register `GpuCompute` capability after GL context creation (stubbed for now, always true if GL 4.3)
7. Call `CapabilityRegistry::LockAfterStartup()` before entering the main loop
8. Write Phase 1 CapabilityRegistry / CapabilityService tests

**Files touched:** `Core/Capabilities/*`, `App/Application.*`

---

### Slice J — StructuredError + blocking popup policy

1. Add `Core/Diagnostics/StructuredError.hpp`:
   - `enum class ErrorCategory { Config, Job, IO, Python, Validation, Internal }`
   - `struct StructuredError` — `ErrorCategory category`, `string code`, `string technicalContext`, `string userMessage`, `string source`
   - Free function `ToNotification(const StructuredError&)` → `Notification` — maps `ErrorCategory` to `NotificationCategory` and severity
2. Add a blocking popup policy helper in `App/Application`:
   - `Application::ShowBlockingError(const StructuredError&)` — presents an ImGui modal popup on the main thread
   - Must be called on the main thread; asserts via `ASSERT_MAIN_THREAD`
   - Used only for pre-execution validation failures (e.g. missing required file before job submission, unavailable required capability)
   - Does **not** use `Notifier`; these are synchronous, blocking user decisions
3. Route `JobSystem` failures through `StructuredError` → `ToNotification()` → `Notifier.Error()`:
   - Worker catches exception, fills `StructuredError{ErrorCategory::Job, ...}`, queues a `JobFailedEvent` with the error
   - Main thread receives event, calls `ToNotification()`, passes result to `Notifier`
4. Route `ConfigManager` load failures through `StructuredError` → `Notifier.Error()` for non-critical failures (fallback succeeded), or `ShowBlockingError` for critical startup failures
5. Write Phase 1 StructuredError and blocking popup policy tests

**Files touched:** `Core/Diagnostics/StructuredError.hpp`, `App/Application.*`, `Core/Jobs/JobSystem.cpp`, `App/ConfigManager.cpp`

---

## 4. TDD order

1. [x] Application lifecycle ordering tests (Slice A)
2. [x] EventBus behavior tests — subscribe, publish, priority, propagation, queue (Slice B)
3. [x] JobSystem state transition tests — lifecycle, cancellation, exception boundary (Slice C)
4. [x] JobSystem concurrency tests — concurrent submit, concurrent cancel (Slice C)
5. [x] ProgressTracker snapshot tests (Slice D)
6. [ ] ConfigManager load/save/fallback tests (Slice E)
7. [ ] Notification emit, history, filtering tests (Slice F)
8. [ ] Thread affinity assertion tests (Slice G)
9. [ ] UI style preset save/load roundtrip tests (Slice H)
10. [ ] CapabilityRegistry / CapabilityService tests — registration, query, lock, unknown key (Slice I)
11. [ ] StructuredError construction and conversion tests (Slice J)
12. [ ] Blocking popup policy routing tests — pre-execution vs runtime failure paths (Slice J)
13. [ ] Final integration tests: Application shell + EventBus + JobSystem + Notifier + CapabilityService end-to-end

---

## 5. Acceptance criteria

T04 is done when all of the following are true:

- [ ] `Application` boots and shuts down safely with no lifecycle ambiguity.
- [ ] `LayerStack` update and event ordering is covered by tests.
- [ ] Platform events (`Core/Platform/Events/`) and subsystem `EventBus` (`Core/Events/`) are fully separate class hierarchies with no shared base class.
- [ ] `Application` owns `EventBus`, `Notifier`, and `CapabilityService`; `CoreLayer` owns `JobSystem` and `ProgressTracker`.
- [ ] `CapabilityService` is initialized before any other subsystem; `CapabilityRegistry` is locked before the main loop starts.
- [ ] `CapabilityService::IsAvailable()` correctly reflects build-time flags and runtime detection results.
- [ ] `StructuredError` is the shared error type for all subsystem failures; raw string errors are not used.
- [ ] Job runtime failures produce `StructuredError` → `Notifier.Error()` (non-blocking).
- [ ] Pre-execution validation failures produce `StructuredError` → `Application::ShowBlockingError()` (blocking modal).
- [ ] `EventBus` provides RAII subscriptions, priority-ordered delivery, deferred removal during dispatch, and a thread-safe internal queue.
- [ ] `Application::ProcessQueue()` is called exactly once per frame on the main thread.
- [ ] `JobSystem` uses `BS::thread_pool` as its private executor backend; no BS:: type appears in the public API. It owns job records, cancellation tokens, and the `JobSnapshot` API.
- [ ] All job execution is wrapped in the standard exception boundary (see MVP spec section 9).
- [ ] `ProgressTracker` is fed from `JobSystem` lifecycle events and provides a snapshot API for UI reads.
- [ ] `ConfigManager` is the sole owner of config file IO; UI style preset is persisted to `ui_settings.yaml`.
- [ ] Hazel-inspired UI style presets (Dark, Light, Classic) are selectable and persist across sessions.
- [ ] `Notifier` emits `NotificationEvent` and maintains append-only session history.
- [ ] `NotificationCenter` is the live UI hub for notification delivery.
- [ ] `NotificationHistory` provides filtered read access to session notifications.
- [ ] Worker threads never directly mutate project-visible UI or runtime state.
- [ ] `ASSERT_MAIN_THREAD` guards `ProcessQueue()`, snapshot write paths, and notification fan-out.
- [ ] T04 ends with a usable diagnostics skeleton: log panel, job monitor panel, notification history panel, capability snapshot panel.

---

## 6. Post-MVP roadmap (deferred — not in T04 scope)

Identified and planned; implementation deferred to later tasks.

### EventBus extensions
- [ ] `UnsubscribeAllOfType<T>()` — remove all subscribers for a given event type
- [ ] One-shot subscription variant — `SubscribeOnce<TEvent>()` auto-unsubscribes after first delivery
- [ ] Filtered subscribe — `Subscribe<TEvent>(callback, predicate)` — invoke callback only when predicate returns true (useful for job-type-specific panels)
- [ ] Tracing / debug dump — log active subscriber counts and queue depth per event type
- [ ] Stricter thread-safety policy — full per-listener-list lock or concurrent queue upgrade if contention becomes a problem

### New domain bus event types

These event types are identified as needed by later tasks. They should be added to `src/Events/` when the corresponding task begins. Listed here so T04's event infrastructure is designed with them in mind.

- [ ] `JobProgressEvent(JobId, float progress, string stage)` — periodic update from worker via `Queue()`; required by T23b (GPU orbital projection async dispatch)
- [ ] `StructureImportedEvent(StructureId, path)` — after successful POSCAR/CIF import (T07)
- [ ] `StructureModifiedEvent(StructureId)` — after atom edit, bond change, or transform (T08)
- [ ] `SelectionChangedEvent(vector<EntityId>)` — scene selection change (T08)
- [ ] `ProjectOpenedEvent` — already in `ProjectEvents.hpp`; extend with `ProjectId` when T10 introduces stable ids
- [ ] `ProjectSavedEvent(ProjectId)` — after successful save (T10)
- [ ] `CalculationStartedEvent(CalculationId)` — when a calculation job begins (T15/T16)
- [ ] `CalculationCompletedEvent(CalculationId, success)` — when a calculation job finishes (T15/T16)

### JobSystem extensions
- [ ] GIL management hooks for Python Bridge jobs (T05 dependency): `IJob` subtype or `JobContext` hook to acquire/release `py::gil_scoped_acquire` around `Execute()` when the job runs Python code; `BS::thread_pool` workers release GIL between tasks
- [ ] Job dependencies (DAG — explicit, no circular detection in first version)
- [ ] Persistent job history on disk (session log)
- [ ] External process jobs (VASP, remote SLURM)
- [ ] Batch job campaigns
- [ ] Job priorities (requires queue upgrade from FIFO)
- [ ] Pause/resume at cooperative checkpoints

### CapabilityRegistry extensions
- [ ] Runtime capability refresh — re-detect a `RuntimeDetected` capability after startup (e.g. Python venv installed while app is running); requires explicit unlock/re-lock cycle
- [ ] Capability dependency declarations — `CapabilityA` requires `CapabilityB` to be available

### StructuredError extensions
- [ ] Error aggregation — collect multiple `StructuredError` instances before presenting them to the user as a single grouped report (pre-execution validation with multiple issues)
- [ ] Recovery suggestion field — optional `string recoverySuggestion` on `StructuredError` for UI display

### Notification extensions
- [ ] Persistent notification log on disk
- [ ] `timeoutMs` policy enforcement in `NotificationCenter` — auto-dismiss transient notifications
- [ ] Notification grouping by category for UI display
- [ ] Notification action callbacks — attach named action buttons to a notification (e.g. "Retry", "Open file") for `NotificationCenter` to expose to UI

---

## 7. Non-goals for T04

- [ ] Do not refactor renderer architecture.
- [ ] Do not merge platform events with subsystem EventBus — they are separate systems.
- [ ] Do not make `Application` a heavy service-locator singleton; it owns only `EventBus`, `Notifier`, and `CapabilityService` as additions.
- [ ] Do not make `ProgressTracker` a second canonical owner of job state.
- [ ] Do not expose `BS::thread_pool` or any other BS:: types through `JobSystem` public API.
- [ ] Do not add hierarchical subtasks to `ProgressTracker` yet.
- [ ] Do not add Python execution features (T05 scope); `CapabilityRegistry` may register `PythonBridge` capability flag, but no Python code runs in T04.
- [ ] Do not add GIL management to `JobSystem` yet — hook points should be designed with it in mind, but implementation is T05.
- [ ] Do not add persistent notification or job history on disk.
- [ ] Do not add job priorities, DAG pipelines, or work stealing.
- [ ] Do not add pause/resume to `JobSystem`.
- [ ] Do not implement `ProjectWorkspace` — establish the ownership rule (section 0.9) with a comment; full implementation is T10/T11.

---

## 8. Notes for Copilot

- Treat section 0 (Locked architectural decisions) as immutable. Any proposed deviation requires explicit approval before implementation.
- Slices must compile and their tests must pass before moving to the next slice.
- **Initialization order in `Application`:** `CapabilityService` first → `EventBus` + `Notifier` → `CoreLayer::OnAttach()` → `CapabilityRegistry::LockAfterStartup()` → main loop.
- Do not rename working platform event code beyond the directory reorganization in Slice A.
- Do not introduce framework-style abstractions unless a concrete T04 test requires them.
- When a choice exists between a clever generic system and a small explicit class, choose the small explicit class.
- Worker threads must use `EventBus::Queue()`, never `EventBus::Publish()`. This is a threading contract, not a style preference.
- `JobRecord` is internal to `JobSystem`. UI code reads `JobSnapshot` only. Do not expose `JobRecord` outside the implementation file.
- `ProgressTracker` is a read model. It does not own or duplicate job state.
- `Notifier` history is append-only. `NotificationCenter` and `NotificationHistory` are consumers, not owners.
- `CapabilityRegistry` is locked after startup. Any code that attempts to register capabilities after `LockAfterStartup()` is a bug, not a feature.
- `StructuredError` is the error type. If a subsystem produces a failure and reaches for `std::string` as the error type, that is wrong — use `StructuredError`.
- Blocking popup (`ShowBlockingError`) is for pre-execution validation only. Runtime job failures go to `Notifier`, not the popup. Never conflate these two paths.
- If the implementation requires changing the documented contract, update docs and tests first, then implementation.