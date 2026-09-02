# T07 – Codex Implementation Brief

Repo: `Defect-Studio-mgr`, branch `main`, bazowy commit `d78c39b` (2026-07-02).
Ten dokument jest przeznaczony do wklejenia jako prompt dla Codex — jeden commit na raz,
w podanej kolejności (twarde zależności między nimi).

## Korekta względem poprzedniej wersji TODO

Wcześniej zapisałem, że `DomainLayer`/`StructureRegistry` nie są wpięte w startup. To był błąd —
zweryfikowałem ponownie: **wpięcie już istnieje**, tylko nie w `ApplicationBootstrap.cpp` (tam
szukałem), lecz w `src/App/RendererStartupComposer.cpp:47-75`:

```cpp
structureRecord = &domainLayer->Workspace().Structures().Add(
    std::move(loadedStructure).Value(), definition.poscarPath, definition.structureName);
...
input.structure = BuildRendererStructureData(
    domainStructure, ..., structureRecord != nullptr ? structureRecord->id : std::string{});
```

`RendererStructureData::domainStructureId` (`src/Renderer/RendererTypes.hpp:45`) jest już realnie
wypełniane prawdziwym `StructureId` ze `StructureRegistry`. Ten P0 z code review 2026-07-01 jest
więc zamknięty na commit `d78c39b`. Pozostałe P0/P1/P2 z tamtego reviewu (view-undo duplication w
`RendererWindowState`, mutowalny `RendererPanel::GetWindows()`, zależności niższych warstw od `App`)
**nie były ponownie weryfikowane w tej sesji** — traktuję je jako zamknięte zgodnie z Twoim
potwierdzeniem, ale przy code review commitów C4/C5 poniżej warto to sprawdzić, bo commity dotykają
ścieżki, którą P0 #2 opisywał.

---

## Zasady globalne dla Codex (obowiązują w każdym commicie poniżej)

1. **Jeden task = jeden branch = jeden PR.** Branch `task/07-data-model/<nazwa-commita>` od `main`.
2. **Konwencje kodu (bez wyjątków):** `[[nodiscard]]` na każdej metodzie zwracającej wartość bez
   efektu ubocznego, `PascalCase` dla publicznych członków, `camelCase` dla prywatnych metod,
   `m_PascalCase` dla pól prywatnych, `static` funkcje pomocnicze w anonimowej przestrzeni nazw
   pliku `.cpp` zamiast `namespace {}` osobno (zgodnie z istniejącym stylem w
   `StructureRendererDataBuilder.cpp`), `std::bind_front` zamiast lambd inline przy rejestracji
   komend. Namespace `DefectStudio`.
3. **Premake:** pliki `.hpp`/`.cpp` pod `src/**` wchodzą do builda automatycznie (wildcard) —
   **nie edytuj `premake5.lua`** dla nowych plików źródłowych. Jedyny wyjątek: nowy vendor
   (`Vendor/stduuid`) wymaga ręcznego dopisania do `includedirs` — patrz commit C1, dokładne linie.
4. **Testy:** GoogleTest, namespace `DefectStudio::Tests`, pliki pod `tests/<Moduł>/...` lustrzanie
   do `src/<Moduł>/...` (patrz istniejące `tests/Domain/Crystal/CrystalStructureTests.cpp`). Projekt
   `DefectStudioTests` w `premake5.lua:336-386` już wciąga `src/Domain/**`, `src/IO/**`,
   `src/ScientificRuntime/**`, `tests/**` przez wildcard — nowe testy nie wymagają zmian w premake.
5. **Build weryfikacyjny po KAŻDYM commicie (Windows, docelowa platforma):**
   `.\scripts\Windows\Build.bat --dry-run --verbose` a następnie pełny build Debug configuration
   przez wygenerowane rozwiązanie VS2022 (`premake5 vs2022` jeśli `build/` nie istnieje). Jeśli
   pracujesz w środowisku Linux/CI bez MSVC — zbuduj przez `premake5 gmake2` i `make config=debug`,
   ale **oznacz w opisie PR, że nie zweryfikowano na docelowym MSVC**.
6. **Testy po KAŻDYM commicie:** uruchom `DefectStudioTests` (target z `premake5.lua:336`) i wklej
   pełny output do opisu PR. Zero regresji w istniejących testach — jeśli test czerwony i nie ma
   związku z Twoją zmianą, **zatrzymaj się i zgłoś**, nie „napraw" go przy okazji.
7. **Nie ruszaj plików spoza listy w danym commicie**, nawet jeśli widzisz okazję do poprawy
   (Ponytail — najmniejsza poprawna zmiana). Osobne obserwacje architektoniczne pisz w opisie PR
   jako sekcję „Uwagi poza zakresem", nie w diffie.
8. **Commit message format:**
   ```
   T07.C<N>: <krótki opis>

   <2-4 zdania: co i dlaczego>

   Files: <lista głównych plików>
   Tests: <nazwy nowych/zmienionych testów>
   ```

---

## Kolejność i zależności commitów

```
C1 (UUID vendor) ──┐
                    ├──> C2 (AtomSite extensions) ──> C6 (pymatgen SD wiring)
C1 ─────────────────┘
                    
C3 (domenowy Bond) ──> C4 (BondGenerator, przenosi logikę z Renderera) ──> C5 (wiring w compose)

C7 (natywny POSCAR parser) ──> C8 (POSCAR serializer) ──> C10 (testy roundtrip)
C9 (StructureSerializer YAML) — niezależny, można równolegle z C7/C8

C11 (saved camera views) — niezależny od reszty, osobna kolejka review (Renderer/Input, nie Domain)
```

Rekomendowana kolejność mergowania do code review: **C1 → C2 → C3 → C4 → C5 → C6 → C7 → C8 → C9 → C10 → C11**.
Każdy jest osobnym, buildowalnym stanem — możesz zatrzymać się po dowolnym i mieć działający main.

---

## C1 — Vendor: stduuid + `StructureId` jako prawdziwy UUIDv4

**Cel:** zastąpić inkrementalny `std::string` w `ProjectWorkspace.hpp:9` (`using StructureId =
std::string;` generowany przez `m_NextId`) prawdziwym UUIDv4.

**Pliki:**
- `Vendor/stduuid/` — nowy, header-only vendor (`mariusbancila/stduuid`, MIT). Wystarczy plik
  `include/uuid.h` (single header wariant biblioteki) + `LICENSE`. Nie potrzeba `include/stduuid`
  jako osobnego premake project — to header-only.
- `premake5.lua` — dopisz `"Vendor/stduuid/include"` do `includedirs` w **obu** miejscach, gdzie
  jest `"Vendor/entt/src"` (linie ok. 224 i 420 w aktualnym pliku — sprawdź dokładny numer przed
  edycją, bo poprzednie commity mogły przesunąć linie).
- `src/Domain/ProjectWorkspace.hpp` — zmień:
  ```cpp
  using StructureId = std::string;
  ```
  na:
  ```cpp
  #include <uuid.h>
  using StructureId = uuids::uuid;
  ```
  `StructureRecord::id` zostaje `StructureId`. Dodaj `[[nodiscard]] std::string ToString(const
  StructureId &id);` jako wolną funkcję w `Domain/ProjectWorkspace.hpp`/`.cpp` (potrzebne wszędzie
  tam, gdzie dziś `StructureId` jest wstrzykiwane jako `std::string` — patrz niżej).
- `src/Domain/ProjectWorkspace.cpp` — `StructureRegistry::Add` generuje UUID przez
  `uuids::uuid_system_generator{}()` zamiast `m_NextId++`. Usuń pole `m_NextId` (nieużywane po
  zmianie) — **tylko jeśli** nic innego z niego nie korzysta (sprawdź grep przed usunięciem).
- `src/App/RendererStartupComposer.cpp:72-75` — `structureRecord->id` jest dziś przekazywane
  bezpośrednio jako `std::string` do `BuildRendererStructureData(..., std::string domainStructureId)`.
  Zamień na `ToString(structureRecord->id)`. **`RendererStructureData::domainStructureId` zostaje
  `std::string`** — renderer nie powinien znać typu `uuids::uuid`, to celowa granica (Renderer nie
  zależy od Domain-specific typów poza `CrystalStructure`, który już zależnościowo przechodzi przez
  `StructureRendererDataBuilder`).

**Nie ruszaj:** `CrystalStructure`, `AtomSite`, `LatticeCell` — to osobny commit (C2).

**Testy:** `tests/Domain/ProjectWorkspaceTests.cpp` już istnieje — dodaj test
`StructureRegistry_Add_GeneratesUniqueUuidPerCall` (dwa `Add()`, `EXPECT_NE(a.id, b.id)`) i
`ToString_ProducesValidUuidFormat` (regex/format check, 36 znaków z myślnikami).

**Do zweryfikowania przez Codex przed startem:** czy `mariusbancila/stduuid` jest osiągalny przez
`codeload.github.com`/`github.com` w Twoim środowisku sieciowym — jeśli nie, alternatywa bez
zależności: `std::array<std::byte, 16>` + `std::random_device`-owy generator w
`Core/Utils/Uuid.hpp` (własna, ~40-linijkowa implementacja v4). Zdecyduj na podstawie dostępności
sieci, zapisz decyzję w opisie PR.

---

## C2 — Rozszerzenie `AtomSite` o pola z POSCAR/CIF

**Cel:** domykamy PARTIAL z `Domain/Crystal/CrystalPrimitives.hpp:9-15`.

**Pliki:**
- `src/Domain/Crystal/CrystalPrimitives.hpp` — rozszerz `AtomSite`:
  ```cpp
  struct AtomSite
  {
      std::string species;
      glm::vec3 position = glm::vec3(0.0f);
      glm::vec3 fractional = glm::vec3(0.0f);
      int index = 0;
      std::string label;                                   // opcjonalna etykieta użytkownika, domyślnie puste = użyj species
      float charge = 0.0f;                                  // formalny ładunek (site charge), domyślnie neutralny
      float magnetization = 0.0f;                            // MAGMOM-style skalarny magnetic moment
      float occupancy = 1.0f;                                 // partial occupancy, domyślnie pełne obłożenie
      std::array<bool, 3> selectiveDynamics = {true, true, true}; // POSCAR Selective Dynamics per-axis; true = ruchomy (T/T/T)
      bool hasSelectiveDynamics = false;                       // czy plik źródłowy w ogóle miał blok SD — odróżnia "brak SD" od "SD=TTT"
  };
  ```
  Uzasadnienie `hasSelectiveDynamics`: POSCAR bez bloku SD i POSCAR z `T T T` dla każdego atomu to
  różne stany — pierwszy nie powinien przy eksporcie dopisywać bloku SD, drugi powinien. Bez tej
  flagi eksporter (C8) zgadywałby.
- `src/Domain/Crystal/CrystalStructure.hpp` — bez zmian strukturalnych (nadal `std::vector<AtomSite>
  atoms`), ale dodaj metodę pomocniczą:
  ```cpp
  [[nodiscard]] bool HasAnySelectiveDynamics() const;
  ```
  w `.cpp`: `std::any_of(atoms.begin(), atoms.end(), [](const AtomSite &a) { return
  a.hasSelectiveDynamics; })`.

**Nie ruszaj:** `VacancySite` (zostaje martwe pole do T11, nie dotykaj), `LatticeCell`.

**Miejsca do zaktualizowania (inicjalizacja domyślna nowych pól, żeby się kompilowało):**
- `src/ScientificRuntime/Python/PymatgenConversion.cpp:16-24` — pętla tworząca `AtomSite` z
  `PymatgenStructureSite`; na razie **nie** wypełniaj tam nowych pól (to robi C6), ale upewnij się,
  że kompiluje się z default-member-initializers (nie wymaga zmian, bo pola mają defaulty).
- `tests/Domain/Crystal/CrystalStructureTests.cpp:22-25` — konstruktor agregatowy
  `AtomSite{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0}` — **przestanie się kompilować** po dodaniu
  pól bez designated initializers w C++23 (aggregate init z pominiętymi członkami nadal działa,
  bo dodajesz pola na końcu z defaultami — zweryfikuj kompilację, to nie powinno wymagać zmiany
  testu, ale sprawdź).

**Testy:** `tests/Domain/Crystal/CrystalStructureTests.cpp` — dodaj
`AtomSite_DefaultConstruction_HasFullOccupancyAndNoSelectiveDynamics`,
`HasAnySelectiveDynamics_ReturnsTrueWhenAnyAtomFlagged`.

---

## C3 — Domenowy model `Bond`

**Cel:** dziś bond istnieje wyłącznie jako `RendererBondData` (`src/Renderer/RendererTypes.hpp:29-35`),
generowany on-the-fly w `StructureRendererDataBuilder.cpp:64-110` (funkcja `BuildBonds`, spatial
hash grid, `kCellSize = 4.72f`, `kCutoffScale = 1.18f` jako stałe wklejone w kodzie). Ten commit
**tylko dodaje typ domenowy**, bez migracji logiki (to C4).

**Pliki:**
- `src/Domain/Crystal/CrystalPrimitives.hpp` — dodaj:
  ```cpp
  enum class BondOrigin
  {
      Auto,    // wygenerowany przez BondGenerator na podstawie promieni kowalencyjnych
      Manual   // dodany ręcznie przez użytkownika (T08), nigdy nie usuwany przez regenerację
  };

  struct Bond
  {
      std::size_t firstAtomIndex = 0;   // indeks w CrystalStructure::atoms — NIE stabilny UUID atomu (patrz uwaga niżej)
      std::size_t secondAtomIndex = 0;
      BondOrigin origin = BondOrigin::Auto;
      bool visible = true;              // ukrycie pojedynczego wiązania bez usuwania z modelu (§4.3 starego dokumentu)
  };

  struct BondGenerationSettings
  {
      float globalCutoffScale = 1.18f;                                  // musi zgadzać się z dotychczasową wartością w StructureRendererDataBuilder.cpp:68, żeby migracja C4 nie zmieniła wizualnie istniejących scen
      std::unordered_map<std::string, float> perPairCutoffOverride;     // klucz: posortowane alfabetycznie "El1-El2", np. "C-O"; T08 UI dopisze edycję, tu tylko struktura danych
  };
  ```
  Wymaga `#include <unordered_map>` w tym pliku.
- `src/Domain/Crystal/CrystalStructure.hpp` — dodaj pola:
  ```cpp
  std::vector<Bond> bonds;
  BondGenerationSettings bondSettings;
  ```

**Ważna uwaga do zapisania w opisie PR (nie do naprawienia w tym commicie):** `Bond` referencjonuje
atomy przez indeks w wektorze, nie przez stabilny identyfikator. Usunięcie/przesortowanie atomów
(funkcjonalność T08 — delete atom, extract to collection) zepsuje te referencje. To świadomy dług
architektoniczny — T08 musi albo (a) przeindeksować `bonds` przy każdej mutacji `atoms`, albo
(b) dodać stabilny `AtomId` do `AtomSite` i przejść na niego w `Bond`. Nie rozwiązuj tego teraz.

**Nie ruszaj:** `RendererTypes.hpp`, `StructureRendererDataBuilder.cpp` — to C4.

**Testy:** `tests/Domain/Crystal/CrystalStructureTests.cpp` — konstrukcja `Bond`/`BondGenerationSettings`,
sprawdzenie defaultów (`globalCutoffScale == 1.18f`, `origin == BondOrigin::Auto`).

---

## C4 — `BondGenerator`: przeniesienie logiki auto-bond z Renderera do Domain

**Cel:** to jest **breaking change** ustalony w rozmowie — Renderer ma odtąd czytać `bonds` z
`CrystalStructure`, nie generować ich sam.

**Nowe pliki:**
- `src/Domain/Crystal/BondGenerator.hpp` / `.cpp`:
  ```cpp
  namespace DefectStudio
  {
      // Nadpisuje structure.bonds wpisami BondOrigin::Auto na podstawie promieni kowalencyjnych.
      // Zachowuje istniejące wpisy BondOrigin::Manual bez zmian. Wywoływać po każdej zmianie
      // geometrii/kompozycji struktury (import, dodanie/usunięcie atomu w T08).
      void RegenerateAutoBonds(
          CrystalStructure &structure,
          const ElementPropertiesTable &elementPropertiesTable);
  }
  ```
  Implementacja: przenieś **1:1** logikę spatial-hash z
  `src/Renderer/StructureRendererDataBuilder.cpp:33-110` (`RendererGridCellKey`,
  `RendererGridCellKeyHasher`, `CellForPosition`, ciało `BuildBonds`) — zmień typy wejścia z
  `std::vector<RendererAtomData>` na `std::vector<AtomSite>`/`CrystalStructure`, promienie kowalencyjne
  z `elementPropertiesTable.CovalentRadius(atom.species)` (dziś w oryginale zapewne analogicznie przez
  `atoms[i].element` — sprawdź dokładną sygnaturę w oryginale przed portowaniem 1:1), cutoff z
  `structure.bondSettings.globalCutoffScale` zamiast stałej `kCutoffScale = 1.18f`, oraz per-parę z
  `structure.bondSettings.perPairCutoffOverride` (klucz `"El1-El2"` posortowany alfabetycznie; jeśli
  brak wpisu, użyj `globalCutoffScale`). **Nie generuj bondów między atomami, które w T08 będą
  należeć do różnych Kolekcji** — na dziś (przed T08) to nie ma znaczenia, bo kolekcji nie ma, więc
  pomiń ten warunek, ale zostaw komentarz `// TODO(T08): pomiń pary z różnych kolekcji`.
  Usuń wygenerowane `BondOrigin::Auto` wpisy przy każdym wywołaniu przed regeneracją (żeby nie
  duplikować), zachowaj `BondOrigin::Manual`.

**Zmiany w istniejących plikach:**
- `src/Renderer/StructureRendererDataBuilder.cpp` — usuń `BuildBonds` i jej pomocnicze typy
  (`RendererGridCellKey*`, `CellForPosition`) z anonimowej przestrzeni nazw. `BuildRendererStructureData`
  ma teraz mapować `structure.bonds` (już wypełnione przez `RegenerateAutoBonds` wywołane wcześniej
  w pipeline — patrz C5) na `RendererBondData`, 1:1 po indeksie (`firstAtomIndex`/`secondAtomIndex`
  kopiowane wprost, `radius`/`gradient` liczone jak dotychczas z `AtomStyleTable`). **Filtrowanie
  po `bond.visible`** — jeśli `!bond.visible`, pomiń przy budowaniu `RendererBondData` (to daje za
  darmo przyszłą funkcję "ukryj pojedyncze wiązanie" z §4.3, nawet bez UI w tym commicie).
- `src/Renderer/StructureRendererDataBuilder.hpp` — sygnatura `BuildRendererStructureData` bez
  zmian (nadal przyjmuje `CrystalStructure`, teraz zakłada że `structure.bonds` jest już wypełnione
  — **udokumentuj to w komentarzu nad deklaracją**, np. `// Wymaga, by structure.bonds było
  wypełnione przez BondGenerator::RegenerateAutoBonds przed wywołaniem tej funkcji.`).

**Nie ruszaj:** `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` (konsumuje `RendererBondData`, nie
zmienia się), shader `bond_transform.comp`.

**Testy:**
- Nowy `tests/Domain/Crystal/BondGeneratorTests.cpp`: dwa atomy w odległości sumy promieni
  kowalencyjnych × 1.0 → bond istnieje przy `globalCutoffScale >= 1.0`; przy odległości 3× sumy
  promieni → brak bondu. Test z `perPairCutoffOverride` nadpisującym globalny cutoff dla
  konkretnej pary. Test że `BondOrigin::Manual` przeżywa `RegenerateAutoBonds`.
- Zaktualizuj/zweryfikuj istniejące testy renderera dotykające `StructureRendererDataBuilder`
  (sprawdź `tests/` pod kątem plików testujących ten moduł — jeśli istnieją, dostosuj wejście tak,
  by `structure.bonds` było prefill'owane przed wywołaniem `BuildRendererStructureData`, inaczej
  testy dostaną pustą listę bondów zamiast wcześniej auto-generowanej).

---

## C5 — Wpięcie `RegenerateAutoBonds` do startup flow

**Cel:** domknięcie C3/C4 — bez tego `structure.bonds` jest zawsze puste, bo nic go nie wypełnia.

**Pliki:**
- `src/App/RendererStartupComposer.cpp` — w `ComposeRendererStartup`, zaraz po
  `domainLayer->Workspace().Structures().Add(...)` (linia ok. 55-58) i **przed** wywołaniem
  `BuildRendererStructureData` (linia ok. 72), wywołaj:
  ```cpp
  if (structureRecord != nullptr)
      RegenerateAutoBonds(const_cast<CrystalStructure &>(structureRecord->structure), composition.assets.elementPropertiesTable);
  ```
  **Uwaga:** `StructureRecord::structure` jest dziś `const` z perspektywy `Find`, ale `Add` zwraca
  `const StructureRecord &`. Sprawdź w `src/Domain/ProjectWorkspace.hpp` czy potrzebna jest zmiana
  sygnatury `Add`, żeby zwracała niekonstantną referencję (do zapisu bondów) — **jeśli tak, to
  minimalna, celowa zmiana API, opisz ją jawnie w PR**, nie chowaj jej jako "przy okazji". Alternatywa
  bez zmiany const-ności: regeneruj bondy na `loadedStructure.Value()` **przed** przekazaniem do
  `Structures().Add(...)`, czyli przenieś wywołanie `RegenerateAutoBonds` nad linię `Add(...)`.
  **Wybierz tę drugą opcję** — nie zmieniaj const-ności API rejestru bez wyraźnej potrzeby.
  Dodaj `#include "Domain/Crystal/BondGenerator.hpp"`.

**Nie ruszaj:** resztę pliku poza tym jednym blokiem w pętli `for (const
RendererStartupWindowDefinition &definition : ...)`.

**Testy:** to jest integracyjna ścieżka startupu — trudna do testu jednostkowego bez sieci
plików/Pythona. Zamiast tego dodaj test w `tests/Domain/Crystal/BondGeneratorTests.cpp` (z C4)
potwierdzający, że `RegenerateAutoBonds` wywołane na przykładowej strukturze POSCAR_Si (fixture
już istnieje: `tests/ScientificRuntime/fixtures/POSCAR_Si.vasp`) daje niepustą listę bondów o
oczekiwanej długości (Si-Si w strukturze diamentowej ma znaną, policzalną liczbę sąsiadów — policz
ręcznie dla fixture i zahardkoduj jako `EXPECT_EQ`).

---

## C6 — Selective Dynamics / site properties z pymatgen

**Cel:** domyka PARTIAL z C2 dla realnych danych z importu.

**Pliki:**
- `scripts/python/examples/pymatgen_structure_load.py` — w komprehensji `sites` (linie ok. 15-21),
  dopisz do każdego słownika site'a:
  ```python
  "selective_dynamics": (
      site.properties.get("selective_dynamics").tolist()
      if site.properties.get("selective_dynamics") is not None
      else None
  ),
  "charge": float(site.properties.get("charge", 0.0)) if site.properties.get("charge") is not None else None,
  "magmom": float(site.properties.get("magmom", 0.0)) if site.properties.get("magmom") is not None else None,
  "occupancy": float(sum(site.species.values())) if hasattr(site, "species") else 1.0,
  ```
  **Zweryfikuj dokładne API pymatgen** dla `site.properties` i `selective_dynamics` (klucz może się
  nazywać inaczej w zależności od wersji pymatgen — sprawdź w środowisku venv projektu,
  `python -c "from pymatgen.core import Structure; help(Structure.from_file)"` i realny plik POSCAR
  z blokiem SD z `tests/ScientificRuntime/fixtures/`, jeśli tam nie ma przykładu z SD — dodaj nowy
  fixture `POSCAR_Si_SelectiveDynamics.vasp` z ręcznie dopisanym blokiem `Selective dynamics` i
  `T T T` / `F F F`).
- `src/ScientificRuntime/Python/PymatgenBridge.hpp` — rozszerz `PymatgenStructureSite`:
  ```cpp
  struct PymatgenStructureSite
  {
      std::string element;
      glm::vec3 fractionalPosition = glm::vec3(0.0f);
      glm::vec3 cartesianPosition = glm::vec3(0.0f);
      std::optional<std::array<bool, 3>> selectiveDynamics;
      std::optional<float> charge;
      std::optional<float> magmom;
      float occupancy = 1.0f;
  };
  ```
  Wymaga `#include <optional>` i `#include <array>`.
- `src/ScientificRuntime/Python/PymatgenBridge.cpp` — w `ParsePymatgenStructurePayload` (linia
  ok. 23-58), rozszerz parsowanie pojedynczego site'a o odczyt nowych kluczy JSON z
  `nlohmann::json::value`/`contains` (wzorzec jak `payload.value("reduced_formula",
  std::string{})` już użyty w linii 26). Pamiętaj o `null` w JSON → `std::optional` bez wartości.
- `src/ScientificRuntime/Python/PymatgenConversion.cpp` — w pętli `for (std::size_t index = 0; ...)`
  (linie 16-24), wypełnij nowe pola `AtomSite`:
  ```cpp
  atom.hasSelectiveDynamics = site.selectiveDynamics.has_value();
  atom.selectiveDynamics = site.selectiveDynamics.value_or(std::array<bool, 3>{true, true, true});
  atom.charge = site.charge.value_or(0.0f);
  atom.magnetization = site.magmom.value_or(0.0f);
  atom.occupancy = site.occupancy;
  ```

**Nie ruszaj:** logikę `lattice`/pozycji — bez zmian.

**Testy:**
- `tests/ScientificRuntime/PymatgenConversionTests.cpp` (istnieje) — dodaj test z syntetycznym
  `PymatgenStructureData` mającym `selectiveDynamics = {{false, false, true}}` dla jednego site'a i
  `std::nullopt` dla drugiego, sprawdź że `ConvertPymatgenStructureToCrystalStructure` daje
  `hasSelectiveDynamics == true` dla pierwszego i `false` dla drugiego.
- Jeśli w repo jest test integracyjny ładujący realny plik przez `PymatgenBridge` (sprawdź
  `tests/ScientificRuntime/` pod kątem testów korzystających z `POSCAR_Si.vasp`) — dodaj analogiczny
  z nowym fixture `POSCAR_Si_SelectiveDynamics.vasp`.

---

## C7 — Natywny C++ parser POSCAR/CONTCAR (`IOLayer`)

**Cel:** dziś jedyna ścieżka importu to Python/pymatgen (`ScientificRuntimeLayer::LoadCrystalStructure`
→ `PymatgenBridge`). To wolniejsze, wymaga uruchomionego interpretera i nie działa offline bez
venv. Zgodnie z zasadą z dokumentacji projektu: **podstawowy parsing strukturalny → C++ `IOLayer`,
wzbogacone dane (symetria, formuła, itp.) → `ScientificRuntime`/ASE/pymatgen**.

**Nowe pliki:**
- `src/IO/PoscarIO.hpp` / `.cpp`:
  ```cpp
  namespace DefectStudio
  {
      struct PoscarParseOptions
      {
          // obecnie brak opcji — miejsce na przyszłe rozszerzenia (np. tolerancja parsowania)
      };

      [[nodiscard]] Result<CrystalStructure> ParsePoscarFile(const Path &filePath, const PoscarParseOptions &options = {});
      [[nodiscard]] Result<CrystalStructure> ParsePoscarText(const std::string &text, const PoscarParseOptions &options = {});
  }
  ```
  Użyj `Result<T>`/`StructuredError` w stylu istniejącym w `src/Core/Diagnostics/` — sprawdź wzorzec
  błędów w `src/ScientificRuntime/Python/PymatgenBridge.cpp` (tam `Result<PymatgenStructureData>`)
  jako referencyjny styl komunikatów błędów.

  **Zakres formatu (VASP5/6), zgodnie z `old-ds-functionality.md` §1.1:**
  1. Linia 1: komentarz/nazwa struktury → `CrystalStructure::name`.
  2. Linia 2: skala sieciowa (`float`). **Jeśli ujemna:** to nie skala liniowa, tylko docelowa
     objętość komórki w Ų — po wczytaniu 3 wektorów sieci trzeba przeskalować wszystkie trzy tak,
     by `|det(lattice)| == |scale|`. Wzór: `factor = cbrt(|scale| / |det(rawLattice)|)`, każdy
     wektor `*= factor`.
  3. Linie 3-5: trzy wektory sieci (3 floaty każdy, separator biały znak) → `LatticeCell::vectors`,
     przemnożone przez skalę (dodatnią) lub przeskalowane wg wzoru wyżej (ujemna).
  4. Linia 6: symbole pierwiastków (VASP5/6 — **wymagana**, zgodnie z notatką w starym dokumencie
     "linia symboli pierwiastków jako obowiązkowa" — **nie** implementuj fallbacku VASP4 bez tej
     linii, zwróć `StructuredError` z jasnym komunikatem jeśli linia 6 wygląda jak liczby zamiast
     symboli pierwiastków).
  5. Linia 7: krotności (int na symbol z linii 6, ta sama kolejność).
  6. Linia 8 (opcjonalna): `Selective dynamics` — jeśli pierwszy znak (case-insensitive) to `S`,
     ustaw `hasSelectiveDynamics = true` dla wszystkich atomów i przesuń numerację linii o 1.
  7. Kolejna linia: `Direct`/`Cartesian` (albo `D`/`C`, case-insensitive, pierwsza litera
     wystarczająca zgodnie ze specyfikacją VASP) — tryb koordynat.
  8. Kolejne linie (suma krotności z pkt. 5): pozycja (3 floaty) + opcjonalnie 3 flagi `T`/`F`
     (jeśli był blok Selective Dynamics) → `AtomSite::selectiveDynamics` (`T` = `true` = ruchomy).
     Jeśli tryb `Cartesian`: **przeskaluj pozycję przez współczynnik sieciowy** (skalę z pkt. 2,
     dodatnią wartość — jeśli oryginalna skala była ujemna, użyj `factor` z pkt. 2, nie surowej
     wartości ujemnej) — to dokładnie błąd nr 3 z sekcji "POSCAR export bugs" w pamięci projektu,
     **uważaj żeby go nie powtórzyć przy imporcie**. Jeśli tryb `Direct`: wartości to już `fractional`,
     policz `position` (kartezjańska) przez `CrystalStructure::FractionalToCartesian`.
  9. `AtomSite::species` przypisz na podstawie tego, w którym „bloku" krotności znajduje się dany
     atom (symbole i krotności z pkt. 4-5 określają grupowanie — POSCAR nie ma symbolu przy każdym
     atomie, tylko zgrupowane bloki).
  10. `AtomSite::index` = pozycja w finalnym `atoms` (0-based, w kolejności pliku).

  **Precyzja liczb:** parsuj przez `std::stod` (double), nawet jeśli `AtomSite::position` jest
  dziś `glm::vec3` (float) — **nie zmieniaj typu `AtomSite::position` w tym commicie** (to osobny,
  większy task migracji `glm::vec3`→`glm::dvec3` opisany w pamięci projektu jako "błąd
  architektoniczny" wymagający zmian w całym parserze i eksporterze; tu tylko parsujemy z pełną
  precyzją double i rzutujemy na float przy zapisie do `AtomSite`, żeby nie tracić precyzji na
  etapie samego parsowania tekstu, nawet jeśli finalne przechowywanie ją i tak obetnie).

**Zmiany w istniejących plikach:**
- `src/IO/IOLayer.hpp`/`.cpp` — **nie musisz** dodawać metody do `IOLayer` (klasa `Layer` obsługuje
  eventy config/UI, nie pasuje tu bezpośrednio). `PoscarIO` to wolna funkcja w `namespace
  DefectStudio`, wywoływana bezpośrednio tam gdzie dziś woła się `ScientificRuntimeLayer::
  LoadCrystalStructure` — **ale w tym commicie NIE podłączaj jej do żadnego call site**
  (`RendererStartupComposer` zostaje na Pythonie na razie — podłączenie natywnego parsera jako
  alternatywnej/domyślnej ścieżki importu to decyzja UX wymagająca Twojej akceptacji, zostaw jako
  osobny task po code review tego commita). Ten commit dostarcza **tylko** funkcję i testy.

**Testy:**
- Nowy plik `tests/IO/PoscarIOTests.cpp`, fixture'y w `tests/IO/fixtures/` (skopiuj/zaadaptuj
  `tests/ScientificRuntime/fixtures/POSCAR_Si.vasp`, dodaj nowe: dodatnia skala, ujemna skala
  (target volume), Direct, Cartesian, z Selective Dynamics, bez Selective Dynamics, wieloskładnikowa
  struktura (≥2 pierwiastki, różne krotności), plik bez linii symboli pierwiastków (oczekiwany
  `StructuredError`, nie crash).
- Test roundtrip **odłóż do C10** (wymaga C8, serializera).

---

## C8 — Natywny serializer POSCAR

**Cel:** eksport bez zależności od Pythona; naprawia przy okazji dwa znane błędy z pamięci projektu
(precyzja ignorowana, `canonicalizeDirectTranslation` domyślnie `true`) — **ale to nowy kod, nie
port starego repo, więc pisz od razu poprawnie, nie „napraw buga" bo go tu jeszcze nie ma**.

**Pliki:**
- `src/IO/PoscarIO.hpp` — dopisz do istniejącego (z C7) pliku:
  ```cpp
  struct PoscarWriteOptions
  {
      int precision = 16;                              // cyfr po przecinku, zakres 1-16
      bool useDirectCoordinates = true;                 // Direct (true) vs Cartesian (false)
      bool canonicalizeDirectTranslation = false;        // patrz uwaga niżej — domyślnie WYŁĄCZONE
      std::optional<std::vector<std::string>> speciesOrder; // jeśli podane, nadpisuje domyślne sortowanie malejące po krotności — patrz niżej
  };

  [[nodiscard]] Result<void> WritePoscarFile(
      const Path &filePath,
      const CrystalStructure &structure,
      const PoscarWriteOptions &options = {});

  [[nodiscard]] Result<std::string> WritePoscarText(
      const CrystalStructure &structure,
      const PoscarWriteOptions &options = {});
  ```
  **`canonicalizeDirectTranslation`** — jeśli `true`, przesuwa wszystkie pozycje `Direct` tak, by
  mieściły się w `[0, 1)` (modulo 1.0 per-oś) przed zapisem. Domyślnie `false`, żeby **nie**
  zmieniać pozycji atomów względem tego co użytkownik widzi w edytorze (round-trip bez niespodzianek
  — to był realny bug w poprzedniej wersji projektu, opisany w pamięci jako "domyślnie `true`,
  shifting atom positions unexpectedly").

  **Kolejność species przy zapisie:**
  - Jeśli `options.speciesOrder` jest ustawione: użyj tej kolejności (musi zawierać dokładnie te
    same symbole co `structure.UniqueSpecies()`, inaczej `StructuredError` — nie milcz i nie
    dopisuj brakujących).
  - Jeśli nie ustawione: domyślnie sortuj malejąco po liczbie atomów danego gatunku (zgodnie z
    `old-ds-functionality.md`/oryginalnym TODO), przy remisie zachowaj kolejność pierwszego
    wystąpienia w `structure.atoms` (stabilne sortowanie).
  - **Ta opcja nie ma dziś gdzie być trwale zapisana jako "właściwość projektu"**, bo `T10` (Project
    System) jeszcze nie istnieje. W tym commicie `speciesOrder` jest tylko parametrem funkcji — **nie
    twórz** żadnego mechanizmu persystencji per-projekt. Zostaw `// TODO(T10): przenieść do
    ProjectMetadata, gdy powstanie` jako komentarz przy `PoscarWriteOptions::speciesOrder`.

  **Precyzja:** użyj `std::format` (C++23, projekt już deklaruje `cppdialect "C++latest"` w
  `premake5.lua:342`) z dynamicznym precyzją, np. `std::format("{:.{}f}", value,
  options.precision)`, **nie** `std::max(16, ClampPrecision(...))` (to był dokładnie ten bug —
  pilnuj, żeby `options.precision` realnie sterowało wyjściem, dodaj test właśnie na to).
  `ClampPrecision`-podobna walidacja: jeśli `precision` poza `[1, 16]`, `StructuredError`
  (walidacja wejścia, nie ciche clampowanie — inna filozofia niż stary kod, bo tu API jest nowe).

**Testy:**
- `tests/IO/PoscarIOTests.cpp` (rozszerz plik z C7): `WritePoscarText` z `precision = 4` faktycznie
  produkuje 4 cyfry po przecinku (parsuj output regexem/manualnie, sprawdź długość części
  ułamkowej). Test że `canonicalizeDirectTranslation = false` **nie** zmienia pozycji atomu poza
  `[0,1)` przy zapisie (np. atom na `1.5` zostaje `1.5`, nie `0.5`). Test że `speciesOrder`
  niekompletny/z obcym symbolem zwraca błąd zamiast cichego zignorowania.

---

## C9 — `StructureSerializer` (YAML), fundament pod T10

**Cel:** serializacja `CrystalStructure` do/z YAML jako część przyszłego formatu projektu — **nie
podłączona do niczego jeszcze**, bo `T10` (Project System) nie istnieje. Ten commit dostarcza tylko
funkcje + testy, żeby T10 mogło je użyć bez pisania serializera od zera.

**Pliki:**
- `src/IO/CrystalStructureYamlIO.hpp` / `.cpp` — wzoruj się na istniejącym `src/IO/
  ElementPropertiesIO.hpp`/`.cpp` (yaml-cpp, ten sam styl błędów):
  ```cpp
  [[nodiscard]] Result<void> WriteCrystalStructureYaml(const Path &filePath, const CrystalStructure &structure);
  [[nodiscard]] Result<CrystalStructure> ReadCrystalStructureYaml(const Path &filePath);
  ```
  Serializuj **wszystkie** pola `CrystalStructure` z aktualnego stanu (po C2/C3: `name`, `cell`,
  `atoms` łącznie z nowymi polami `label/charge/magnetization/occupancy/selectiveDynamics/
  hasSelectiveDynamics`, `vacancies` — nawet jeśli dziś zawsze puste, format ma być gotowy — `bonds`
  z `origin`/`visible`, `bondSettings`, `isPeriodic`). To jest inny format niż POSCAR (C7/C8) —
  YAML jest lossless dla całego modelu domenowego, POSCAR jest lossy (format VASP nie ma pojęcia
  `label`/`magnetization` per-atom w standardowy sposób).

**Testy:**
- `tests/IO/CrystalStructureYamlIOTests.cpp` — roundtrip: zbuduj `CrystalStructure` ze wszystkimi
  polami wypełnionymi (włącznie z bondami, selective dynamics), zapisz, wczytaj, porównaj pole po
  polu (`EXPECT_FLOAT_EQ`/`EXPECT_EQ` per pole, nie `operator==` na całej strukturze — taki operator
  nie istnieje i nie dodawaj go w tym commicie bez wyraźnej potrzeby).

---

## C10 — Testy roundtrip end-to-end

**Cel:** spiąć C7+C8 razem, potwierdzić że parser+serializer są ze sobą zgodne.

**Pliki:**
- `tests/IO/PoscarIOTests.cpp` — dodaj testy: wczytaj fixture POSCAR przez `ParsePoscarFile`, zapisz
  przez `WritePoscarText`, wczytaj ponownie, porównaj `CrystalStructure` (lattice, atoms per-atom
  `species`/`fractional`/`selectiveDynamics`) z tolerancją odpowiadającą `precision` użytemu przy
  zapisie (np. `precision=16` → tolerancja `1e-10`). Osobny test dla pliku z ujemną skalą (target
  volume) — po roundtrip finalna objętość komórki musi się zgadzać z oryginałem w granicach
  tolerancji numerycznej.
- Test **cross-format**: wczytaj ten sam plik POSCAR przez natywny `ParsePoscarFile` (C7) i przez
  `PymatgenBridge`/`ConvertPymatgenStructureToCrystalStructure` (istniejąca ścieżka) — porównaj
  `lattice`/`atoms[].fractional` między obiema ścieżkami (tolerancja `1e-4`, bo pymatgen może inaczej
  zaokrąglać). To wyłapie rozjazd interpretacji formatu między dwoma parserami **zanim** ktoś
  zacznie ich używać zamiennie w T08+.

---

## C11 — Zapisane widoki kamery (multiple saved views, cykliczne przełączanie)

> Uwaga: to nie jest Domain, tylko Renderer/Input — osobna kolejka review, nie mieszaj z C1-C10
> w jednym PR. Umieszczam tu, bo doprecyzowanie dotyczyło tego samego fragmentu TODO co reszta.

**Cel (doprecyzowany):** nie jeden zapisany widok, tylko **lista** zapisanych widoków kamery.
Jeden klawisz cyklicznie przechodzi do przodu po liście (z zawinięciem na początek), `Alt`+ten sam
klawisz — do tyłu.

**Przed startem:** sprawdź aktualny stan `SetCameraViewCommand`
(`src/Renderer/Commands/SetCameraViewCommand.cpp`) i `RendererWindowState::viewUndoHistory`/
`viewRedoHistory` (`src/Renderer/RendererWindowState.hpp:50-51`) — w code review z 2026-07-01 były
oznaczone jako P0 (dwa równoległe systemy undo dla widoku kamery). Jeśli to nadal aktualne w chwili
pracy nad tym commitem, **podłącz zapisane widoki do tego samego mechanizmu co reszta zmian
kamery** (cokolwiek to jest po rozwiązaniu tamtego P0) — nie twórz trzeciego, równoległego systemu
historii kamery.

**Pliki:**
- `src/Renderer/RendererWindowState.hpp` — dodaj:
  ```cpp
  std::vector<RendererViewSnapshot> savedViews;      // wykorzystuje istniejący RendererViewSnapshot (RendererTypes.hpp:55-58)
  std::size_t activeSavedViewIndex = 0;                // indeks ostatnio odwiedzonego zapisanego widoku; niezdefiniowany dopóki savedViews.empty()
  ```
- `Core/Commands` — trzy nowe komendy (rejestrowane tam, gdzie dziś rejestrowane są istniejące
  komendy widoku renderera — sprawdź `src/Renderer/Commands/RendererViewportCommands.cpp` jako wzorzec):
  - `renderer.view.save_current` — dopisuje bieżący `RendererViewSnapshot` (kamera aktywnego okna)
    na koniec `savedViews`, `activeSavedViewIndex = savedViews.size() - 1`. Domyślny klawisz:
    **do ustalenia w Settings, zaproponuj `Shift+V`** (nie koliduje z niczym w istniejącej mapie —
    zweryfikuj przez `KeymapResolver::GetConflicts()` po dodaniu, zgodnie z T12 (już zaimplementowane
    wykrywanie konfliktów, patrz `src/Core/Input/KeymapResolver.cpp:67`)).
  - `renderer.view.cycle_next` — jeśli `savedViews.empty()`, no-op. W przeciwnym razie
    `activeSavedViewIndex = (activeSavedViewIndex + 1) % savedViews.size()`, uruchom istniejący
    mechanizm przejścia kamery (`transitionActive`/`transitionStart*`/`transitionEnd*` w
    `RendererWindowState` — reużyj, nie pisz nowej animacji) do `savedViews[activeSavedViewIndex]`.
    Domyślny klawisz: `V`.
  - `renderer.view.cycle_previous` — analogicznie, indeks `(activeSavedViewIndex - 1 +
    savedViews.size()) % savedViews.size()` (uwaga na arytmetykę `std::size_t` — nie odejmuj
    bezpośrednio bez dodania `savedViews.size()`, bo przy `activeSavedViewIndex == 0` da to
    ogromną liczbę zamiast -1). Domyślny klawisz: `Alt+V`.
- **Persystencja:** `savedViews` żyje tylko w pamięci na czas sesji — `T10` (Project System) jeszcze
  nie istnieje, więc nie ma gdzie tego trwale zapisać. Zostaw `// TODO(T10): persystować savedViews
  w manifeście projektu razem z pozostałym stanem widoku` przy definicji pola.

**UI (opcjonalnie w tym commicie, do decyzji Codex na podstawie rozmiaru diffa):** licznik
`"Widok {activeSavedViewIndex + 1}/{savedViews.size()}"` gdzieś w `RendererPanel`/toolbar, żeby
użytkownik widział gdzie jest w liście — jeśli dodanie tego znacząco rozszerza zakres commita,
zostaw jako osobny, mały follow-up i napisz to wprost w PR.

**Testy:** `cycle_next`/`cycle_previous` na liście 3 zapisanych widoków — pełny obrót do przodu
wraca do indeksu 0 (`0→1→2→0`), pełny obrót do tyłu z `0` daje `2` (`0→2→1→0` idąc `previous`).
Test na pustej liście — no-op, brak crasha.

---

## Checklist do code review (dla Ciebie, po każdym PR)

- [ ] Branch nazwany zgodnie z `task/07-data-model/<commit>`, jeden PR = jeden commit z listy wyżej.
- [ ] Build Debug (docelowo MSVC) zielony, output wklejony w PR.
- [ ] `DefectStudioTests` zielony, output wklejony w PR, **żadnego** testu spoza zakresu commita nie
      zmieniono bez wyjaśnienia.
- [ ] Diff nie dotyka plików spoza listy „Pliki" dla danego commita — jeśli dotyka, sprawdź
      uzasadnienie w opisie PR.
- [ ] Żadnych zmian w `premake5.lua` poza C1 (vendor includedirs).
- [ ] Sekcja „Uwagi poza zakresem" w opisie PR — sprawdź, czy nie ma tam czegoś, co powinno być
      osobnym zgłoszeniem/taskiem zamiast dopiskiem.
- [ ] Dla C4/C5/C11: potwierdź czy P0 z code review 2026-07-01 (view-undo duplication, mutowalny
      `RendererPanel::GetWindows()`) faktycznie zamknięte na branchu, z którego Codex startuje —
      jeśli nie, zatrzymaj PR i wróć do tego przed mergem.