# ProgressTrackingSystem — rozszerzone wymagania

## Cel systemu
`ProgressTracker` ma być lekkim, bezpiecznym dla UI read modelem postępu jobów. Nie jest drugim właścicielem stanu joba i nie może zastępować `JobSystem`. Jego zadaniem jest dostarczenie sesyjnie stabilnych snapshotów do paneli diagnostycznych i później do innych widoków operacyjnych.

## Aktualny stan wyjściowy
Na dziś istnieje tylko bardzo podstawowy `ProgressTrackingSystem`, a równolegle istnieją już `EventSystem` i `JobSystem`. Oznacza to, że rozwój trackera powinien być podporządkowany temu układowi:
- `JobSystem` pozostaje źródłem prawdy dla lifecycle joba,
- `EventBus` jest kanałem propagacji zmian,
- `ProgressTracker` subskrybuje zdarzenia i buduje pochodny read model.

## Główne zasady projektowe
- `ProgressTracker` nie zarządza wykonaniem jobów.
- `ProgressTracker` nie przechowuje backendowych obiektów wykonawczych.
- `ProgressTracker` nie publikuje zmian do workerów; on tylko konsumuje stan i wystawia snapshoty do UI.
- Snapshoty muszą być bezpieczne do odczytu bez trzymania locka przez render ImGui.
- Aktualizacje stanu widocznego dla UI muszą być commitowane na main thread.

## Minimalny model danych vNext
Każdy wpis powinien zawierać:
- `JobId id`
- `std::string source`
- `std::string label`
- `JobStatus status`
- `float totalWork`
- `float completedWork`
- `std::string currentStage`
- `std::string currentMessage`
- `std::chrono::system_clock::time_point createdAt`
- `std::chrono::system_clock::time_point startedAt`
- `std::chrono::system_clock::time_point finishedAt`
- `std::string errorSummary`
- `bool finished` jako pole pochodne

## API snapshotów
System powinien udostępniać co najmniej:
- `std::optional<ProgressEntrySnapshot> GetSnapshot(JobId id) const`
- `std::vector<ProgressEntrySnapshot> GetAllSnapshots() const`
- opcjonalnie w kolejnym kroku: `GetActiveSnapshots()` i `GetFinishedSnapshots()`

Ważne:
- zwracane obiekty to kopie lub niemutowalne snapshoty,
- UI nie może dostać referencji do wewnętrznego stanu mutowalnego,
- snapshoty muszą nadawać się do sortowania i filtrowania po stronie panelu.

## Źródła aktualizacji
`ProgressTracker` powinien reagować na:
- submit joba,
- start joba,
- update progress,
- update stage,
- update message,
- completed,
- failed,
- cancelled.

Jeśli obecnie nie istnieje jeszcze osobny `JobProgressEvent`, trzeba przewidzieć dwa etapy:
1. MVP: odczyt części danych z `JobSystem` snapshotów i podstawowych `JobEvents`.
2. vNext: osobne eventy progresu i/lub etapów, aby UI nie było zmuszone do agresywnego polling'u.

## Integracja z EventBus
Wersja rozwinięta ma subskrybować `JobEvents` przez `EventBus` przekazany referencją przy konstrukcji.
Potrzebne zdarzenia minimalne:
- `JobStartedEvent`
- `JobCompletedEvent`
- `JobFailedEvent`
- `JobCancelledEvent`

Potrzebne zdarzenie kolejne:
- `JobProgressEvent(JobId, progress, stage, message)` albo równoważny zestaw eventów

## Wymagania dla UI JobMonitor
Aby panel JobMonitor był naprawdę użyteczny, tracker powinien docelowo wspierać:
- stabilną listę snapshotów dla całej sesji,
- łatwe rozróżnienie active vs finished,
- dokładne pola `currentStage` i `currentMessage`,
- `errorSummary` dla jobów failed,
- timestamps do liczenia elapsed,
- możliwość rozszerzenia o historię etapów.

## Stage history — rekomendowane rozszerzenie
Nie musi wejść w pierwszym kroku, ale warto zaprojektować miejsce na:
- listę zmian etapu z timestampem,
- listę ważnych checkpointów,
- ostatni znany komunikat per etap.

Przykładowy model przyszłościowy:
- `std::vector<ProgressStageEvent>` z polami: `timestamp`, `stage`, `message`, `completedWork`, `totalWork`

To pozwoli zbudować timeline w JobMonitorze bez dublowania logiki w UI.

## ETA i pola pochodne
Nie trzeba robić agresywnej predykcji, ale warto przygotować proste pola pochodne liczone na odczycie albo przy budowie snapshotu:
- elapsed,
- progress ratio,
- czy job jest cancellable,
- czy wpis jest terminalny,
- opcjonalnie prosty ETA tylko gdy dane są wystarczające.

## Obsługa błędów
Dla statusu `Failed` tracker powinien przechować krótki, bezpieczny dla UI `errorSummary`.
Pełne szczegóły techniczne mogą pozostać w logach lub `StructuredError`, ale panel listy jobów potrzebuje krótkiego streszczenia błędu.

## Wymagania wydajnościowe
- Odczyt snapshotów przez UI ma być tani.
- Render nie może trzymać blokad na mutowalnym stanie.
- Operacje sortowania i filtrowania mogą dziać się po stronie UI na kopii snapshotów.
- Tracker nie powinien kopiować całych logów jobów do każdego snapshotu.

## Czego nie robić teraz
Poza zakresem tego kroku:
- hierarchiczne subtaski,
- persistent history na dysku,
- retry semantics,
- zależności DAG między jobami,
- pause/resume,
- pełne workflow orchestration,
- przejęcie odpowiedzialności za kanoniczny stan z `JobSystem`.

## Proponowana kolejność rozwoju
1. Uzupełnić model `ProgressEntry` o wymagane pola.
2. Dodać snapshot API bezpieczne dla UI.
3. Spiąć tracker z `EventBus` i lifecycle `JobEvents`.
4. Dodać update progress/stage/message.
5. Dodać `errorSummary` i poprawne stany terminalne.
6. Przygotować miejsce pod stage history.
7. Dopiero potem rozbudowywać JobMonitor o bardziej zaawansowane widoki.

## Definicja sukcesu
System jest gotowy, gdy JobMonitor może bez hacków pokazać:
- listę wszystkich jobów,
- poprawny status,
- progress bar,
- current stage/message,
- created/started/finished timestamps,
- error summary dla failed,
- stabilną selekcję po `JobId`,
- bezpieczny render bez locków i bez czytania mutowalnego stanu backendu.
