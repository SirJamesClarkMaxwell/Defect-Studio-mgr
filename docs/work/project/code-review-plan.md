# Code Review Plan

Cel: trzymac w jednym miejscu plan review i referencje do szczegolowych materialow.

## Gdzie jest glowny opis

- Sciezka review (szczegolowy opis): docs/work/project/code-review-path.md
- Primer (start od zera, API + synch/asynch + diagramy): docs/work/project/job-system-primer.md

## Zakres review

- JobSystem API i implementacja.
- ProgressTracker i mapowanie eventow.
- Integracja UI (widoki postepu i statusow).
- Testy JobSystem i ProgressTracker.

## Kolejnosc pracy

1. API i kontrakty metod.
2. Lifecycle joba od submit do finished.
3. Cancel / resume / retry / reset.
4. Delayed jobs i synchronizacja.
5. Projekcja eventow do read modelu.
6. Testy targetowane pod ryzyka.

## Wyjscie z review

- Lista znalezionych problemow i ryzyk.
- Propozycje poprawek.
- Potwierdzenie testami.

## Notatki robocze

- Aktywne tematy i decyzje: docs/work/project/problems.md
- Stabilne ograniczenia bez priorytetu: KNOWN_ISSUES.md
