# Event Systems

DefectStudio intentionally has two event mechanisms.

## EventHandling vs EventBus

Use the systems for different problems:

| System                                                                | Primary purpose                                        | Typical payload                                                                     | Processing moment                                   |
| --------------------------------------------------------------------- | ------------------------------------------------------ | ----------------------------------------------------------------------------------- | --------------------------------------------------- |
| EventHandling (`Core/Event.hpp`, `EventDispatcher`, `Layer::OnEvent`) | Route low-level window/input/app events through layers | `Event` subclasses (`WindowResizeEvent`, `MouseMovedEvent`, `KeyPressedEvent`, ...) | Controlled by `Application::processPendingEvents()` |
| EventBus (`Core/EventBus.hpp`)                                        | Decoupled pub/sub between independent modules/panels   | Plain typed message structs (for example `Demo::FileOpenedBusEvent`)                | Immediate, synchronous at `Publish(...)` call site  |

These systems are complementary, not interchangeable.

## What EventHandling Means In This Codebase

EventHandling is the pipeline that starts with queued low-level events and ends in layer callbacks.

Runtime flow:

1. `Window::PollEvents()` collects OS/GLFW events.
2. Callback emits `EventVariant` into `Application::EmitEvent(...)`.
3. `Application::processPendingEvents()` drains `EventQueue`.
4. Each event is visited and sent into `Application::OnEvent(...)`.
5. `Application` forwards to layers in reverse order.
6. Each layer receives the event through `Layer::OnEvent(Event&)`.

Important responsibility rule:

- Every event is offered to every layer (top-most first).
- The layer decides whether it handles the event.
- Handling must happen inside the layer's `OnEvent` function.
- If a layer sets `event.handled = true`, propagation stops.

## Why The Old Wording Was Misleading

The phrase "when you already have one concrete event object" is ambiguous.

More precise rule:

- Use `EventDispatcher` when your current API gives you `Event&` (base type),
- and at this call site you need type-specific branching (`WindowResizeEvent`, `MouseScrolledEvent`, etc.).

## EventDispatcher vs EventBus

Use `EventDispatcher` when:

- you are inside `OnEvent(Event&)` and need type-specific handling,
- event propagation/ordering through `LayerStack` matters,
- you are routing platform/window/input/application lifecycle events.

Use `EventBus` when:

- publisher and subscriber must not know each other,
- communication spans panels/subsystems,
- you want typed subscriptions and fan-out to multiple listeners.

## Runtime Event Flow Through Application

Expected flow (collect first, process later):

1. `Window::PollEvents()` only collects OS/GLFW events.
2. Window callback passes each event to `Application::EmitEvent(...)`.
3. `Application::processPendingEvents()` decides when to process queued events.
4. Each queued event goes through `Application::OnEvent(...)`.
5. Lifecycle handler runs first.
6. If still unhandled, event is propagated through `LayerStack` in reverse order.

Important behavior:

- traversal is reverse (top layers first),
- `event.handled` stops propagation,
- processing moment is controlled by `Application`, not by `Window`.

## Object Lifetime Diagram (Runtime)

```text
Application
    owns EventBus (single global instance)
    owns LayerStack
        owns DemoLayer
            owns EventDispatcherDemo
            owns EventBusDemo
                owns EventBusPublisherDemo
                owns EventBusSubscriberDemo
```

## Function Sequence Diagram

```text
GLFW callback
    -> Window::SetEventCallback
    -> Application::EmitEvent(EventVariant)
    -> Application::processPendingEvents()
    -> Application::OnEvent(Event&)
    -> HandleLifecycleEvent(event, state)
    -> LayerStack reverse traversal
    -> Layer::OnEvent(event) for each layer until handled
    -> EventDispatcher::Dispatch<T>(handler)
```

## Real Example (No New Event Type)

Scenario: viewport reacts to `MouseScrolledEvent`.

How it works in practice:

1. User scrolls mouse wheel over the viewport.
2. GLFW callback creates `MouseScrolledEvent`.
3. Event goes through queue and reaches `Application::OnEvent(...)`.
4. Top-most layer receives it in `Layer::OnEvent(Event&)`.
5. If this layer controls viewport zoom, it handles the event and sets `event.handled = true`.
6. Lower layers do not receive this event anymore.

Minimal handling pattern inside a layer:

```cpp
void EditorLayer::OnEvent(Event &event)
{
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent &scroll)
    {
        m_Camera.ZoomBy(scroll.GetYOffset());
        return true; // marks event as handled
    });
}
```

## What Happens During Publish?

`EventBus::Publish` is synchronous in current implementation.

Detailed behavior:

1. Publisher calls `EventBus::Publish(event)` on the same thread.
2. `EventBus` looks up handlers by event type in `m_Subscriptions` (`unordered_map<type_index, vector<Subscription>>`).
3. Handlers are called immediately in subscription order.
4. `Publish` returns only after all handlers finish.

Practical implications:

- handler code runs in publisher thread context,
- there is no internal queue and no deferred dispatch,
- heavy handler logic can block publisher path.

## EventBus + Background Job Submission

Current status:

- `EventBus` does not provide built-in async publish.
- To run work in background immediately after a bus message, submit a job from the subscriber.

Pattern:

```cpp
auto subId = bus.Subscribe<Demo::DataProcessedBusEvent>([](const Demo::DataProcessedBusEvent &event)
{
    Application::Get().GetJobSystem().SubmitDetached([summary = event.resultSummary]
    {
        // heavy background work
        DoExpensivePostProcessing(summary);
    });
});
```

This keeps bus semantics simple (sync dispatch), while long work is offloaded to `JobSystem`.

Implementation checklist:

1. Subscribe to existing bus message type.
2. Keep subscriber callback short.
3. Inside callback call `GetJobSystem().Submit(...)` or `SubmitDetached(...)`.
4. Move expensive work into the submitted callable.
5. If UI must be updated, publish another bus message when work is done.

## EmitEvent (Application-side event injection)

`Application::EmitEvent(EventVariant)` uses the same queue as window events.

That means:

1. subsystem creates an `EventVariant` value,
2. calls `Application::EmitEvent(...)`,
3. event is queued,
4. event is processed later by `Application::processPendingEvents()` using the same pipeline.

This keeps one consistent event path for both OS-originated and app-originated events.

## Real EventBus Example In This Repository

The demo already shows decoupled pub/sub:

- Publisher: `EventBusPublisherDemo` publishes `FileOpenedBusEvent`, `DataProcessedBusEvent`, `PipelineStatusBusEvent`.
- Subscriber: `EventBusSubscriberDemo` receives those events and updates its own local state.

Files:

- `src/Demo/EventBusPublisherDemo.cpp`
- `src/Demo/EventBusSubscriberDemo.cpp`
- `src/Demo/DemoEvents.hpp`

How to implement similar flow:

1. Define message struct (or reuse existing one).
2. In producer module call `EventBus::Publish(message)`.
3. In consumer module call `EventBus::Subscribe<T>(handler)`.
4. Store returned `SubscriptionId`.
5. Unsubscribe in destructor.

## Where Data Is Stored And How To Access It

- Subscriptions: inside `EventBus::m_Subscriptions`.
- Published event object: owned by caller for call duration.
- Subscriber state: fields in subscriber class instances.

Access points in runtime:

- global bus instance lives in `Application`,
- demos use `Application::Get().GetEventBus()` as the access API.

## How To Add Your Own Low-Level Event

Step-by-step for EventHandling/Dispatcher pipeline:

1. Define a new class derived from `Event` in `src/Core/Events/...`.
2. Add `DS_EVENT_CLASS_TYPE(...)` and `DS_EVENT_CLASS_CATEGORY(...)`.
3. Add the type to `EventVariant` in `src/Core/Event.hpp`.
4. Update the `static_assert` variant-size guard in `src/Core/Event.hpp`.
5. Emit this event (for example from `Window` callbacks or `Application::EmitEvent(...)`).
6. Handle it in a layer inside `Layer::OnEvent(Event&)` using `EventDispatcher`.

Example class:

```cpp
class ProjectOpenedEvent final : public DefectStudio::Event
{
   public:
       explicit ProjectOpenedEvent(std::string path) : m_Path(std::move(path)) {}

       const std::string& GetPath() const { return m_Path; }

       EVENT_CLASS_TYPE(ProjectOpened)
       EVENT_CLASS_CATEGORY(DefectStudio::EventCategoryApplication)

   private:
       std::string m_Path;
};
```

Example layer handling:

```cpp
void MyLayer::OnEvent(Event &event)
{
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<ProjectOpenedEvent>([this](ProjectOpenedEvent &opened)
    {
        LoadProjectUiState(opened.GetPath());
        return true;
    });
}
```

## How To Add Your Own EventBus Message

Bus messages can be plain structs.

```cpp
struct ProjectLoadedBusEvent
{
    std::string path;
    int structureCount = 0;
};
```

Publisher:

```cpp
eventBus.Publish(ProjectLoadedBusEvent{path, structureCount});
```

Subscriber:

```cpp
auto subId = eventBus.Subscribe<ProjectLoadedBusEvent>(
    std::bind(&MyPanel::OnProjectLoaded, this, std::placeholders::_1));
```

## When To Choose EventBus (Real Cases)

Choose `EventBus` when:

1. One panel publishes state changes and multiple other panels should react (for example import panel -> status panel + log panel).
2. A subsystem finishes work and independent UI widgets should refresh without direct references.
3. You need optional listeners (telemetry/debug overlays) that can appear/disappear dynamically.
4. You want to trigger background jobs from messages while keeping producer code unaware of `JobSystem` internals.
5. You want feature modules to stay loosely coupled across folder boundaries.

## Two-Window Communication Example

Window A (publisher): import workflow panel.

Window B (subscriber): statistics or status panel.

Flow:

1. Window A imports data.
2. Window A publishes bus event.
3. Window B receives bus event and updates its own state.
4. No direct A->B dependency is needed.

This is the recommended pattern for panel-to-panel communication.

## Why EventDispatcher Exists (Instead Of Manual Calls)

Manual direct call is simpler for local code, but it does not solve routing.

`EventDispatcher` gives you:

- type-safe cast and call in one place,
- one common handling contract (`bool handled`),
- compatibility with propagation model (`handled` stops lower layers).

Use direct calls for local utility logic.
Use `EventDispatcher` when handling generic `Event&` from pipeline callbacks.

## Decision Checklist

- "I have one concrete input event and need local routing" -> `EventDispatcher`
- "I need cross-subsystem messaging without coupling" -> `EventBus`
- "I want to control *when* processing happens" -> queue in `Application`, then call `ProcessQueuedEvents()` at chosen point.
