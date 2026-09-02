# EventDispatcher Subsystem

## Purpose / Problem Statement

EventDispatcher subsystem exists to route platform-originated events through a deterministic shell pipeline.

It solves:

- centralized event handling entry,
- predictable ordering through layers,
- explicit propagation stop semantics.

It does not solve subsystem pub/sub communication. That belongs to EventBus.

## Mental Model

- event is produced by platform callback,
- event is queued as `EventVariant`,
- `Application` chooses processing moment,
- `LayerStack` receives event in reverse order,
- layer may stop propagation with `handled = true`.

## Event Flow and Lifecycle

1. Create concrete event (`WindowResizeEvent`, `MouseScrolledEvent`, ...).
2. Push into `EventQueue`.
3. Drain queue in `Application` frame update.
4. Dispatch into `Application::OnEvent(...)`.
5. Forward through layers top-to-bottom (reverse stack iterator).
6. Destroy event object after dispatch batch scope ends.

![EventDispatcher flow](../diagrams/generated/event-dispatcher-flow.svg)

![LayerStack reverse propagation](../diagrams/generated/layerstack-event-propagation.svg)

## Delivery Model / Guarantees

- deferred processing via queue,
- deterministic layer traversal order,
- propagation stop via `handled == true`,
- processing timing controlled by application frame.

## Ownership and Lifetime

- queue owns platform events while pending,
- dispatch receives references to active event instances,
- references are not safe beyond dispatch scope.

## Threading Model

- event production can be asynchronous at callback boundary,
- queue operations are synchronized,
- event commit happens in frame processing path.

## API Semantics

### `Application::OnEvent(TEvent&)`

- runs lifecycle handling,
- forwards to layers,
- respects `handled` propagation stop.

### `EventDispatcher::Dispatch<T>(handler)`

- type-checked dispatch helper for `Event&` inputs,
- sets `handled` based on handler result.

## Extension Guide

1. Add type to platform events headers.
2. Include it in `EventVariant` alias.
3. Update static assert count.
4. Emit event from source callback.
5. Handle in appropriate layer.
6. Add tests for order and propagation.

## Constraints / Non-goals

- do not use for subsystem broadcast messaging,
- do not bypass application-controlled processing for platform flow,
- do not assume random access to event data after dispatch scope.

## Common Pitfalls

- forgetting to add new event to `EventVariant`,
- expecting low layers to receive event after upper layer sets `handled`,
- treating this lane as generic event bus.

## Debugging

- confirm callback emission,
- inspect queue size/drain calls,
- trace `handled` transitions across layers,
- verify reverse layer traversal expectations.

## Testing

- propagation stop tests,
- reverse-order traversal tests,
- variant registration tests for new events,
- queue-to-dispatch integration tests.
