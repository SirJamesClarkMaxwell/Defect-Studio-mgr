# Plan sesji: SceneOutliner / ObjectProperties / wiązania / displacement arrows

> Branch: `task/08-scene/outliner-bonds-displacement` (utworzony z `main` @ `8f65077`,
> 2026-08-23). Ten plik istnieje po to, żeby nowy chat (bez historii tej rozmowy) mógł wejść w
> projekt i w tę konkretną partię pracy bez doczytywania całego `TODO.md`. `TODO.md` zostaje
> główną, żyjącą listą zadań całego projektu — ten plik to jednorazowy plan implementacyjny dla
> jednej działki pracy, docelowo do skasowania/zarchiwizowania po zamknięciu brancha (albo
> zostawienia jako historyczna notatka, jak inne pliki w `docs/work/project/plans/`).

## Orientacja w projekcie (dla nowego chata)

Defect Studio to C++23 klon VESTY do wizualizacji struktur krystalicznych i defektów (DFT/VASP).
Kluczowe punkty zaczepienia:

- **`docs/work/project/TODO.md`** — główny, żyjący plan (Polish, ~1300+ linii), podzielony na
  zadania `T01`…`T15` + `Backlog`. Każdy task ma branch `task/NN-nazwa/...`. Czytaj sekcję `T08`
  przed dalszą pracą — tam żyje kontekst dla większości zadań z tego planu.
- **ECS (`entt`)**: `SceneRegistry` (`src/Renderer/Scene/SceneRegistry.hpp`) — właściciel
  `entt::registry` + wektory indeksów `AtomEntities()`/`BondEntities()`/`LabelEntities()`.
  Komponenty w `src/Renderer/Scene/SceneComponents.hpp`. `SceneSystem`
  (`src/Renderer/Scene/SceneSystem.{hpp,cpp}`) — czyste funkcje mostkujące ECS ↔ płaski hot-path
  `RendererWindowState`/`RendererStructureData` (GPU).
- **Domain vs renderer**: `CrystalStructure`/`AtomSite`/`Bond` (domain, `src/Domain/Crystal/`) to
  źródło prawdy, persystowane, przechodzą przez `Core/Undo`. `RendererStructureData`/
  `RendererAtomData`/`RendererBondData` (`src/Renderer/RendererTypes.hpp`) to build'owany z domeny
  hot-path do GPU, **nie** edytuj go bezpośrednio — mutacje idą przez domain + rebuild.
- **Komendy**: `Core/Commands` (`ICommand`/`CommandRegistry`/`CommandContext`), rejestrowane w
  `src/Renderer/Commands/RendererCommandRegistration.cpp`, keybindingi w
  `install/users/default/config/keybindings.yaml`. Wzorzec edycji atomów (Delete/Duplicate/
  Copy/Paste/Transform) — `src/Renderer/Commands/RendererAtomEditCommands.cpp`, **to jest wzorzec
  do skopiowania** dla nowych komend mutujących strukturę (patrz niżej).
- **Undo**: dwa piętra. Globalny `Core/Undo`/`Ctrl+Z`/`Ctrl+Y` dla mutacji domenowych (atomy,
  wiązania). Lokalny per-window snapshot-undo (`Ctrl+Alt+*`) dla stanu renderer-only
  (kamera, piny etykiet) — **nowe zadania z tego planu to mutacje domenowe, więc idą przez
  `Core/Undo`**, nie przez lokalny wzorzec.
- **Build**: `build/generated/vs2022/DefectStudio.sln`, MSBuild. Weryfikacja po nietrywialnej
  zmianie: `MSBuild build\generated\vs2022\DefectStudio.sln /t:DefectStudio /p:Configuration=Debug
  /p:Platform=x64` + `/t:DefectStudioTests` + uruchomienie
  `build\bin\Debug-windows-x86_64\DefectStudioTests\DefectStudioTests.exe`. **Merge do `main`
  tylko po pełnym Debug + Release build** (reguła projektu, zob. `TODO.md` nagłówek).
- **Python bridge** (`ScientificRuntime/Python/`): `PymatgenBridge` (domyślny loader),
  `PuntukasBridge` (zewnętrzne, prywatne narzędzie `punktukas-tools`, **nigdy nie vendorować**,
  opcjonalna zależność z fallbackiem). Nowe structure loady idą przez `PuntukasBridge`, nie
  `PymatgenBridge` (utrwalona preferencja z pamięci sesji).

## Kolejność w wiadomości użytkownika vs rekomendowana kolejność implementacji

Użytkownik wymienił zadania 0–7 w tej kolejności; **0 (displacement arrows) jest jednak
najmniej domenowo zbadane i ma otwartą decyzję poza zasięgiem tego repo** (patrz niżej) — nie
blokuj na nim reszty. Rekomendowana kolejność: **6 → 3 → 1 → 2 → 5 → 4 → 7 → 0**, bo:

- **6 (BondComponent Selection/Visibility)** jest fundamentem pod **3** (manual bond — ukrycie
  pojedynczego wiązania) i pod **7** (`Ctrl+1..4`) — rób pierwsze.
- **3 (manual bond)** jest zaskakująco tani — model domenowy (`BondOrigin`, `Bond::visible`) **już
  istnieje**, patrz sekcja 3 niżej. Duży zwrot za mały koszt, rób wcześnie.
- **1 (SceneOutliner)** i **2 (ObjectProperties)** to czysta nowa infrastruktura UI, zero
  zależności od siebie nawzajem poza wspólnym wzorcem panelu — mogą iść równolegle, ale
  Outliner ułatwia testowanie wszystkiego co potem (widoczność na liście).
- **5 (add atom popup)** korzysta z tego samego wzorca komend co **3** (`ApplyInterstitial` +
  `RebuildAndSync`) — sensownie zaraz po 3, świeże w pamięci.
- **4 (Element Catalog)** niezależne, czysty UI nad istniejącymi danymi — filler.
- **7 (`Ctrl+1..4`)** trywialne po 6.
- **0 (displacement arrows)** na koniec — patrz otwarta decyzja w sekcji 0.

---

## 0. Displacement arrows między wskazanymi plikami

**Zmiana zakresu względem `TODO.md`:** oryginalny wpis zakładał POSCAR→CONTCAR (ta sama struktura,
ta sama liczba/kolejność atomów, czysta relaksacja). Przykład użytkownika (**V_2 vs V_2CBCN**)
to porównanie **dwóch różnych kompleksów defektowych** — prawdopodobnie różna liczba i/lub skład
atomów (V_2 = diwakancja, V_2CBCN = ta sama diwakancja wypełniona podstawieniami C@B/C@N — inna
liczba atomów niż V_2, inny skład niż pryształ). To nie jest już "distance dla pary o tym samym
indeksie" — to problem **korespondencji atomów między dwiema niekoniecznie izomorficznymi
strukturami**.

### Co już istnieje

- `PuntukasBridge::LoadStructure(path)` (`src/ScientificRuntime/Python/PuntukasBridge.{hpp,cpp}`)
  ładuje **jedną** strukturę przez subprocess do `puntukas`, zwraca `PymatgenStructureData` (ten
  sam kontrakt co `PymatgenBridge`). Zero funkcji porównania dwóch struktur dziś.
  `puntukas.atoms.base.AtomsBase.get_distances(p1, p2, pbc=True)` (referencja z `TODO.md`) liczy
  minimum-image distance dla **znanej już pary indeksów** — nie rozwiązuje korespondencji.
- Instancing wiązań (cylindry) już istnieje i jest reużywalny dla grotu strzałki (inny mesh na
  czubku, ten sam wzorzec co bond rendering) — potwierdzone w `TODO.md`, niezweryfikowane w tej
  sesji które dokładnie pliki.

### Otwarta decyzja — **wymaga rozmowy z użytkownikiem, nie rozwiązywalna czytaniem tego repo**

`punktukas-tools` jest prywatnym narzędziem poza tym repo (`C:\Users\fzabi\punktukas-tools`) —
nie widzę jego źródeł stąd i **z zasady nie wolno go vendorować**. Zanim ruszy implementacja,
trzeba ustalić:

1. **Czy `puntukas` już ma gotową funkcję dopasowania atomów między dwiema różnymi strukturami**
   (np. czymś takim użytkownik już analizuje V_2 vs V_2CBCN w notebookach)? Jeśli tak — owinąć to
   przez subprocess bridge (najtańsza ścieżka, wzorzec `ScriptRunner`/`PuntukasBridge`).
2. **Jeśli nie — proponowany fallback: dopasowanie po najbliższym sąsiedztwie.** Dla każdego atomu
   w strukturze B szukaj najbliższego atomu w strukturze A w promieniu odcięcia (analogicznie do
   cutoffu auto-bond w `BondGenerator.cpp` — promienie kowalencyjne z `ElementPropertiesTable`,
   albo prościej: stały promień rzędu połowy najmniejszej stałej sieci). Atomy bez dopasowania
   (wakancja w jednej ze struktur, podstawienie bez odpowiednika) dostają brak strzałki — ewentualnie
   osobny marker; **`AtomStyleTable`/`VacancyRenderStyle`
   (`src/Renderer/AtomStyleTable.hpp`) już ma koncept "widmowego" renderowania, da się reużyć
   stylistycznie**. Dopasowania powyżej sensownego progu przesunięcia (prawdopodobnie fałszywy
   match) też odrzucić.
3. **UX wskazania plików.** Zero istniejącej infrastruktury multi-plikowego porównania.
   `ProjectTreePanel` dziś obsługuje drag-drop pojedynczego pliku (WAVECAR) — brak potwierdzonego
   multi-select. Najniższe ryzyko na start: osobny dialog z dwoma file-pickerami (reference +
   comparison), wzorzec NFD (nativefiledialog, już zvendorowany, używany gdzie indziej do Open).
   Struktura "porównawcza" nie potrzebuje własnego okna renderera — jednorazowy load przez
   `PuntukasBridge`, dopasowanie, lista `(startPos, endPos, displacementMagnitude)` nakładana na
   już otwarte okno referencyjne.
4. **Czy `BondGenerator` ma już poprawną minimum-image odległość w C++** (periodic bond
   generation, `RegenerateAutoBonds`) — jeśli tak, część matchingu można zrobić bez Pythona wcale
   dla struktur o tej samej komórce; do zweryfikowania jako pierwszy krok implementacji, nie
   zakładać z góry.

**Rekomendacja:** zacząć tę pozycję od rozmowy z użytkownikiem o punkcie 1 (co `puntukas` już
potrafi) zanim jakikolwiek kod powstanie — inaczej realne ryzyko zbudowania dopasowania po
sąsiedztwie, które `puntukas` już ma gotowe i lepiej przetestowane.

---

## 1. SceneOutliner panel

**Cel:** lista struktur w oknie, toggle widoczności, F2 rename. Zero Collections (odrzucone
2026-08-22 — osobne okna renderera już dają izolację) — jedna struktura per okno, więc lista jest
prostsza niż w oryginalnym pomyśle z `old-ds-functionality.md` §"Outliner+kolekcje".

**Nowe pliki:** `src/Presentation/Panels/SceneOutlinerPanel.{hpp,cpp}`, wzorzec do skopiowania —
`TextEditorPanel.{hpp,cpp}` (najświeższy dodany panel, `IPanel` + rejestracja w
`PanelRegistry.cpp`, patrz jak tamten panel dostaje dane z `RendererLayer`).

**Zawartość v1:**
- Lista okien rendererowych z `RendererLayer` (każde okno = jedna struktura dziś, bez Collections).
- Toggle widoczności per okno/struktura — czy chodzi o widoczność całej struktury (pokaż/ukryj
  cały viewport?) czy widoczność per-obiekt w strukturze? **Do potwierdzenia z użytkownikiem** —
  `TODO.md` linia o "Ukryj (H)/odkryj wszystkie (Alt+H)" sugeruje że per-okno toggle-all już
  istnieje jako skrót; Outliner to raczej UI nad tym samym mechanizmem, nie nowy model.
- F2 rename — okna renderera potrzebują nazwy edytowalnej przez usera (dziś zapewne nazwa pliku).
  Sprawdzić czy `RendererWindowState` ma już pole na display name, czy trzeba dodać.

**Zależności:** żadne blokujące. Ułatwia testowanie 3/6/7 (widać stan zaznaczenia/widoczności na
liście podczas testów ręcznych).

---

## 2. ObjectProperties panel

**Cel:** właściwości zaznaczonego atomu — numeryczne pola transformu (translate X/Y/Z, uniform
snap), docelowo też miejsce na per-atom customization (patrz `TODO.md` "Replanning 2026-08-22").

**Nowy plik:** `src/Presentation/Panels/ObjectPropertiesPanel.{hpp,cpp}`, ten sam wzorzec co wyżej.

**Zawartość v1:**
- Czyta `windowState.selectedAtomIndices` z aktywnego/fokusowanego okna (wzorzec:
  `RendererLayer::GetFocusedViewportWindowId()`, już używany gdzie indziej, np.
  `RendererAtomEditCommands.cpp`'s `ResolveAtomEditTarget`).
- Pola liczbowe translate — commit przez ten sam mechanizm co gizmo drag
  (`TransformSelectedAtomsCommand`, `RendererAtomEditCommands.cpp:366`) żeby undo/redo działało
  identycznie jak drag gizma, nie osobna ścieżka mutacji.
- Multi-select: pokazuj tylko gdy `selectedAtomIndices.size() == 1` w v1 (edycja pojedynczego
  atomu), grupowa edycja to rozszerzenie na później — nie projektować teraz.

**Zależności:** żadne blokujące na start. Per-atom customization (kolor/rozmiar per instancja) z
`TODO.md`'a wyląduje w tym panelu później, ale to osobne zadanie (nie w tym batchu 0–7).

---

## 3. Manual bond add/remove (`J`)

**Duże odkrycie tej sesji: model domenowy już to przewiduje.** `src/Domain/Crystal/
CrystalPrimitives.hpp`:

```cpp
enum class BondOrigin { Auto, Manual };
struct Bond
{
    std::size_t firstAtomIndex, secondAtomIndex;
    float lengthAngstrom;
    BondOrigin origin = BondOrigin::Auto;
    bool visible = true;
    glm::ivec3 periodicShift;
};
```

I `BondGenerator.cpp::RegenerateAutoBonds` **już tylko kasuje/przelicza bondy `BondOrigin::Auto`**
— `Manual` bondy przeżywają każdy regen nietknięte. To znaczy: to zadanie jest w większości
**samym wiring, nie nowym modelem danych.**

**Do zrobienia:**
1. Nowa komenda `ConnectSelectedAtomsCommand` (`RendererAtomEditCommands.cpp`, wzorzec:
   `PasteAtomsCommand` na linii 602 — resolve target, snapshot `atoms`+`bonds` do undo, mutacja,
   `RebuildAndSync`). Wymaga dokładnie 2 zaznaczonych atomów; push `Bond{firstIdx, secondIdx,
   length=glm::distance(...), origin=Manual, visible=true}` do `structure.bonds`. **Nie** woła
   `RegenerateAutoBonds` (to by zresetowało auto bondy bez potrzeby) — tylko dopisuje jeden Manual.
2. Komenda usuwania — `Bond::origin == Manual` → erase z listy; `Bond::origin == Auto` → **nie
   erase, tylko `visible = false`** (patrz pkt 4, realny gap).
3. Rejestracja `renderer.bonds.connect` na klawisz `J` w `keybindings.yaml`
   (`renderer.viewport.focused` context) — `F` już zajęte przez flip pinu etykiety (rozwiązane
   2026-08-22), **nie używać `F` ponownie**.
4. **Realny architektoniczny gap do rozwiązania, nie tylko wiring:** `Bond::visible = false` na
   auto-bondzie **nie przetrwa** kolejnego `RegenerateAutoBonds` — ta funkcja erasuje WSZYSTKIE
   `Auto` bondy i buduje je od zera z `visible` domyślnie `true`, więc ukryty auto-bond
   "zmartwychwstanie" widoczny przy najbliższej edycji atomu (Delete/Duplicate/Paste/Transform
   wszystkie wołają `RegenerateAutoBonds`). **Rekomendacja:** przed erase w
   `RegenerateAutoBonds`, zbuduj mapę `(firstAtomIndex, secondAtomIndex, periodicShift) →
   visible` ze starych `Auto` bondów, i po rebuildzie zaaplikuj ją na nowe — kilka linii, ale
   koniecznie **przed** wypuszczeniem "hide single bond" jako działającej funkcji, inaczej to
   będzie subtelnie zepsute (znika po dowolnej niepowiązanej edycji atomu).
5. `RendererBondData` (`RendererTypes.hpp:30`, render hot-path) **nie ma pola `visible`** —
   trzeba dodać, i filtrować w budowaniu instancji cylindrów bond (sprawdzić
   `StructureRendererDataBuilder.cpp` — build z domain `Bond::visible` → `RendererBondData::visible`,
   plus filtr przy tworzeniu GPU instances, analogicznie do jak `RendererAtomData::visible` już
   działa dla atomów).

**Zależności:** żadne blokujące na sam connect. Hide-single-bond (pkt 4/5) korzysta z tego samego
modelu, warto zrobić razem skoro i tak dotyka `RendererBondData`.

---

## 4. Element Catalog editor panel

**Cel:** tabelaryczny UI edytujący globalną tabelę kolor/promień per pierwiastek. Dane już
istnieją (`AtomStyleTable`, `src/Renderer/AtomStyleTable.hpp` — `unordered_map<symbol,
AtomRenderStyle{color, displayRadius}>`), różne od Periodic Table pickera
(`RendererPanel::drawPeriodicTableWindow`, wybór pojedynczego pierwiastka, nie edycja stylu).

**Realny gap:** `AtomStyleIO` (`src/IO/AtomStyleIO.hpp`) ma tylko `LoadFromFile`/`ParseYaml` —
**zero funkcji zapisu.** Panel edycji bez zapisu to demo, nie feature — `AtomStyleIO::SaveToFile`
(nowa funkcja, odwrotność `ParseYaml`) jest częścią tego zadania, nie osobnym follow-upem.

**Właściciel danych:** `Application::m_RendererAtomStyleTable`
(`src/App/Application.hpp:158`), załadowany raz w `ApplicationBootstrap.cpp:792`. Komendy
(`RendererAtomEditCommands.cpp`) dostają **kopie przez wartość** (`AtomStyleTable
atomStyleTable` w konstruktorach) — do zweryfikowania przy implementacji: czy edycja w panelu
musi jakoś dotrzeć do już skonstruowanych/zarejestrowanych komend (prawdopodobnie nie, jeśli
komendy są tworzone on-demand z aktualnej `m_RendererAtomStyleTable` przy każdym wywołaniu — do
sprawdzenia w `RendererCommandRegistration.cpp`), i czy zmiana koloru wymaga rebuildu
`RendererStructureData` żeby było widać w viewporcie (prawdopodobnie tak, przez ten sam
`RebuildAndSync` co inne mutacje).

**Zależności:** żadne. Czysty UI + 1 nowa funkcja IO.

---

## 5. Add atom przez współrzędne (popup)

**Wzorzec do skopiowania 1:1:** `PasteAtomsCommand` (`RendererAtomEditCommands.cpp:602`) — insert
nowego atomu to domenowo `ApplyInterstitial(structure, PointDefectOperation{atom})`, dokładnie ta
sama funkcja co Paste używa. Nowa komenda `AddAtomAtCoordinatesCommand` różni się od Paste tylko
źródłem pozycji (pole z popupu zamiast schowka) i tym że wstawia jeden atom z wybranym
pierwiastkiem (potrzebny UI: combo pierwiastka + tryb koordynat frakcyjne/kartezjańskie + radio
"pod 3D cursor" / "w centrum zaznaczenia").

**Do zrobienia:**
1. Popup UI (ImGui modal albo docked mini-panel) z: element combo (reużyć listę z Periodic Table
   pickera), tryb Direct/Cartesian, 3 pola liczbowe, przycisk insert.
2. `AddAtomAtCoordinatesCommand` — wzorzec `PasteAtomsCommand`, jeden atom zamiast listy ze
   schowka. `RegenerateAutoBonds` po insercie (nowy atom może stworzyć auto-bond z sąsiadami —
   Paste już to robi, kopiuj to zachowanie).
3. Punkt wstawienia "pod 3D cursor" — `windowState.cursor3DPosition`/`cursor3DPlaced` już istnieje
   (`RendererWindowState.hpp`, zrobione 2026-08-22), gotowe do użycia bez dodatkowej pracy.
   "W centrum zaznaczenia" — policz centroid `selectedAtomIndices` (wzorzec: gizmo pivot już to
   liczy co klatkę, zobacz `renderTransformGizmo`).

**Zależności:** żadne blokujące, ale robić zaraz po 3 — ten sam wzorzec komend świeży w głowie.

---

## 6. `BondComponent` + `SelectionComponent`/`VisibilityComponent`

**Cel:** fundament pod `Ctrl+1..4` (7) i pod hide-single-bond (3, pkt 5).

**Stan dziś** (`SceneSystem.cpp::SyncSceneWithStructure`, linia ~35-51): pętla tworząca bond
entities dodaje **tylko** `BondComponent` — brak `SelectionComponent`/`VisibilityComponent`,
inaczej niż pętla atomów tuż nad nią (ma oba + `CollectionComponent`, ten ostatni **nie
dodawać** do bondów — Collections odrzucone).

**Do zrobienia:**
1. `SyncSceneWithStructure`: dodaj `entity.AddComponent<VisibilityComponent>(VisibilityComponent{
   bond.visible})` (wymaga `RendererBondData::visible`, patrz zadanie 3 pkt 5 — **rób to razem z
   3, nie osobno**, bo obie zmiany dotykają tej samej pętli) i
   `entity.AddComponent<SelectionComponent>()`.
2. `PushSelectionAndVisibilityToWindowState`: dopisz analogiczny blok dla bondów (dziś iteruje
   tylko `AtomComponent`+`SelectionComponent`+`VisibilityComponent` — potrzebny drugi `view` nad
   `BondComponent` zamiast `AtomComponent`, zapisujący do jakiegoś `windowState.selectedBondIndices`
   — **nowe pole**, nie ma dziś odpowiednika).
3. Bond pick (klik w wiązanie w viewporcie) — **nie istnieje w ogóle dziś**, `handleAtomPick`
   raycastuje wyłącznie sfery atomów. Potrzebny nowy hit-test: odległość punkt-do-odcinka w 3D
   (kapsuła wokół cylindra bondu) albo prostszy screen-space odpowiednik wzorca już użytego w
   `handlePinnedMeasurementInteraction`/gizmo hit-testach (axis screen-projection + perpendicular
   distance) — **do zaprojektowania jako część tego zadania, nie ma gotowego wzorca do
   skopiowania 1:1** (w odróżnieniu od 3/5, gdzie wzorzec już istnieje).

**Zależności:** żadne blokujące na start, ale koordynuj z zadaniem 3 (ta sama pętla w
`SyncSceneWithStructure`, ten sam nowy `RendererBondData::visible`).

---

## 7. `Ctrl+1..4` tryby zaznaczania

**Cel:** atoms / atoms+bonds / bonds+labels (tylko etykiety, do przesuwania gizmem bez ruszania
atomów) / atoms+bonds+labels.

Trywialne po 6: to głównie filtr na to co `handleAtomPick`/przyszły bond-pick/label-pick
faktycznie mogą trafić, sterowany nowym polem w `RendererWindowState` (np.
`atomSelectionModeMask` albo cztery bool flagi) i UI toggle (przyciski w toolbarze albo tylko
skrót, do ustalenia). `1/2/3`/`Alt+1/2/3` zajęte przez align-axis (decyzja 2026-08-21, axis align
zostaje) — `Ctrl+1..4` wolne, zweryfikowane w `keybindings.yaml` wcześniej w tej sesji.

**Zależności:** twardo po 6 (potrzebuje bond `SelectionComponent` żeby "atoms+bonds" tryb miał co
przełączać).

---

## Weryfikacja przed merge do main

Dla każdego zadania z osobna: `MSBuild .../DefectStudio.sln /t:DefectStudio
/p:Configuration=Debug /p:Platform=x64`, `/t:DefectStudioTests`, uruchomić
`DefectStudioTests.exe` (212 testów bazowych + cokolwiek nowego dopisane). Przed finalnym mergem
całego brancha do `main`: pełny Debug **i** Release build (reguła projektu z nagłówka `TODO.md`).
