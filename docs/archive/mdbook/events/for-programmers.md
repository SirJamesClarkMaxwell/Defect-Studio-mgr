# Event Systems for Programmers

This page is the implementation-facing guide.

## Scope and Boundaries

DefectStudio has two event lanes:

- platform lane: EventDispatcher / EventHandling path,
- subsystem lane: EventBus path.

Do not merge these lanes semantically or at type level.

## Mental Model

### Platform lane

- producer: OS/GLFW callbacks and application-side injections of platform events,
- transport: `EventQueue` with `EventVariant`,
- dispatcher: `Application::OnEvent(...)`,
- consumers: layers via `Layer::OnEvent(Event&)`.

### Subsystem lane

- producer: modules/services/panels/jobs,
- transport: `EventBus` listener map and optional internal bus queue,
- dispatcher: `EventBus::Publish` or `EventBus::ProcessQueue`,
- consumers: typed subscribers.

## Delivery Model and Guarantees

### EventDispatcher lane

- processing moment is controlled by `Application` frame flow,
- layer delivery order is reverse stack traversal,
- `event.handled == true` stops delivery to lower layers,
- queue drain is per-batch commit point.

### EventBus lane

- `Publish` is synchronous,
- subscriber callbacks run on caller thread for `Publish`,
- `Queue` defers delivery to `ProcessQueue`,
- listener ordering is priority-first with stable order within equal priority,
- unsubscribe during dispatch is deferred,
- subscribe during dispatch is deferred for next cycle.

### Bus flags

- `handled`: informational in bus events,
- `stopPropagation`: explicit stop for remaining listeners of current event instance.

## Ownership and Lifetime

### Platform lane

- events are stored inline in `EventVariant` inside `EventQueue`,
- queue owns event objects until `Drain`,
- handlers receive references during dispatch window only,
- do not store long-lived references/pointers to dispatched event objects.

### EventBus lane

- queued bus events are owned by bus queue storage,
- published bus events are caller-owned for call duration,
- subscription lifetime is controlled by `SubscriptionHandle` ownership.

## Threading Model

- `EventQueue::Add` and `Drain` are synchronized,
- platform-event commit is performed by frame-driven `Application` processing,
- bus queue path is synchronized,
- prefer queueing from worker threads when main-thread commit behavior is required,
- avoid direct heavy synchronous work in `Publish` subscribers.

## Invariants

1. `EventVariant` is a closed compile-time set of platform event types.
2. Platform events flow through `Application -> EventQueue -> Application::OnEvent -> LayerStack`.
3. `event.handled == true` stops propagation to lower layers in platform lane.
4. EventBus is not used for OS/input/window routing.
5. EventQueue is part of platform lane, not a generic subsystem bus.
6. Deferred subscribe/unsubscribe rules prevent invalidation during bus dispatch.

## API Semantics by Subsystem

- [EventDispatcher](event-dispatcher.md)
- [EventBus](event-bus.md)
- [EventQueue](event-queue.md)

## Extension Checklists

### Add new platform event type

1. Add type under `src/Core/Platform/Events/`.
2. Register in `PlatformEvent.hpp` `EventVariant` alias.
3. Update variant-size static assert.
4. Emit from callback integration.
5. Handle via `Layer::OnEvent(Event&)` + `EventDispatcher`.
6. Add tests for ordering and handled semantics.

### Add new bus event type

1. Define type derived from `BusEvent`.
2. Add subscribers and persist `SubscriptionHandle`.
3. Choose delivery mode: `Publish` or `Queue`.
4. Verify callback thread expectations.
5. Add tests for ordering and subscribe/unsubscribe behavior.

## Common Pitfalls

- routing OS/input through EventBus,
- using platform events for subsystem messaging,
- keeping references to short-lived event objects,
- placing heavy blocking work in synchronous bus callbacks,
- forgetting to hold `SubscriptionHandle` (subscription disappears too early),
- assuming guarantees not provided by chosen lane.

## Debugging Guide

### Event does not arrive

- verify event type registration (`EventVariant` for platform lane),
- verify producer uses correct lane,
- verify subscriber/handler is registered and alive,
- verify propagation was not stopped earlier (`handled` / `stopPropagation`).

### Event arrives too late

- confirm whether queue-based delivery is used,
- inspect frame timing and queue drain call sites,
- inspect worker-thread queueing pattern.

### Wrong thread behavior

- check whether path is immediate `Publish` (caller thread),
- move to queue path if commit must be centralized.

## Testing Strategy

Test at three layers:

1. unit behavior:
   ordering, propagation stop, queue operations, subscribe/unsubscribe.
2. concurrency behavior:
   queue under multithreaded producers, bus deferred operations.
3. integration behavior:
   frame-loop commit timing and layer-order side effects.

Suggested focus:

- EventDispatcher ordering + `handled` stop,
- EventBus priority + deferred mutation safety,
- EventQueue deterministic batch processing.

## Design Decisions and Trade-offs

See [Design Decisions](design-decisions.md) for rationale, alternatives, and trade-offs.
