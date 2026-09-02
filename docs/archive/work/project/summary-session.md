# SESSION SUMMARY – DefectsStudio

> Log sesji roboczych. Jedna sesja = jeden wpis.  
> Cel: nie tracić kontekstu między sesjami z AI (Copilot, ChatGPT, Perplexity) i własnymi sesjami kodowania.  
> Najnowszy wpis zawsze **na górze**.

## 2026-04-07 – Build Matrix Stabilization

- Naprawiono stabilność macierzy buildów dla msvc, gcc, clang.
- Dodano czyszczenie artefaktów pomiędzy przebiegami konfiguracji (`--clean-between-runs`).
- Dodano wrappery: [scripts/Windows/TestBuildMatrix.bat](scripts/Windows/TestBuildMatrix.bat), [scripts/Linux/TestBuildMatrix.sh](scripts/Linux/TestBuildMatrix.sh).
- Ujednolicono ścieżki i bootstrap `PYTHONPATH` w skryptach Linux/Python.
- Zweryfikowano: pełna macierz przechodzi lokalnie na Windows (`--continue-on-error`).