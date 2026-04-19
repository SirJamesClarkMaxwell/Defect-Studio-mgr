# Job + Progress Systems

The **Job System** and **Progress Tracker** together form the core execution and monitoring infrastructure for asynchronous work within Defect Studio.

## Quick Navigation

- **[Overview](overview.md)** — High-level architecture, components, and design decisions for all readers
- **[For Users](for-users.md)** — Complete API reference, usage patterns, and practical examples for integrators
- **[For Developers](for-developers.md)** — Implementation details, thread safety, race conditions, and internals for maintainers

## What Are These Systems?

**Job System** orchestrates asynchronous execution of work units (jobs) with:
- Priority-based scheduling
- Delayed submission and cancellation
- Nested job support (parent-child relationships)
- Progress tracking via event callbacks
- Thread pool coordination

**Progress Tracker** is an event-driven read model that maintains the state of all jobs by subscribing to lifecycle events:
- Queued, Started, Progress, Completed, Cancelled, Failed
- Snapshot queries for UI updates
- Listener notifications for external systems

Together, they enable responsive UIs, background processing, and observability across the application.

---

**Start with the [Overview](overview.md) for architectural context, or jump to [For Users](for-users.md) if you need API reference and examples.**
