# Defect Studio — Spis funkcjonalności

Dokumentacja sporządzona na podstawie przeglądu kodu źródłowego gałęzi `main`.
Przeznaczenie: punkt wyjścia do opisu funkcjonalności przy przepisaniu projektu od zera.

---

## 1. Import / Eksport

### 1.1 Import POSCAR / CONTCAR
- Plik VASP5/6 (linia symboli pierwiastków jako obowiązkowa)
- Obsługiwane sekcje: skala sieciowa (pozytywna i negatywna jako objętość docelowa), wektory sieciowe, symbole pierwiastków, krotności, Selective Dynamics, koordynaty Direct i Cartesian
- Skala ujemna → przeliczona na skalę liniową z docelową objętością komórki
- Koordynaty kartezjańskie automatycznie przeskalowywane przez współczynnik sieciowy przy parsowaniu
- Wczytywanie wielowątkowe (lazy), kolejkowanie operacji ładowania

### 1.2 Multi-import jako Kolekcje
- Wczytanie kolejnego POSCAR dołącza jego atomy jako nową Kolekcję w istniejącej scenie
- Automatyczna konwersja układu współrzędnych nowej struktury do układu sceny (Direct ↔ Cartesian)
- Stem ścieżki pliku staje się nazwą kolekcji

### 1.3 Eksport POSCAR / CONTCAR
- Wybór trybu koordynat (Direct / Cartesian)
- Wybór precyzji (1–16 cyfr po przecinku)
- Opcja Selective Dynamics
- Eksport całej struktury roboczej lub aktywnej Kolekcji
- Automatyczne tworzenie katalogu docelowego

### 1.4 Import CHG / CHGCAR / PARCHG (dane wolumetryczne VASP)
- Multi-blokowe datasety (kilka pól skalarnych w jednym pliku)
- Nagłówek struktury kryształu z pliku CHGCAR — opcjonalne zastąpienie struktury sceny
- Interpretacja VESTA-like dla PARCHG: blok 1 = gęstość totalna, blok 2 = magnetyzacja, kanały pochodne: spin-up, spin-down
- Leniwe ładowanie bloków w tle (wątki robocze)
- Walidacja spójności: porównanie siatki, gatunków, kolejności atomów, pozycji między plikiem wolumetrycznym a bieżącą sceną

---

## 2. Wizualizacja 3D

### 2.1 Viewport OpenGL
- Instancjonowany rendering atomów (sfera per atom)
- Ball-and-stick: rendering wiązań jako walce
- Kamera orbitalna (Blender-like): LMB orbit, MMB pan, scroll zoom, Roll Left/Right
- Obsługa touchpada
- Tryby projekcji: perspektywiczny / ortograficzny
- Indywidualny viewport z osobną kamerą na Render Preview
- Obrót o zadany kąt (przycisk + pole kąta)

### 2.2 Wektory osiowe (Axis Overlay)
- Osie XYZ w kolorach R/G/B w narożniku viewportu
- Tryb globalny i tryb relatywny (osi względem wybranych atomów)
- Konfigurowalne kolory i orientacja

### 2.3 Krawędzie komórki (Unit Cell)
- Toggle pokazujący/ukrywający krawędzie komórki elementarnej
- Kolor konfigurowalny

### 2.4 Siatka (Grid)
- Konfigurowalna płaszczyzna siatki (spacing, half-extent, origin, kolor)
- Automatyczne dopasowanie skali siatki do zakresu struktury przy załadowaniu

### 2.5 Renderowanie powierzchni izoenergetycznych (Isosurface)
- Pojedyncza lub podwójna powierzchnia (A + B)
- Konfigurowalne: wartość iso, kolor, opacność, materiał
- Tryby VESTA-like: positive, negative
- Preview w panelu Volumetrics
- Persystencja stanu powierzchni w manifeście projektu

---

## 3. Edycja struktury

### 3.1 Selekcja
- Kliknięcie LMB — selekcja pojedynczego atomu
- Shift+LMB — dodanie do selekcji
- Box-select (B + drag)
- Select All (Ctrl+A), Clear Selection, Invert Selection
- Frame Selected (`.` lub menu) — kamera obraca się wokół wybranych atomów

### 3.2 Transformacje
- Translate: `G`, następnie opcjonalnie `X`/`Y`/`Z` (wiązanie do osi) lub `Shift+X/Y/Z` (blokada płaszczyzny)
- Rotate: `R`, następnie opcjonalnie `X`/`Y`/`Z`
- Scale: `S`
- Gizmo ImGuizmo (transformacja kursorem)
- Translate przez pola numeryczne w Properties Panel
- Uniform XYZ snap na jedną wartość

### 3.3 Atom — operacje
- Dodaj atom: `Shift+A` (menu) lub menu kontekstowe → pozycja pod 3D cursorem lub w centrum selekcji
- Popup konfiguracyjny przed wstawieniem: wybór pierwiastka, tryb koordynat (Direct / Cartesian)
- Zmiana pierwiastka zaznaczonych atomów
- Usuń: `Delete`
- Ukryj: `H`, odkryj wszystkie: `Alt+H`
- Kopiuj/Wklej/Duplikuj (Ctrl+C / Ctrl+V / Ctrl+D)
- Extract to New Collection — przeniesienie wybranych atomów do nowej Kolekcji

### 3.4 Undo / Redo
- Stos historii zmian sceny (Ctrl+Z / Ctrl+Y lub Ctrl+Shift+Z)
- Snapshoty dla: translacji, rotacji, skalowania, usunięcia, ukrycia, dodania atomu, operacji na kolekcjach

---

## 4. Wiązania (Bonds)

### 4.1 Auto-bond
- Automatyczne generowanie wiązań na podstawie sum promieni kowalencyjnych × próg globalny
- Dynamiczna aktualizacja przy modyfikacji atomów lub progu
- Bez wiązań między atomami z różnych Kolekcji

### 4.2 Kontrola progu
- Globalny slider mnożnika progu
- Tryb per-para pierwiastków z osobnymi mnożnikami (override na parę)

### 4.3 Widoczność wiązań
- Ukryj/pokaż indywidualne wiązania (bez usuwania z modelu)
- Ukryj przy usunięciu atomu

### 4.4 Etykiety długości wiązań
- Tekst z długością wyświetlany wzdłuż walca wiązania w przestrzeni 3D
- Konfigurowalne rozmiar / widoczność

---

## 5. Pomiary

- Odległość między ostatnimi 2 zaznaczonymi atomami — etykieta w viewporcie
- Kąt między ostatnimi 3 zaznaczonymi atomami — etykieta w viewporcie
- Centrum masy (Center of Mass) zaznaczenia (przejście kursora)

---

## 6. Kolekcje (Collections)

- Model Blender-like: atomy należą do dokładnie jednej Kolekcji
- Kontrolki w Scene Outliner: widoczność (eye), blokada selekcji (lock), kolor etykiety
- Zmiana nazwy kolekcji
- Kolekcja staje się źródłem eksportu (Export Active Collection to POSCAR)

---

## 7. Grupy (Groups)

- Niezależny od Kolekcji mechanizm grupowania — atomy mogą być w jednej grupie i dowolnej kolekcji
- Operacje: Create group from selection, Add/Remove selection, Select active group, Delete group

---

## 8. Obiekty sceny (Scene Objects)

### 8.1 Empty
- Punkt pomocniczy w przestrzeni 3D z lokalnym układem osi
- Transformowalny (G/R) jak atom
- "Align active empty Z to selected atoms" — oś Z empty wyznaczona przez zaznaczone atomy
- Align axes to world / to camera view

### 8.2 Origin i Light
- Specjalne obiekty jednoinstancyjne (jeden Origin, jedno Light)
- Origin — punkt obrotu / referencyjny
- Light — pozycja źródła oświetlenia viewportu, transformowalny

---

## 9. 3D Cursor

- Ustawiany przez menu kontekstowe w viewporcie (na płaszczyźnie siatki)
- Może służyć jako pivot do transformacji lub punkt wstawienia atomu

---

## 10. Projekt

- Tworzenie / Otwieranie projektu (katalog roboczy)
- Manifest projektu (`project.yaml`): ścieżka struktury, datasety wolumetryczne, stan wyglądu
- Ostatnie projekty (lista MRU)
- Persystencja stanu sceny: ukryte atomy, ukryte wiązania, overrides kolorów, stan kolekcji, pozycja kamery
- Persystencja ustawień wyglądu per-projekt (kolory atomów, materiały)

---

## 11. Konfiguracja i ustawienia

### 11.1 Element Catalog
- Domyślne kolory i rozmiary per pierwiastek (pełna tablica Mendelejewa)
- Edytowalny z poziomu UI
- Zakładka Periodic Table jako picker

### 11.2 Atom Settings
- YAML: `config/atom_settings.yaml` — globalne domyślne dane wizualne pierwiastków
- Override per-projekt w `PROJECT_ROOT/config/project/project_appearance.yaml`

### 11.3 UI / Viewport Settings
- Styl paneli (ciemny, jasny, szary itd.)
- Spacing scale
- Tło viewportu, kolor siatki, kolor krawędzi komórki, kolor osi
- Tryb oświetlenia: pozycja, intensywność, kolor ambient/diffuse/specular
- Persystencja układu doków ImGui między uruchomieniami

---

## 12. Render Image (F12)

- Renderowanie offscreen do PNG / JPG
- Wybór rozdzielczości (niezależnej od viewportu)
- Prostokąt kadrowania (crop region) w render preview
- Tryb override: białe tło, nadpisanie kolorów atomów
- Spójna skala etykiet wiązań przy różnych rozdzielczościach
- Okno Render Preview z podglądem na żywo i możliwością eksportu

---

## 13. Panele i UI

| Panel             | Opis                                                              |
| ----------------- | ----------------------------------------------------------------- |
| Scene Outliner    | Drzewo sceny: atomy, kolekcje, puste, grupy                       |
| Properties        | Właściwości zaznaczonego obiektu (pozycja, pierwiastek, flagi SD) |
| Actions           | Szybkie akcje sceny                                               |
| Appearance        | Kolory, materiały atomów i wiązań, globalne opcje wyglądu         |
| Viewport Settings | Tło, siatka, oświetlenie, tryb projekcji                          |
| Volumetrics       | Sterowanie danymi wolumetrycznymi i izopowierzchniami             |
| Element Catalog   | Edycja globalnych danych wizualnych pierwiastków                  |
| Periodic Table    | Picker pierwiastków                                               |
| Log / Errors      | Panel logów z poziomami i metadanymi źródła                       |
| Stats             | Liczba atomów, wiązań, wydajność                                  |
| Viewport Info     | Informacje diagnostyczne viewportu                                |
| Shortcuts         | Podgląd wszystkich skrótów klawiszowych                           |
| Settings          | UI, styl, skróty klawiszowe konfiguralne                          |
| Render Preview    | Podgląd renderowanego obrazu                                      |

---

## 14. Skróty klawiszowe (wybrane)

| Skrót                    | Akcja                      |
| ------------------------ | -------------------------- |
| Ctrl+O                   | Open POSCAR                |
| Ctrl+S                   | Export POSCAR              |
| Ctrl+Z / Ctrl+Y          | Undo / Redo                |
| Ctrl+A                   | Select All                 |
| Ctrl+C / Ctrl+V / Ctrl+D | Copy / Paste / Duplicate   |
| G / R / S                | Translate / Rotate / Scale |
| G+X/Y/Z                  | Translate wzdłuż osi       |
| B                        | Box Select                 |
| H / Alt+H                | Hide / Unhide All          |
| Delete                   | Usuń zaznaczenie           |
| Shift+A                  | Dodaj atom (menu)          |
| F12                      | Render Image               |
| N                        | Toggle boczny panel        |
| Tab                      | Wyjście z trybu transform  |

---

## 15. Infrastruktura i narzędzia

- **Wielowątkowość**: `thread-pool` — ładowanie struktury, datasety wolumetryczne, generowanie siatki iso
- **Profiling**: Tracy integration (CPU zones + pamięć)
- **Logowanie**: Per-level z metadanymi (plik, linia, funkcja)
- **Build system**: Premake5, wildcard include — nowe pliki wchodzą automatycznie
- **Skrypty setup**: PowerShell + uv (Python 3.11+) — `Tooling.bat setup`
- **Konfiguracja**: YAML (yaml-cpp) — `config/default.yaml`, `atom_settings.yaml`, `ui_settings.yaml`

---

## 16. Propozycje funkcjonalności — zgodne z duchem projektu

Poniższe funkcje naturalnie wynikają z kierunku projektu (materiały, defekty, VASP, DFT) i nie zostały jeszcze zaimplementowane:

### 16.1 Defekty punktowe (pierwotny cel projektu)
- **Vacancy**: usunięcie atomu ze struktury z oznaczeniem miejsca wakancji w scenie (ghost sphere); eksport struktury z vakantem
- **Interstitial**: wstawienie atomu w pozycji wysokiej symetrii (podpowiedź z symetrii komórki)
- **Antisite**: zamiana pierwiastka jednego atomu na inny z zachowaniem historii
- **Substitutional dopant**: jak antisite, z możliwością maskowania przy eksporcie
- **Eksport z/bez defektu**: szybki toggle "pristine vs defected" dla porównania

### 16.2 Supercell
- Generowanie superceli: powtórzenie komórki N×M×K
- Dopasowanie macierzy transformacji (np. do konkretnej orientacji powierzchni)
- Import superceli i rozpoznanie relacji do prymitywnej komórki

### 16.3 Analiza struktury
- Wyznaczanie grupy przestrzennej i grupy punktowej (integracja z spglib)
- Identyfikacja nieekwiwalentnych pozycji atomowych
- Obliczanie współczynnika packing (APF)
- Wykrywanie nieciągłości sieciowych (atomy poza komórką, duplikaty, za blisko siebie)

### 16.4 Energetyka i DFT (ScientificRuntime)
- Uruchamianie VASP / Quantum ESPRESSO ze sceny (wywołanie przez PythonScriptJob)
- Monitorowanie konwergencji SCF na żywo (parsing OUTCAR / log)
- Import sił na atomy z OUTCAR i wizualizacja wektorów sił
- Formation energy calculator (defekt vs pristine)
- Charge density difference: CHG(z defektem) − CHG(pristine)

### 16.5 Pomiary zaawansowane
- Mapa odległości do wybranego defektu (distance field kolorowanie atomów)
- Dystrybucja długości wiązań jako histogram
- Analiza środowiska koordynacyjnego (Voronoi + polyhedra)
- RDF (Radial Distribution Function) z wykresu wbudowanego

### 16.6 Eksport i integracja
- Eksport do CIF (Crystallographic Information File)
- Eksport do XYZ / extended XYZ (ASE-kompatybilny)
- Eksport do VESTA `.vesta` projektu
- Integracja ASE: roundtrip przez Python (już zaplanowany w ScientificRuntime)
- Siatka k-punktów: generowanie pliku KPOINTS (Monkhorst-Pack, Gamma)
- Eksport POTCAR (wybór pseudopotencjałów per pierwiastek)

### 16.7 Scripting Python (zaplanowany, niezaimplementowany)
- In-process CPython embedding (pybind11)
- Moduł `ds` z submodułami: `ds.scene`, `ds.commands`, `ds.events`, `ds.app`
- REPL wbudowany w UI
- Hot-reload skryptów (file watcher, np. efsw)
- Debugger VSCode / debugpy

### 16.8 Kolaboracja i historia
- Git-like snapshot timeline dla projektu (nie tylko undo stack, ale trwała historia)
- Eksport diff struktury: co się zmieniło względem oryginału (dodane / usunięte / przesunięte atomy)
- Adnotacje i notatki przypisane do atomów lub pozycji w przestrzeni

### 16.9 Rendering
- MSDF / SVG labeling (zaplanowany w T14)
- Multi-viewport (kilka kamer jednocześnie)
- Ray-traced render (opcjonalnie Blender via subprocess)
- Animacja: sekwencja klatek z AIMD lub NEB (interpolacja ścieżek atomów)

---

*Dokument wygenerowany na podstawie analizy statycznej kodu: `src/`, `docs/project-control/`, `config/`, `assets/samples/`.*