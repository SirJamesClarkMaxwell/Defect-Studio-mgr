# Handoff - stan pracy (aktualizowany przy każdym pushu)

Ostatnia aktualizacja: 2026-10-20 (seria 108b-e zrobiona; Debug + Release zielone: 1826 testów, 1821 przechodzi, 5 pominiętych). Branch: `task/109-phonon-workspace` (od `task/105-ccd-analysis`, ten od
`dev` 3739e5b). Nic nie jest zmergowane do `dev`/`main`; merge dopiero po sprawdzeniu GUI przez użytkownika i
pełnym buildzie Debug + Release (skill `full-build-verify`).

## Plany (źródło prawdy)

- `docs/work/project/plans/2026-10-13-phonon-workspace.md` - fonony dla wielu defektów (109a-111), tabela stanu.
- `docs/work/project/plans/2026-10-13-structure-building-gizmo.md` - budowanie komórek gizmo (112a-112g), status.
- `docs/work/project/plans/2026-10-14-vibration-animation.md` - uniwersalna „Animacja drgań” (108a-d) z decyzjami.
- `docs/work/project/plans/2026-10-11-figure-module.md` - moduł rycin (matplotlib), 111 to zalążek.
- `docs/work/project/plans/2026-10-20-python-session-and-scripts.md` - inwentarz Pythona + propozycja (sesja ze
  zmiennymi, skrypty z szablonu, debugger VS Code, `ds` read-only), po 2 rundach z codex; czeka na decyzje (§7).
- `docs/work/project/plans/2026-10-20-figures-tsv-origin.md` - ryciny matplotlib + wspólny styl ImPlot/matplotlib
  (`plot_styles.yaml`) + jeden eksport TSV pod Origin (F0-F4), po 2 rundach z codex; pytania w §5 (m.in. test w Originie).
- Zadania: `docs/work/project/tasks/<NN>-*.md`, raporty: `docs/work/project/reports/task-<NN>-*.md`.

## Zrobione (commity na branchu, build Debug + testy zielone, GUI NIESPRAWDZONE)

| Commit | Co |
|---|---|
| 95c3e16 | jeden podświetlony wiersz w tabeli zestawów |
| 43cd734 | 109a: trzy okna `Fonony` / `Fonony: mody` / `Fonony: wykresy` na wspólnej sesji; EditorLayer.cpp podzielony |
| fe8d7d4 | 112a ułamki sieci w G/R/S (`G X 1/8`, Ctrl 1/8, Ctrl+Shift 1/24, `U`), 112b `Shift+D` kopia w miejscu |
| e652b74 | 109b przypadki (phonon_panel.yaml v2, migracja v1) |
| 35e7942, 7f4ab9f | 112c panel Komórka (edycja, powtórz komórkę, zawiń), 112d Dodaj podsieć + nowa komórka |
| 90daab5, f380b18 | 110 DataTableIO + adaptery wykresów i eksport TSV/CSV/schowek |
| a98a8aa, c8c61c3 | 112e przyciąganie do celów + `V` oś z dwóch atomów, 112f `P` punkt obrotu, `D` odległość od atomu |
| 489a5a8 | 109c kolejka wsadowa; zmiana przypadku nie anuluje liczenia |
| 802cd18 | 109d porównanie (oś E_ZPL−E, normalizacja, tabela Porównanie) + 111 ryciny matplotlib |
| 099b894, 0636acb | 108a pole drgań + mieszanie modów zdegenerowanych, 112g Powtórz/Odbij/`Shift+R` |
| 970f8cd | 108b animacja kierunku ΔQ (przejście GS ↔ ES, drganie wokół GS), stan animacji per przypadek |
| d0c7246 | 108c mod bulku w q (`phonon_mode.py`, modulacja phonopy na N×M×K komórki prymitywnej), bulk i dyspersja per przypadek |
| fe0329e | 108d klik/przeciąganie po dyspersji, liczenie po puszczeniu, stałe katalogi `bulk_mode_q…` |
| 3cba960 | 108e porównanie ΔR: ΔQ (CCD), max ΔR, atomy 90 % ΔQ², pokrycie modami, wykres ΔQ²(r), podobieństwo ΔR |

Testy Debug i Release: 1826, 1821 przechodzi, 5 pominiętych (oczekiwane: 2 przez `DS_PYTHON_CAPI_AVAILABLE=0`, 3 wymagające
danych z klastra/dysku O:).

## W toku / następne

Zakres zatwierdzony 2026-10-20 (pamięć `vibration-108-scope`) jest zrobiony: 108b-e + persystencja per przypadek
+ porównanie wielu defektów. Raporty: `reports/task-108b…108e-*.md` (odstępstwa od zadań opisane na końcu).

1. Użytkownik klika GUI (lista niżej) - nic z 108a-e nie było oglądane w aplikacji.
2. Otwarte pomysły (nie zlecone): grupowanie zestawów `bulk_mode_q…` w tabeli zestawów (przy dużej liczbie kliknięć
   na dyspersji), unieważnianie cache porównania po mtime (dziś tylko „Odśwież”), mieszanie 3-krotnych multipletów.
3. Struktury dalej (opcjonalnie): malowanie wzoru pierwiastków, czyszczenie pozycji, powiązane pozycje (spglib).
4. Merge do `dev` dopiero po GUI + pełnym Debug/Release.
5. Python (osobny tor, nic nie zaczęte): plan `2026-10-20-python-session-and-scripts.md`, pytania w §7.
   Znaleziony ukryty błąd: `InteractiveProcess::Terminate()` robi `join` przed zamknięciem joba (etap P0).
6. Ryciny/TSV (osobny tor, nic nie zaczęte): plan `2026-10-20-figures-tsv-origin.md`, pytania w §5.

## Do kliknięcia (GUI)

Fonony: podświetlenie wiersza, 3 okna, przełączanie przypadków, `Policz zaznaczone`, `Porównanie`, `Eksport…`,
`Rycina…`, wykresy wyglądają jak wcześniej (rysowanie przeszło na adaptery), „Animacja drgań” + `Mieszanie θ`,
źródło `kierunek ΔQ` (wymaga ponownego `Policz CCD`, potem `Otwórz geometrię GS`, Ruch, Zasięg, suwak Q),
źródło `mod bulku w q` (FC bulku w przypadku, np. `sjcm_mgr/hBN/bulk/phonons/vasp_661`: `Policz dyspersję` - nowa,
bo stara nie ma `qpoints.tsv` - potem klik na krzywej w `Fonony: wykresy` → Dyspersja, przeciąganie linii,
`Otwórz superkomórkę bulku`, ostrzeżenie o fragmencie i `Użyj N×M×K`), Porównanie: nowe kolumny ΔR, wykres
ΔQ²(r), tabela „Podobieństwo ΔR”; zmiana przypadku przywraca jego źródło/q/gałąź/θ i nic nie startuje samo.
Struktury: `G X 1/8`, `Shift+D`, panel Komórka (podsieć, powtórz 1×1×2), tryb Cele (Ctrl), `V`, `P`, `D`,
`Przekształć` → Powtórz/Odbij, `Shift+R`. Uwaga: dawne `Shift+D` „ustaw widok jako domyślny” jest tylko w palecie.

## Decyzje użytkownika (skrót; szczegóły w planach)

- Przypadek = jedno przejście GS→ES jednego defektu w jednym ustawieniu; warianty = zestawy w przypadku.
- Podświetlenie: tylko jeden wiersz (ostatnio otwarty zestaw), pozostałe wczytane jako znacznik.
- Struktury: NIE generator z grupy przestrzennej/Wyckoffa („vesta już tak robi i nie jest to wygodne”), tylko
  gizmo w stylu Blendera, przyciąganie, transformacje względem atomów.
- 108: niewspółmierne q = fragment + podpowiedź superkomórki; ΔQ = oba tryby z przełącznikiem; suwak mieszania
  modów zdegenerowanych od razu.

## Zasady pracy (nie wszystkie są w AGENTS.md/CLAUDE.md)

- Folder pracy magisterskiej i dysk O: tylko do odczytu; nigdy nie kopiować LOCPOT/CHGCAR/WAVECAR z klastrów;
  nie zlecać zadań na klastrach; po pracy przez OpenVPN zamknąć połączenie.
- Prywatne repo w `Vendor/` (groupy, puntukas_tools2, dephonopy) - gitignored, nigdy nie commitować.
- Push/merge tylko na prośbę użytkownika (prośba 2026-10-14: utrzymywać ten branch wypchnięty na GitHub).
- Nie zamykać DefectStudio.exe użytkownika; nie uruchamiać drugiej instancji, gdy użytkownik pracuje.
- UI: ścieżki względne do projektu (pełna w tooltipie), etykiety z lewej/nad polem, tooltipy wszędzie,
  wyłączone przyciski mówią dlaczego, DragFloat dla wartości ciągłych, jeden akcent podświetlenia.
- Codex: `cat prompt.md | codex exec -C . -s workspace-write -c 'windows.sandbox="unelevated"'
  -c 'model_reasoning_effort="high"' -o result.md -`; w promptach zawsze STYLE RULE (bez przeformatowania)
  i „Do NOT run MSBuild” (zawieszone węzły MSBuild blokują .tlog; budować z `-nodeReuse:false`).
- Build na tej maszynie: `DS_TOOLSET=msc-v143`, `scripts\Windows\GenerateProjects.bat` po dodaniu plików,
  MSBuild z instalacji VS (na laptopie ścieżka może być inna - patrz CLAUDE.md).
