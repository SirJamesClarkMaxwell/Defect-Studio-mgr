
> Aktywne blokery, pytania bez odpowiedzi i nierozstrzygnięte decyzje.  
> Rozwiązane problemy przenoś do sekcji **Resolved** na dole pliku.  
> Znane ograniczenia bez aktywnego priorytetu → `KNOWN_ISSUES.md`

---

### Aktywne problemy

Brak.
---

### Resolved

1. Wyjaśnienie mechaniki `threadCountWorkerLoop` / `m_PendingThreadCount` / `m_ThreadCountCv` omówione w rozmowie (bez rozbudowanego opisu w tym pliku).
2. `clang` build/test matrix dla `Release` i `Dist` zostal potwierdzony jako zielony po poprawkach czyszczenia artefaktow i konfiguracji linkowania.
3. Rzadki race w `JobSystemNestedSubmissionTests.MultipleJobsCanSubmitOtherJobsConcurrently` zostal usuniety przez synchronizacje zapisu do wspolnej kolejki wykonywania w testach.

---