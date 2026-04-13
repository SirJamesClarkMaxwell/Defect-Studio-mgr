# Event Queue System

## Overview

The Event Queue (`src/Core/EventQueue.hpp`) is a thread-safe, batch-oriented event accumulation system for the DefectStudio application shell. It decouples event production (from window polling, OS callbacks) from event consumption (per-frame dispatching to layers).

## Architecture

### Data Structure

```cpp
class EventQueue 
{
    std::vector<EventVariant> m_Pending;  // contiguous variant storage
    std::recursive_mutex m_Guard;           // thread-safe access, supports explicit Lock()/Unlock()
    std::size_t m_GrowthStep = 256;        // batch growth increment
};
```

**Key Design Decision: `std::variant`-based storage**

- All 10 event types are stored inline in a `std::vector<EventVariant>`
- Eliminates per-event heap allocations (was: `vector<Unique<Event>>`)
- Enables compile-time type safety: closed event set prevents type errors at compile time
- Better memory locality: variant instances pack tightly in vector

### Event Types

Organized in subdirectories under `src/Core/Platform/Events/`:

- **WindowEvents.hpp**: `WindowCloseEvent`, `WindowResizeEvent`
- **KeyboardEvents.hpp**: `KeyPressedEvent`, `KeyReleasedEvent`, `KeyRepeatedEvent`
- **MouseEvents.hpp**: `MouseMovedEvent`, `MouseButtonPressedEvent`, `MouseButtonReleasedEvent`, `MouseScrolledEvent`
- **TouchpadEvents.hpp**: `TouchpadGestureEvent`

All inherit from `Event` base class (defined in `PlatformEventBase.hpp`).

### EventVariant

```cpp
using EventVariant = std::variant<
    WindowCloseEvent,
    WindowResizeEvent,
    KeyPressedEvent,
    KeyReleasedEvent,
    KeyRepeatedEvent,
    MouseButtonPressedEvent,
    MouseButtonReleasedEvent,
    MouseMovedEvent,
    MouseScrolledEvent,
    TouchpadGestureEvent
>;

static_assert(std::variant_size_v<EventVariant> == 10,
    "EventVariant: dodano nowy typ eventu — zaktualizuj alias i ten assert");
```

**Adding a new event type:**
1. Create type in appropriate subdirectory (e.g., `Core/Events/NewCategoryEvents.hpp`)
2. Add `#include` in `Core/Platform/Events/PlatformEvent.hpp`
3. Add type to `EventVariant` alias
4. Update `static_assert` guard from 10 → 11 (or current+1)

## Public API

### Configuration

```cpp
void Configure(std::size_t initialCapacity, std::size_t growthStep = 256);
```
- Pre-allocates memory and sets growth strategy
- Called once at Application startup (`createFromSpecification`)
- Enforces minimum 32 for both capacity and growth step

### Event Addition

```cpp
void Add(EventVariant event);
```
- Thread-safe: acquires lock internally
- Expands capacity if needed (by `growthStep`)
- Called from:
  - Window event callbacks (OS events → variant value)
  - Application::EmitEvent() (queued external event injection)

### Event Retrieval

```cpp
std::vector<EventVariant> Drain();
```
- Atomically swaps and returns all queued events
- Clears internal queue in one operation (efficient batch)
- Thread-safe: acquires lock, then releases before return
- Called once per frame in `Application::onUpdate()`

### Queries

```cpp
std::size_t Size() const;           // current event count (locked)
std::size_t Capacity() const;       // allocated capacity (locked)
bool Empty() const;                 // Size() == 0
void SetGrowthStep(std::size_t);    // adjust batch growth
void Lock() / void Unlock();        // manual lock control (rarely used)
```

### Memory Management

```cpp
void Resize(std::size_t newCapacity);  // explicitly resize to new capacity
void FitToSize();                      // compact: reserve exactly Size() elements
```

## Event Dispatch Flow

```
┌─────────────────┐
│  OS / GLFW      │  (Window callbacks fire)
│  PollEvents()   │
└────────┬────────┘
         │
         │ creates EventVariant values
         ▼
  ┌──────────────────┐
  │   EventQueue     │  (thread-safe accumulation)
  │   Add()          │  lock + push_back + capacity check
  └────────┬─────────┘
           │
           | (per frame)
           │
    ┌──────▼──────────┐
    │  onUpdate()     │
    │  Drain()        │  lock + swap + release (atomic batch)
    └────────┬────────┘
             │
    ┌────────▼─────────────┐
    │  processPendingEvents│
    │  std::visit dispatch │  for each variant:
    │  → OnEvent<T>        │    dispatch to layers
    └──────────────────────┘
```

## Integration Points

### Application Startup

```cpp
m_EventQueue.Configure(256);  // in createFromSpecification()
```

### Per-Frame Event Processing

```cpp
void Application::onUpdate(float deltaTime) {
    m_Graphics.window->PollEvents();  // fills event queue via callback
    processPendingEvents();            // drains and dispatches
}
```

### Event Dispatch (Variant Visitor Pattern)

```cpp
struct VariantEventVisitor {
    Application *self;
    template <typename TEvent>
    void operator()(TEvent &event) const {
        self->OnEvent(event);  // calls Application::OnEvent<TEvent>
    }
};

// In processPendingEvents():
for (auto &variant : events) {
    std::visit(VariantEventVisitor{this}, variant);
}
```

**Template overload for each event type:**
```cpp
template <typename TEvent>
void Application::OnEvent(TEvent &event)
{
    // Dispatch to lifecycle handler
    // Pass to LayerStack in reverse order
}
```

## Thread Safety

- **Guarantees:** 
  - `Add()` is thread-safe (scoped_lock)
  - `Drain()` is atomic with respect to queue state
  - Multiple threads can `Add()` concurrently; only one `Drain()` per frame
  
- **Non-thread-safe patterns:**
  - Do NOT call `Drain()` from worker threads (designed for main loop only)
  - Do NOT query `Size()`/`Capacity()` and assume state unchanged (use snapshots if needed)

- **Manual control (rarely used):**
  ```cpp
  queue.Lock();   // m_Guard.lock()
  // ... critical section ...
  queue.Unlock(); // m_Guard.unlock()
  ```

## Memory Characteristics

### Space

- **Per-event overhead:** 0 bytes (inline in variant)
- **Vector overhead:** sizeof(pointer) + 2×sizeof(size_t) = 24 bytes (on 64-bit)
- **Example:** 256 events × 48 bytes avg + 24 bytes overhead ≈ 12 KB

### Time Complexity

| Operation  | Complexity     | Notes                                |
| ---------- | -------------- | ------------------------------------ |
| `Add()`    | O(1) amortized | Batch growth minimizes reallocations |
| `Drain()`  | O(1)           | Swap operation, no deep copy         |
| `Size()`   | O(1) + lock    | Read only                            |
| `Resize()` | O(n)           | Reallocates; use sparingly           |

## Future Optimizations

1. **Memory Pooling:** Pre-allocate arena, reuse event storage
2. **SIMD Dispatch:** Batch visit-dispatch for contiguous variant ranges
3. **Priority Queues:** Separate critical/normal event streams
4. **Lock-Free Variant:** Replace mutex with atomic CAS for single-producer scenarios

## Testing

See `tests/Core/EventQueueTests.cpp` for comprehensive test suite covering:
- Configuration and capacity management
- Add/Drain semantics
- Variant type preservation
- Event-specific data preservation
- Thread safety (basic)
- Event categories and flags

Run tests: `.\scripts\Windows\Build.bat --config Debug --target DefectStudioTests`
