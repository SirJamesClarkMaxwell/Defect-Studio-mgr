# Task 84 handoff - hide volumes, przekazanie między maszynami

Stan na 2026-10-07, branch `task/84-hide-volumes`. Przeczytaj
`docs/work/project/tasks/84-hide-volumes.md` - zawiera sekcje "Files that must NOT be touched"
i kryteria odbioru.

Feature: bryła (sfera / prostopadłościan / cylinder) ukrywa atomy w środku albo poza sobą. To
preset renderu per materiał - jeden dla diamentu, inny dla hBN, przenoszony kopiuj-wklej. Cel:
ustawienie widoku defektu albo funkcji falowej to dwa kliknięcia, nie ręczne H po trzystu atomach.

## Gotowe, testy zielone (25/25 nowych)

- **geometria**: `src/Renderer/Scene/SceneHideVolume.{hpp,cpp}` - jeden test kształtu za jedną
  transformacją układu, nie trzy niezależne testy w świecie
- **persystencja**: `src/IO/SceneObjectsHideVolumeYaml.cpp` + mapowanie w
  `SceneObjectPersistence.cpp`; klucze YAML: `kind: SceneHideVolume`, `shape:`, `frame:`
- **maska**: `ApplyHideVolumeMaskToWindowState`, wołana na końcu
  `SceneSystem::PushSelectionAndVisibilityToWindowState`

## Nie zrobione, trzy rundy

1. **UI** - Add menu, wiersz w Scene Outliner z dwoma oczami, parametry w Object Properties. Bez
   tego feature jest nieosiągalny z aplikacji.
2. **Rysowanie bryły w viewporcie** - półprzejrzysty wireframe, reużyj passa płaszczyzny. Cylinder
   potrzebuje uchwytu osi, sfera i box jadą na istniejącym gizmie.
3. **Kopiuj/wklej między strukturami.** Schowka obiektów sceny w repo NIE MA - jest tylko
   `ProjectTreePanel` dla plików. To nowy kod, nie reuse.

## Decyzje zamknięte z użytkownikiem, nie otwierać ponownie

- trzy kształty od razu, nie po kolei
- dwa układy przełączane na bryle: `Anchored` (angstremy, środek idzie za atomami) i `Fractional`
  (ułamki wektorów sieci, skaluje się z komórką)
- bryły żyją per struktura w `scene_objects.yaml`, dzielone kopiuj-wklej, bez osobnej biblioteki
  presetów
- maska **tylko gasi** flagi, nigdy nie zapala, i nie dotyka ECS `VisibilityComponent`. Ręczna
  połowa (H, `HiddenSceneState`, oko w outlinerze) zostaje właścicielem prawdy. Inaczej edycja
  promienia zjada to co użytkownik ukrył ręcznie. Test
  `AManualHideOutsideEveryVolumeSurvivesTheMask` tego pilnuje.
- dwie kolumny bryły mapują się wprost na dwie kolumny atomu: oko wycina z ekranu, kamera wycina
  z eksportu

## Pierwsza rzecz do zrobienia

Pełna suita Release **bez obcinania wyjścia**. Poprzedni przebieg dał 1322 / 1301 zielonych /
4 pominięte, czyli 17 czerwonych, ale log był ucięty do ostatnich 30 linii. Widoczne 3 to
`PointGroupAnalysisBridgeTests` z `'PGIrepSymb' object has no attribute 'frobenius_schur'` - znane,
brakująca metoda w groupy, nie regresja. Pozostałych 14 nikt nie zidentyfikował. Nie ufaj
brancowi póki tego nie wiesz.

## Pułapki, kosztowały czas 2026-10-07

- `scripts/python/build.py` **zwraca 0 przy nieudanej kompilacji**. Log mówi "Kompilacja NIE
  POWIODŁA SIĘ / Liczba błędów: 10", skrypt kończy exit 0 i zostawia stary `.exe`. Czytaj log, nie
  exit code. Wpis w `BACKLOG.md`.
- nie przepuszczaj logów builda przez `grep`/`tail` - stracisz dokładnie te linie które potrzebujesz
- prompt dla Codexa musi mówić **dwie** rzeczy: "don't build" (jego sandbox nie zbuduje MSVC +
  premake) **oraz** "no approval step, don't ask for approval again". Bez drugiej Codex staje i pyta
  o zgodę na design zamiast pisać kod.
- `codex exec resume`: flagi `-C`/`-s`/`-o` idą **przed** podkomendą `resume`, nie po niej
- po dodaniu `.cpp`/`.hpp` uruchom `scripts/Windows/GenerateProjects.bat` - premake globuje źródła
  przy generowaniu, nie przy budowaniu
- `RendererWindowState` jest niekopiowalny; w testach buduj drugi egzemplarz, nie kopiuj
- jak funkcja zwraca wskaźnik w strukturę, nie wołaj jej na tymczasowym - jeden czerwony test
  poszedł dokładnie na to

## Workflow

Claude pisze kontrakt (header + padające testy), Codex implementuje, Claude buduje i weryfikuje.
Przed mergem: `full-build-verify`, `architecture-boundary-review`, ręczne odpalenie aplikacji
i przeczytanie diffa.
