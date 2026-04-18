# Sciezka Code Review - Job System + Progress Tracker

## Cel

Ten plan ma Ci dac dwie rzeczy jednoczesnie:

1. szybkie Code Review (co sprawdzic, gdzie sa ryzyka),
2. zrozumienie systemu od srodka (zrodlo zdarzenia -> moment przetwarzania -> wlasciciel przeplywu -> miejsce obslugi -> konsekwencja architektoniczna).

## Zakres

- Backend: JobSystem, JobContext, ProgressTracker, eventy jobow.
- Integracja UI: panele monitorujace postep i zadania.
- Testy: suite'y JobSystem i ProgressTracker.

## Kolejnosc Review (zalecana)

1. Przeczytaj publiczne API JobSystem i opisz kontrakty metod.
2. Przejdz przez sciezke wykonania zadania od Submit do Finished.
3. Przejdz przez semantyke cancel/resume/retry/reset.
4. Zweryfikuj sciezke delayed jobs i synchronizacje watkow.
5. Przejdz przez mapowanie eventow do snapshotow w ProgressTracker.
6. Zweryfikuj przeplyw parentId i agregacje w UI.
7. Odpal testy targetowane i porownaj z lista ryzyk.

## Mapa "Od Srodka" (Source -> Processing -> Owner -> Handler -> Consequence)

### A. Utworzenie joba (Submit / SubmitAfter)

- Zrodlo zdarzenia: wywolanie Submit/SubmitAfter przez warstwe aplikacji.
- Moment przetwarzania: natychmiast, wewnatrz submitInternal.
- Wlasciciel przeplywu: JobSystem (rekordy + kolejki + worker pool).
- Miejsce obslugi: recordQueued, publishQueuedEvent, enqueueForExecution albo delayed queue.
- Konsekwencja architektoniczna: UI nie czyta worker-state bezposrednio, tylko przez eventy/snapshoty.

### B. Wykonanie joba

- Zrodlo zdarzenia: worker pool pobiera task.
- Moment przetwarzania: runJob.
- Wlasciciel przeplywu: JobSystem + JobContext.
- Miejsce obslugi: markRunning, publishStartedEvent, callbacki progress/stage/message/log.
- Konsekwencja architektoniczna: logika domenowa joba jest odseparowana od transportu stanu.

### C. Zakonczenie joba

- Zrodlo zdarzenia: normalny koniec, cancel albo exception.
- Moment przetwarzania: catch/finalize w runJob.
- Wlasciciel przeplywu: JobSystem.
- Miejsce obslugi: markFinished + publishFinishedEvent.
- Konsekwencja architektoniczna: stabilny lifecycle i deterministyczny widok dla obserwatorow.

### D. Projekcja do read modelu (ProgressTracker)

- Zrodlo zdarzenia: eventy JobQueued/Started/Progress/Completed/Cancelled/Failed.
- Moment przetwarzania: podczas EventBus::ProcessQueue.
- Wlasciciel przeplywu: ProgressTracker.
- Miejsce obslugi: onQueued/onStarted/onProgress/onCompleted/onCancelled/onFailed.
- Konsekwencja architektoniczna: UI renderuje spojną kopie danych bez lockowania workerow.

## Lista Kontrolna Na Review (blokujace)

1. Czy kazda zmiana statusu ma uzasadnienie i nie ma skokow bezposrednich do terminalnego statusu?
2. Czy cancel jest kooperacyjny i sprawdzany regularnie w jobach dlugobieznych?
3. Czy nested submission nie prowadzi do deadlocku przy malej liczbie workerow?
4. Czy SetThreadCount zmienia pule asynchronicznie, ale bez utraty rekordow?
5. Czy delayed job po cancel nie uruchamia sie mimo uplywu dueAt?
6. Czy parentId jest zachowany od backendu do snapshotu i UI?
7. Czy usuniecie historii nie zostawia osieroconych wpisow w trackerze?

## Sciezka Testow (minimum)

### Build

```powershell
.\scripts\Windows\Build.bat --config Debug --compiler msvc --target DefectStudioTests --rebuild
```

### Run (targetowane)

```powershell
.\build\bin\Debug-windows-x86_64\DefectStudioTests\DefectStudioTests.exe --gtest_filter=JobSystem*:*ProgressTracker*
```

### Przypadki o najwyzszym zwrocie

- Nested tree: 3 poziomy, fanout 3, 2 workery.
- Resume po cancel delayed joba.
- ThreadCount up/down + brak utraty rekordow.
- Tracker lifecycle: queued -> started -> progress -> finished.

## Szybka mapa plikow do review

- JobSystem API: src/Core/JobSystem/JobSystem.hpp
- JobSystem impl: src/Core/JobSystem/JobSystem.cpp
- JobContext: src/Core/JobSystem/JobContext.hpp oraz src/Core/JobSystem/JobContext.cpp
- Tracker: src/Core/ProgressTrackingSystem/ProgressTracker.hpp oraz src/Core/ProgressTrackingSystem/ProgressTracker.cpp
- Testy JobSystem: tests/Core/JobSystem/
- Testy Tracker: tests/Core/ProgressTracking/ProgressTrackerTests.cpp

## Workflow notatek

- Aktywne znaleziska wpisuj do docs/work/project/problems.md.
- Po potwierdzeniu fixu przenos do sekcji Resolved wraz z odniesieniem do testu.
