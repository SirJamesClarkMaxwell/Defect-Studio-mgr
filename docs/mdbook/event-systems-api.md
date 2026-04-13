# Event Systems API Definition

This page defines the contract for the two event systems in DefectStudio.

## Scope

There are two separate APIs:

1. **EventHandling** for low-level OS/input/application events.
2. **EventBus** for decoupled application and subsystem messages.

They are intentionally separate and must not be merged into one abstraction.

## EventHandling API

### Purpose

Route concrete low-level events through `Application` and `LayerStack` in a predictable order.

### Main types

- `Event` base class in `Core/Event.hpp`
- `EventVariant` in `Core/Event.hpp`
- `EventDispatcher` in `Core/EventBase.hpp`
- `Layer::OnEvent(Event &event)` in `Core/Layer.hpp`
- `Application::EmitEvent(EventVariant event)` in `App/Application.hpp`
- `Application::ProcessQueuedEvents()` in `App/Application.hpp`
- `Application::OnEvent<TEvent>(TEvent &event)` in `App/Application.hpp`

### Contract

| API                                      | Responsibility                                                                       |
| ---------------------------------------- | ------------------------------------------------------------------------------------ |
| `Application::EmitEvent(EventVariant)`   | Accept a concrete event value and enqueue it for later processing.                   |
| `Application::ProcessQueuedEvents()`     | Drain the queue and deliver events on the main thread.                               |
| `Application::OnEvent<TEvent>(TEvent &)` | Handle lifecycle logic and forward the event to layers.                              |
| `Layer::OnEvent(Event &)`                | Decide whether the layer handles the event. Set `event.handled = true` when it does. |
| `EventDispatcher`                        | Perform type-specific handling inside a local `OnEvent(Event&)` implementation.      |

### Delivery order

1. OS or GLFW callback creates a concrete event object.
2. `Window` converts it to `EventVariant` and passes it to `Application::EmitEvent(...)`.
3. `Application::ProcessQueuedEvents()` drains the queue.
4. Each event is visited and sent into `Application::OnEvent<TEvent>(...)`.
5. `Application` checks lifecycle rules first.
6. `LayerStack` receives the event in reverse order, top-most layer first.
7. Each layer decides whether to handle the event in `Layer::OnEvent(...)`.

### Layer responsibility rule

Every queued event is offered to every layer until a layer marks it handled.

The layer that receives the event is responsible for calling `EventDispatcher` inside `OnEvent(Event&)` when it needs type-specific branching.

### Common use cases

- window close and resize handling
- keyboard shortcuts such as zoom, save, reset layout
- mouse move, drag, scroll, and button handling
- application lifecycle events such as shutdown requests

### Example flow

Mouse wheel zoom in a viewport:

1. GLFW produces `MouseScrolledEvent`.
2. `Window` emits it to `Application::EmitEvent(...)`.
3. `Application` drains the queue next frame.
4. The top-most layer receives `MouseScrolledEvent` in `OnEvent(Event&)`.
5. The layer dispatches it with `EventDispatcher` and updates zoom.
6. The layer sets `handled = true`.
7. Lower layers do not receive the event.

### Adding a new low-level event

1. Create the event class in `src/Core/Events/...`.
2. Add `DS_EVENT_CLASS_TYPE(...)` and `DS_EVENT_CLASS_CATEGORY(...)`.
3. Add the type to `EventVariant` in `src/Core/Event.hpp`.
4. Update the `static_assert` count in `src/Core/Event.hpp`.
5. Emit the new event from `Window` or `Application::EmitEvent(...)`.
6. Handle it in a layer through `Layer::OnEvent(Event&)`.

## EventBus API

### Purpose

Deliver typed messages between independent modules without direct dependencies.

### Main types

- `EventBus` in `Core/EventBus.hpp`
- message structs such as `Demo::FileOpenedBusEvent` in `Demo/DemoEvents.hpp`

### Current contract

| API                                    | Responsibility                                                             |
| -------------------------------------- | -------------------------------------------------------------------------- |
| `Subscribe<TEvent>(handler)`           | Register a typed handler and return a subscription id.                     |
| `Unsubscribe<TEvent>(id)`              | Remove a previously registered handler.                                    |
| `Publish<TEvent>(const TEvent &event)` | Deliver the message synchronously to all current subscribers of that type. |

### Current behavior

- `Publish(...)` is synchronous.
- Subscribers run on the caller thread.
- Callbacks execute in subscription order.
- The bus is a fan-out mechanism, not a queue.

### Common use cases

- one panel notifying another panel about changed state
- publishing demo or debug messages between independent widgets
- broadcasting job or project status to multiple consumers
- emitting notifications without direct dependencies between producer and consumer

### Background work pattern

`EventBus` itself does not start background jobs automatically.

If a subscriber wants to schedule work, it should do so explicitly from the callback:

```cpp
bus.Subscribe<Demo::DataProcessedBusEvent>([](const Demo::DataProcessedBusEvent &event)
{
    Application::Get().GetJobSystem().SubmitDetached([summary = event.resultSummary]
    {
        DoExpensivePostProcessing(summary);
    });
});
```

This keeps the bus simple and leaves job orchestration to `JobSystem`.

### Adding a new bus message

1. Define a plain message struct.
2. Publish it with `EventBus::Publish(...)`.
3. Subscribe to it with `EventBus::Subscribe<TEvent>(...)`.
4. Store the returned `SubscriptionId`.
5. Unsubscribe when the owner is destroyed.

## Rule of thumb

- Use EventHandling when the code path starts with OS/input/window events and must go through layers.
- Use EventBus when independent modules need typed communication and direct dependencies would be harmful.
