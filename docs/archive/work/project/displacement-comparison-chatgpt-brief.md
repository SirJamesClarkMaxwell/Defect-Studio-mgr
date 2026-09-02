# Displacement Comparison - ChatGPT Brief

## Prompt

Jestes ChatGPT robiacy niezalezny audyt implementacji w repo Defect Studio. Przeanalizuj kod dotyczacy strzalek przemieszczenia atomow miedzy dwiema strukturami po relaksacji lub miedzy wariantami defektu.

Kontekst problemu:
- Chcemy pokazac strzalki przemieszczen atomow miedzy struktura referencyjna i porownawcza.
- Kolejnosc atomow w plikach moze byc inna.
- Typ atomu moze sie zmienic, wiec dopasowanie nie moze zakladac identycznego gatunku.
- Struktury maja periodyczne warunki brzegowe, wiec strzalka ma pokazywac rzeczywisty najkrotszy wektor przemieszczenia, a nie linie przez cala komorke.
- Claude dodal implementacje, ale wynik nie dziala idealnie.

Zadanie:
1. Wyjasnij, co robi obecna implementacja, po co istnieje kazda warstwa i jak plyna dane od UI do renderera.
2. Sprawdz algorytm dopasowania atomow: macierz kosztow, progi, cross-species matching, obsluge atomow niedopasowanych i Hungarian assignment.
3. Sprawdz minimum-image/PBC: czy obecne C++ `MinimumImageCartesianDelta` jest poprawne dla ortogonalnych i nieortogonalnych komorek, czy respektuje `isPeriodic`, i czy nie daje dlugich falszywych strzalek.
4. Porownaj podejscie z gotowymi bibliotekami, ktore juz sa w projekcie: scipy, pymatgen, ASE, puntukas. Szczegolnie sprawdz, czy mozna uzyc `pymatgen.core.Lattice.get_distance_and_image`, `Lattice.get_all_distances`, `StructureMatcher`, albo `ase.geometry.find_mic/get_distances`.
5. Wypisz konkretne bugi, ryzyka i brakujace testy, z odniesieniami do plikow i linii.
6. Zaproponuj najmniejsza poprawna zmiane zgodna z repo: preferuj istniejace helpery, bez nowych abstrakcji, bez przepisywania renderera, jesli nie trzeba.

Pliki do przeczytania w pierwszej kolejnosci:

```text
src/Domain/Crystal/PeriodicGeometry.hpp
src/Domain/Crystal/PeriodicGeometry.cpp
src/Domain/Crystal/StructureComparison.hpp
src/Domain/Crystal/StructureComparison.cpp
tests/Domain/Crystal/StructureComparisonTests.cpp
src/ScientificRuntime/Python/StructureComparisonSolver.hpp
src/ScientificRuntime/Python/StructureComparisonSolver.cpp
src/ScientificRuntime/Python/ScipyAssignmentBridge.hpp
src/ScientificRuntime/Python/ScipyAssignmentBridge.cpp
scripts/python/examples/scipy_hungarian_assignment.py
src/ScientificRuntime/Python/CompareStructuresJob.hpp
src/ScientificRuntime/Python/CompareStructuresJob.cpp
src/ScientificRuntime/Python/PuntukasBridge.hpp
src/ScientificRuntime/Python/PuntukasBridge.cpp
src/ScientificRuntime/Python/PymatgenBridge.hpp
src/ScientificRuntime/Python/PymatgenBridge.cpp
src/ScientificRuntime/Python/PymatgenConversion.hpp
src/ScientificRuntime/Python/PymatgenConversion.cpp
scripts/python/examples/puntukas_structure_load.py
scripts/python/examples/pymatgen_structure_load.py
src/Presentation/Panels/DisplacementComparisonPanel.hpp
src/Presentation/Panels/DisplacementComparisonPanel.cpp
src/Renderer/RendererWindowState.hpp
src/Renderer/OpenGl/OpenGlRendererBackend.hpp
src/Renderer/OpenGl/OpenGlRendererBackend.cpp
src/Presentation/Panels/RendererPanelToolbar.cpp
src/Presentation/Panels/ProjectTreePanel.hpp
src/Presentation/Panels/ProjectTreePanel.cpp
src/Presentation/EditorLayer.hpp
src/Presentation/EditorLayer.cpp
src/Events/ProjectEvents.hpp
src/Events/RendererEvents.hpp
src/IO/ProjectManifestIO.hpp
src/IO/ProjectManifestIO.cpp
```

Lokalne referencje z puntukas-tools:

```text
C:\Users\fzabi\punktukas-tools\puntukas_tools\puntukas\atoms\base.py
C:\Users\fzabi\punktukas-tools\puntukas_tools\puntukas\atoms\geometry.py
C:\Users\fzabi\punktukas-tools\puntukas_tools\puntukas\atoms\geometry.pyx
```

Przydatne notatki projektowe:

```text
docs/work/project/plans/2026-08-24-calc-tools.md
docs/work/project/TODO.md
```

## Short Summary

Obecny pipeline wyglada tak:
- `DisplacementComparisonPanel` wybiera plik porownawczy i okno referencyjne.
- `CompareStructuresJob` laduje strukture porownawcza przez `PuntukasBridge`.
- `PymatgenConversion` konwertuje dane do `CrystalStructure` i zawija pozycje frakcyjne do `[0,1)`.
- `StructureComparisonSolver` buduje macierz kosztow w C++ i wysyla ja do `ScipyAssignmentBridge`.
- `scripts/python/examples/scipy_hungarian_assignment.py` uruchamia `scipy.optimize.linear_sum_assignment`.
- `BuildComparisonResultFromAssignment` sklada wynik: match, vacancy-like, interstitial-like.
- `OpenGlRendererBackend::renderDisplacementArrows` filtruje wynik progiem i rysuje walec + grot.

Najbardziej podejrzane miejsca:
- `MinimumImageCartesianDelta` sprawdza tylko 27 obrazow komorki. Dla trudnych/skosnych komorek ASE/puntukas robia bardziej ogolny MIC.
- `BuildDisplacementCostMatrix` nie sprawdza jawnie `CrystalStructure::isPeriodic`, tylko uzywa PBC jesli siatki pasuja.
- Przy roznych liczbach atomow `linear_sum_assignment` zwroci tylko `min(rows, cols)` par; nieprzetworzone comparison rows po `pairCount` moga nie trafic do `unmatchedComparisonAtoms`.
- `PymatgenConversion` zawija fractional coords przed porownaniem. To zwykle dobre dla renderera, ale warto sprawdzic, czy nie kasuje informacji potrzebnej do interpretacji relaksacji.
- W mismatch lattice fallback idzie w raw Cartesian distance. To bezpieczniejsze niz zle PBC, ale moze ukrywac sensowne porownania, jesli komorki roznia sie minimalnie albo tylko reprezentacja wektorow jest inna.

## Suggested Investigation Order

1. Zacznij od `StructureComparisonTests.cpp`, zeby zobaczyc intencje Claude'a.
2. Potem przeczytaj `StructureComparison.cpp` i `PeriodicGeometry.cpp`.
3. Sprawdz kontrakt `scipy_hungarian_assignment.py` wzgledem prostokatnych macierzy.
4. Przejdz przez `DisplacementComparisonPanel.cpp -> CompareStructuresJob.cpp -> StructureComparisonSolver.cpp`.
5. Na koncu sprawdz renderer: `RendererWindowState.hpp` i `OpenGlRendererBackend.cpp::renderDisplacementArrows`.
6. Porownaj MIC z `puntukas/atoms/geometry.pyx` oraz ASE/pymatgen.
