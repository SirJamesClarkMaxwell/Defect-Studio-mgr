# TODO Detailed – T04 Core Implementation Plan

**Status:** In progress  
**Branch:** `task/04-core`  
**Last updated:** 2026-04-11

> This file contains only unfinished work. Completed T01/T03 details live in [BACKLOG-completed.md](BACKLOG-completed.md).

---

## T04 Goal

Deliver a stable core runtime with:
- deterministic application lifecycle
- explicit layer and event routing
- decoupled panel communication through EventBus
- a minimal but usable JobSystem with cooperative cancellation
- progress reporting for long-running work
- structured errors and notifications for UI policy
- diagnostics that stay main-thread safe

---

## 1. Contract and boundary pass

- [ ] Finalize the minimal core contracts before adding more behavior.
- [ ] Keep `Application`, `Layer`, `LayerStack`, `Event`, `EventDispatcher`, `EventBus`, `ProjectWorkspace`, and `EditorContext` as the only first-order concepts in this milestone.
- [ ] Document ownership rules directly in docs: `ProjectWorkspace` owns mutable runtime state, `EditorContext` only references it.
- [ ] Keep UI layers read-mostly and main-thread only for commits.
- [ ] Confirm source placement stays inside `src/Core/`, `src/App/`, and `src/Debug/` for this phase.

---

## 2. Application lifecycle

- [ ] Complete the `Application` singleton lifecycle with `Create`, `Run`, and `Shutdown`.
- [ ] Add deterministic `OnUpdate`, `OnEvent`, and `OnRender` call order.
- [ ] Keep the frame loop simple: delta time, fixed hook points, no scheduler complexity.
- [ ] Route OS events into the `LayerStack` through one canonical path only.
- [ ] Add explicit startup and shutdown logging so the main phases are visible in logs.
- [ ] Preserve the existing window abstraction split under `src/Core/Platform/`.
- [ ] Prevent renderer-feature creep in this task.

---

## 3. Event system implementation

### 3.1 Core event model

- [ ] Keep the event system aligned with the specification in `tmp/event-system-specification.md`.
- [ ] Use a base `Event` type with handled and stop-propagation flags.
- [ ] Add event priority levels only where dispatch order matters.
- [ ] Keep listener identifiers stable and RAII-safe.

### 3.2 EventBus behavior

- [ ] Implement subscription, unsubscription, publishing, queued publishing, and queue processing.
- [ ] Preserve listener ordering by priority.
- [ ] Keep unsubscribe safe during dispatch by deferring cleanup.
- [ ] Support queued event processing for main-thread consumption of worker-thread output.
- [ ] Add subscriber count and queue count queries for diagnostics.
- [ ] Ensure dispatch stops when an event requests propagation stop.

### 3.3 RAII and receivers

- [ ] Add `SubscriptionHandle` as the sole ownership boundary for event subscriptions.
- [ ] Add `EventReceiver` as the helper base for UI and panel objects that own multiple subscriptions.
- [ ] Make subscription cleanup automatic during destruction.
- [ ] Keep event receiver enable/disable behavior explicit and testable.

### 3.4 Event types needed for T04

- [ ] Add application lifecycle events.
- [ ] Add project open/close events for panel coordination.
- [ ] Add job lifecycle events for progress and notifications.
- [ ] Add notification events for UI presentation.
- [ ] Keep event types small and purpose-specific, not a monolithic variant.

### 3.5 Event tests

- [ ] Add tests for publish delivery.
- [ ] Add tests for priority ordering.
- [ ] Add tests for unsubscribe during dispatch.
- [ ] Add tests for queued event processing.
- [ ] Add tests for receiver-owned subscriptions.

---

## 4. JobSystem MVP

### 4.1 Public API

- [ ] Keep the JobSystem wrapper thin and backend-agnostic.
- [ ] Use a custom priority enum instead of binding the public API to the thread-pool library type.
- [ ] Preserve cooperative cancellation through `CancellationToken` and `CancellationSource`.
- [ ] Keep submit APIs returning typed futures for task results.
- [ ] Keep a cancellation-aware submit path separate from ordinary submit.

### 4.2 Runtime behavior

- [ ] Run jobs on a fixed-size pool with deterministic queue behavior for tests.
- [ ] Keep job execution off the UI thread.
- [ ] Track queued, running, and total jobs for diagnostics.
- [ ] Enforce pre-execution cancellation checks.
- [ ] Allow jobs to observe cancellation during execution when supported by the callable.
- [ ] Keep the backend decoupled from any future replacement of BS_thread_pool.

### 4.3 Job-system events

- [ ] Emit job started, completed, cancelled, and failed events through EventBus.
- [ ] Connect job events to progress and notification flows instead of direct UI calls.
- [ ] Keep event emission on the main-thread side of the boundary when required by UI policy.

### 4.4 JobSystem tests

- [ ] Add tests for normal submit and future completion.
- [ ] Add tests for priority ordering.
- [ ] Add tests for queued cancellation.
- [ ] Add tests for job metrics.
- [ ] Add integration tests that verify event emission hooks from job completion.

---

## 5. Progress tracking and task model

### 5.1 Current tracker expansion

- [ ] Expand `ProgressTracker` into the richer task/progress model needed by T04.
- [ ] Preserve the existing minimal register/report/finish behavior until the new model fully covers it.
- [ ] Keep snapshots safe for UI reading without mutation.

### 5.2 Data model

- [ ] Add task metadata for source, label, priority, timestamps, tags, and project context.
- [ ] Add task progress fields for current work, total work, elapsed time, estimated time, and subtask progress.
- [ ] Keep subtasks flat for the first iteration.
- [ ] Add result payload support so tasks can return typed results and structured failures.

### 5.3 Lifecycle

- [ ] Create task records when jobs are submitted, not when the UI asks for them.
- [ ] Update progress from the job execution path through explicit APIs.
- [ ] Mark completion, cancellation, and failure distinctly.
- [ ] Keep historical snapshots lightweight enough to retain for the session.

### 5.4 Task tests

- [ ] Add tests for task creation from job submit.
- [ ] Add tests for progress update and finish transitions.
- [ ] Add tests for snapshot stability under concurrent reads.
- [ ] Add tests for subtask aggregation and completion.

---

## 6. Ownership and config boundaries

- [ ] Keep `ProjectWorkspace` as the runtime source of truth for project/domain state.
- [ ] Keep `EditorContext` as a reference facade, not a storage container.
- [ ] Keep `ConfigManager` as the only owner of config file IO for `default.yaml` and `ui_settings.yaml`.
- [ ] Keep config failure paths structured and safe.
- [ ] Keep main-thread-only commit rules documented and enforced.

---

## 7. Structured errors and notifications

### 7.1 Error model

- [ ] Add a structured error type with category, code, technical context, and user-facing message.
- [ ] Use that model across config, jobs, progress, and notifications.
- [ ] Prefer actionable messages for the user and technical details for logs.

### 7.2 Notification pipeline

- [ ] Add `NotificationCenter` for live events.
- [ ] Add `NotificationHistory` for session persistence.
- [ ] Route runtime and execution failures to notifications, not blocking popups.
- [ ] Keep blocking popups only for pre-execution validation failures.
- [ ] Integrate `imgui-notify` for toast presentation.
- [ ] Add the rendering bridge in the ImGui layer.
- [ ] Add the persistent history panel in the diagnostics UI.

### 7.3 Notification tests

- [ ] Add tests for notification publish and history append.
- [ ] Add tests for severity mapping.
- [ ] Add tests for error-to-notification conversion.
- [ ] Add tests for the policy split between blocking and non-blocking paths.

---

## 8. Diagnostics UI

- [ ] Add a baseline log panel.
- [ ] Add a job/task monitor panel.
- [ ] Add a capability snapshot view.
- [ ] Add thread-affinity assertions on critical paths with `ASSERT_MAIN_THREAD`.
- [ ] Keep diagnostics useful but low-noise in normal workflow.
- [ ] Keep `DebugLayer` privileged and read-mostly.

---

## 9. Detailed T04 Task Breakdown from TODO.md

### 9.1 ProjectWorkspace and EditorContext

- [ ] Define the runtime ownership boundary in code comments and docs.
- [ ] Keep `ProjectWorkspace` as the only mutable owner of open-project state.
- [ ] Keep `EditorContext` as a thin reference layer for UI and tools.
- [ ] Add tests that verify `EditorContext` cannot silently duplicate state ownership.
- [ ] Add a main-thread commit helper for project-visible mutations.

### 9.2 ConfigManager

- [ ] Make `ConfigManager` the owner of all configuration file IO.
- [ ] Keep `default.yaml` and `ui_settings.yaml` as the first managed files.
- [ ] Preserve structured failure reporting and fallback defaults.
- [ ] Add tests for load, save, and failure fallback.

### 9.3 UI style presets and settings persistence

- [ ] Define the baseline UI style preset set, starting with the Hazel-like look.
- [ ] Add a settings model for appearance-related preferences.
- [ ] Persist user settings separately from per-project settings.
- [ ] Keep style presets and settings restoration isolated from core runtime logic.
- [ ] Add tests for preset application and settings roundtrip.

### 9.4 JobSystem

- [ ] Keep the public API focused on queued jobs, priorities, and cancellation.
- [ ] Use a custom priority enum and map it internally to the backend scheduler.
- [ ] Add cooperative cancellation through `CancellationToken` and `CancellationSource`.
- [ ] Keep typed futures as the result surface.
- [ ] Add tests for queue order, future completion, and cancellation.

### 9.5 ProgressTracker

- [ ] Expand progress registration to support long-running jobs.
- [ ] Add the richer task model with metadata, progress, and lightweight history.
- [ ] Keep the first version flat rather than hierarchical.
- [ ] Add tests for task creation, reporting, finish, and snapshot stability.

### 9.6 CapabilityRegistry and CapabilityService

- [ ] Add a baseline registry for build-time capabilities.
- [ ] Add runtime-detected capabilities and policy/access flags.
- [ ] Expose a read API for diagnostics and UI.
- [ ] Add a startup snapshot hook so capability state can be shown early.

### 9.7 DebugLayer

- [ ] Add a privileged, read-mostly diagnostics layer.
- [ ] Keep it available in Debug and optional internal Release builds.
- [ ] Connect it to log, job, and notification inspection.
- [ ] Add tests for its activation policy and guardrails.

### 9.8 Structured error model

- [ ] Define error category, code, technical context, and user-facing message fields.
- [ ] Use the same error object shape across config, jobs, and notifications.
- [ ] Prefer technical context for logs and concise text for the user.
- [ ] Add tests for formatting and conversion.

### 9.9 NotificationCenter and NotificationHistory

- [ ] Add a live notification center for runtime failures and important completions.
- [ ] Add persistent session history for inspection in the UI.
- [ ] Integrate `imgui-notify` for toast presentation.
- [ ] Add the ImGui rendering bridge and history panel.
- [ ] Add tests for publish, history append, and severity mapping.

### 9.10 Blocking popup policy

- [ ] Keep blocking popups only for pre-execution validation failures.
- [ ] Route runtime and execution failures to non-blocking notifications.
- [ ] Document the policy so future task types follow the same rules.
- [ ] Add tests that distinguish blocking and non-blocking paths.

### 9.11 Cooperative cancellation tokens

- [ ] Keep cancellation cooperative and explicit in JobSystem.
- [ ] Ensure queued work respects cancellation before execution.
- [ ] Ensure running work can observe cancellation when supported.
- [ ] Add tests for queued cancellation and execution-time checks.

### 9.12 Thread affinity guards

- [ ] Add `ASSERT_MAIN_THREAD` and any needed critical-path assertions.
- [ ] Prevent direct UI mutation from worker threads.
- [ ] Keep thread checks active in debug-focused builds.
- [ ] Add tests or integration checks where the guard can be exercised safely.

### 9.13 Main-thread-only commit rule

- [ ] Document the main-thread commit rule in the T04 docs.
- [ ] Keep project-visible state mutations routed through the main thread.
- [ ] Make background jobs publish intent or snapshots, not direct UI mutation.
- [ ] Add tests that ensure the rule is respected by the new runtime flow.

### 9.14 Operational diagnostics

- [ ] Add a baseline log panel.
- [ ] Add a job/task monitor panel with progress visibility.
- [ ] Add a capability snapshot view.
- [ ] Keep diagnostics useful but low-noise.
- [ ] Add one integration test that proves the diagnostics path is reachable from the app shell.

---

## 10. TDD order

1. [ ] Write tests for EventBus and event types.
2. [ ] Write tests for JobSystem submit, priority, and cancellation.
3. [ ] Write tests for progress/task snapshots.
4. [ ] Write tests for structured errors and notifications.
5. [ ] Implement the smallest code slice that makes each test pass.
6. [ ] Keep integration tests green after each slice.

---

## 11. Acceptance criteria

- [ ] Application boots and shuts down cleanly.
- [ ] Layer and event ordering is deterministic.
- [ ] EventBus decouples panels without tight dependencies.
- [ ] JobSystem supports priority, cancellation, and metrics.
- [ ] Progress reporting works for long-running work and UI snapshots.
- [ ] Structured errors and notifications are visible in UI.
- [ ] Debug diagnostics remain main-thread safe.
- [ ] The T04 core can support the next milestone without redesigning the boundary.

---

## 12. Explicit non-goals

- [ ] Do not add advanced renderer features here.
- [ ] Do not turn Python into a user scripting system yet.
- [ ] Do not grow `EditorContext` into a second owner of runtime state.
- [ ] Do not make the JobSystem API depend on the thread-pool backend type.
- [ ] Do not introduce hierarchical subtasks until the flat model is proven insufficient.
