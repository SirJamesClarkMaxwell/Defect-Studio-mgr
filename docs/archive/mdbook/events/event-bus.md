# EventBus Subsystem

## Purpose / Problem Statement

EventBus exists to support decoupled module-to-module communication.

It solves:

- typed fan-out to multiple independent consumers,
- optional deferred delivery,
- explicit subscription lifetime control.

It does not solve platform/input/window routing.

## Mental Model

- producer publishes a typed bus event,
- EventBus finds subscribers for event type,
- callbacks execute by configured delivery mode,
- subscribers remain independent from producer implementation.

## Event Flow and Lifecycle

### Immediate path

1. Producer calls `Publish(event)`.
2. EventBus dispatches immediately to active subscribers.
3. Call returns after callback chain ends.

### Queued path

1. Producer calls `Queue(event)`.
2. Event stored in bus queue.
3. `ProcessQueue()` dispatches queued events later.

![EventBus lifecycle](../diagrams/generated/event-bus-lifecycle.svg)

## Delivery Model / Guarantees

- `Publish` is synchronous,
- callbacks from `Publish` run on caller thread,
- listeners are ordered by priority,
- equal-priority order is stable,
- unsubscribe during dispatch is deferred,
- subscribe during dispatch is deferred to next cycle,
- `stopPropagation` stops remaining callbacks for current event instance.

## Ownership and Lifetime

- immediate publish: caller owns event object during call,
- queue path: EventBus queue owns stored event object,
- subscription lifetime is tied to `SubscriptionHandle` ownership.

Never keep references to event objects beyond callback scope unless copied.

## Threading Model

- queue operations are synchronized,
- immediate publish executes in caller thread context,
- queued delivery executes where `ProcessQueue` is called,
- prefer queue path from worker threads when centralized commit behavior is needed.

## API Semantics

### `Subscribe<TEvent>(handler, priority)`

- registers typed callback,
- returns move-only `SubscriptionHandle`.

### `Publish<TEvent>(const TEvent&)`

- immediate dispatch,
- no internal async scheduling.

### `Queue<TEvent>(const TEvent&)`

- thread-safe enqueue for deferred dispatch.

### `ProcessQueue()`

- drains queued events and dispatches.

### Diagnostics helpers

- `HasSubscribers<T>()`
- `GetSubscriberCount<T>()`
- `GetTotalSubscriberCount()`
- `GetQueuedEventCount()`
- `IsDispatching()`

## Extension Guide

1. Create bus event type derived from `BusEvent`.
2. Add subscriber and keep `SubscriptionHandle` in owner state.
3. Select delivery mode (`Publish` or `Queue`).
4. Keep callbacks short; delegate heavy work.
5. Add tests for ordering, propagation stop, and lifetime.

## Constraints / Non-goals

- do not push OS/input events through EventBus,
- do not encode layer-propagation behavior in EventBus,
- do not assume Publish is async.

## Common Pitfalls

- heavy CPU work inside synchronous callbacks,
- missing `SubscriptionHandle` ownership,
- relying on callback order without setting priorities,
- confusion between `handled` and `stopPropagation` semantics.

## Debugging

- verify event type and subscriber registration,
- inspect subscriber counts and queue counts,
- trace callback thread context,
- inspect deferred mutation behavior when subscribing/unsubscribing during dispatch.

## Testing

- priority and stable-order tests,
- `stopPropagation` tests,
- subscribe/unsubscribe during dispatch,
- queue processing and thread-safety tests,
- lifetime safety tests for subscription handles.
