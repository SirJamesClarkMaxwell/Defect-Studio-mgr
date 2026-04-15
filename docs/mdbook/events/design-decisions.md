# Event System Design Decisions

This page captures rationale and trade-offs behind current architecture.

## 1. Why Two Systems Instead of One

Decision:

- keep platform event routing separate from subsystem messaging.

Reason:

- each lane needs different guarantees and mental model.

Trade-off:

- more concepts to document,
- significantly less coupling and clearer extension boundaries.

## 2. Why Platform Flow Goes Through Application and LayerStack

Decision:

- platform events are queued and dispatched by `Application` through layers.

Reason:

- deterministic frame-time control,
- unified lifecycle handling,
- predictable propagation order.

Trade-off:

- delayed processing relative to callback emission,
- improved consistency and debuggability.

## 3. Why EventQueue Uses EventVariant

Decision:

- store platform events as closed-set `EventVariant` values.

Reason:

- compile-time type closure,
- inline storage and predictable memory behavior,
- explicit extension point via variant registration.

Alternative considered:

- polymorphic heap-based event hierarchy in queue.

Why not chosen:

- higher allocation overhead and less explicit type closure.

## 4. Why EventBus Publish Is Synchronous

Decision:

- `Publish` dispatches immediately on caller thread.

Reason:

- simple and explicit semantics,
- easier call-chain debugging,
- no hidden scheduler behavior.

Trade-off:

- heavy callbacks can block caller path.

Mitigation:

- keep callbacks light,
- use `Queue` + `ProcessQueue` when deferred processing is needed.

## 5. Why Queue Processing Is Explicit

Decision:

- queue processing (`Drain`, `ProcessQueue`) is explicit and caller-controlled.

Reason:

- clear commit points,
- predictable ordering in runtime loop,
- explicit thread/ownership boundaries.

Trade-off:

- requires disciplined call placement in frame lifecycle.

## 6. Why EventBus Has Deferred Mutation Rules

Decision:

- subscription mutation during dispatch is deferred.

Reason:

- prevents invalidation and undefined behavior during iteration.

Trade-off:

- lifecycle updates are not immediate inside the same dispatch cycle.

## 7. Non-goals

- EventBus is not an input routing system.
- EventDispatcher path is not a generic module message broker.
- EventQueue is not a replacement for EventBus.
