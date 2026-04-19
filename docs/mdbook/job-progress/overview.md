# Job + Progress Systems Overview

Ta sekcja opisuje wspolnie `JobSystem` i `ProgressTracker`, bo w runtime dzialaja jako jeden lancuch:
- `JobSystem` wykonuje prace,
- `EventBus` przenosi informacje o lifecycle,
- `ProgressTracker` buduje read-model dla UI.

## Problem, ktory rozwiazujemy

Bez tego zestawu komponentow:
- praca dlugotrwala blokowalaby glowny watek,
- UI nie mialoby stabilnego modelu postepu,
- moduly bylyby mocno ze soba sprzezone.

Z tym zestawem:
- joby dzialaja asynchronicznie,
- status jest obserwowalny i queryable,
- integracja jest event-driven.

## Architektura wysokiego poziomu

```mermaid
flowchart LR
  A[Caller / App] --> B[JobSystem]
  B --> C[BS::thread_pool]
  C --> D[IJob::Execute(JobContext)]
  B --> E[EventBus Queue(Job*Event)]
  E --> F[EventBus.ProcessQueue]
  F --> G[ProgressTracker]
  G --> H[UI: snapshot views]
```

## Glowne komponenty i role

- `IJob`
  - kontrakt pracy domenowej (`GetName`, `GetType`, `Execute`).
- `JobSystem`
  - submit/control/query,
  - aktualizacja snapshotow i logow,
  - harmonogramowanie delayed jobs,
  - runtime resize worker pool.
- `JobContext`
  - API runtime dla kodu joba (progress, stage, message, log, cancel, nested jobs).
- `EventBus`
  - transport eventow lifecycle (`Queue` + `ProcessQueue`).
- `ProgressTracker`
  - subskrybuje eventy i buduje snapshoty dla UI.

## Przeplyw od stworzenia zadania do zakonczenia

1. Aplikacja wywoluje `Submit` albo `SubmitAfter`.
2. `JobSystem` tworzy rekord `Queued` i publikuje `JobQueuedEvent`.
3. Zadanie trafia do puli (lub do delayed queue do czasu `dueAt`).
4. Worker uruchamia `runJob` i status przechodzi na `Running`.
5. `IJob::Execute` raportuje postep przez `JobContext`.
6. `JobSystem` publikuje `JobProgressEvent`.
7. Po koncu publikuje `JobCompletedEvent` / `JobCancelledEvent` / `JobFailedEvent`.
8. `EventBus::ProcessQueue()` dostarcza eventy do `ProgressTracker`.
9. UI czyta `ProgressEntrySnapshot` przez `GetActiveSnapshots` / `GetFinishedSnapshots`.

## Scenariusz uzycia (krotki)

Uzytkownik uruchamia import. `JobSystem` startuje `ImportJob` na workerze i publikuje eventy lifecycle. Glowne update loop wywoluje `EventBus::ProcessQueue()`, a `ProgressTracker` aktualizuje snapshot. Panel monitoringu pokazuje etap i postep. Po zakonczeniu wpis trafia do listy finished.

## Lifecycle joba (krok po kroku)

- `Queued`: rekord istnieje, job czeka na wykonanie.
- `Running`: worker rozpoczal `Execute`.
- `Completed` / `Cancelled` / `Failed`: stan terminalny.
- Snapshot i logi pozostaja queryable do czasu `RemoveFromHistory`.

## Thread safety (co jest bezpieczne)

- `JobSystem`:
  - `m_RecordsMutex` chroni `m_Records`,
  - `m_DelayedMutex` + `m_DelayedCv` chroni delayed queue,
  - `m_ThreadCountMutex` + `m_ThreadCountCv` chroni channel zmiany liczby workerow,
  - `m_NextId` i `m_ShutdownRequested` sa atomikami.
- `ProgressTracker`: `m_Mutex` chroni `m_Entries`.
- `EventBus`: `m_QueueMutex` chroni kolejke queued eventow.

Kod `IJob::Execute` pozostaje odpowiedzialnoscia autora joba i nie jest automatycznie synchronizowany przez runtime.

## Kod jako zrodlo prawdy

Kontrakty i zachowanie opisane w tym rozdziale wynikaja bezposrednio z:
- `src/Core/JobSystem/JobSystem.hpp`
- `src/Core/JobSystem/JobSystem.cpp`
- `src/Core/ProgressTrackingSystem/ProgressTracker.hpp`
- `src/Core/ProgressTrackingSystem/ProgressTracker.cpp`
- `src/Core/EventSystem/BusEventSystem/EventBus.hpp`
- `src/App/Events/JobEvents.hpp`
- `tests/Core/JobSystem/*.cpp`
- `tests/Core/ProgressTracking/ProgressTrackerTests.cpp`
