# Moduł Figure: eksport wykresów przez matplotlib ("Origin-light")

Status: propozycja (2026-10-11), do decyzji użytkownika. Konsultacja: codex (read-only).

**Zrobiłbym moduł „Figure” do przygotowywania rycin do pracy: jedna specyfikacja JSON i jeden renderer matplotlib, z ograniczonym edytorem wyglądu.** Taki moduł ma sens: zastąpi powtarzalne przenoszenie TSV do Origina, zachowując dane i ustawienia potrzebne do odtworzenia ryciny.

Obecny eksport jest dobrym punktem startowym. Przycisk w [ElectronicStructurePanel.cpp](D:/STUDIA/Fizyka/02_Master-Studies/Defect-Studio-mgr/src/Presentation/Panels/ElectronicStructurePanel.cpp:399) wywołuje `ElectronicStructureSession::ExportOccupationDiagramImage`. Sesja filtruje orbitale, przesuwa energie względem VBM, buduje JSON i uruchamia `electronic_structure_plot.py` przez `ScriptRunner`. Wywołanie jest synchroniczne, a Python osobno odtwarza geometrię diagramu — tych dwóch cech nie przenosiłbym do nowego modułu.

**Architektura: wspólny renderer, małe adaptery po stronie C++.**

Przepływ: **wyniki obliczeń + ustawienia widoku → snapshot wykresu → FigureSpec → JSON → PythonScriptJob → matplotlib → plik**.

- `Presentation/Figures/FigureSpec.hpp`: opis ryciny, bez zależności od ImGui/ImPlot. To model prezentacji, więc **nie Domain**.
- `Presentation/Figures/`: przygotowanie specyfikacji i ustawień eksportu. Dane liczbowe przygotowane dla ImPlot i eksportu powinny pochodzić ze wspólnych funkcji, zamiast dwóch implementacji tego samego przekształcenia.
- `IO`: odczyt/zapis tekstu przez istniejący `TextFileIO`. Konwersja wyników fizycznych na wykres pozostaje poza IO; IO nie musi znać typów z Presentation.
- `ScientificRuntime`: istniejące `PythonScriptJob` i `ScriptRunner` uruchamiają renderer. **Nie potrzeba nowego mechanizmu procesów ani embedded Python.**
- `App`: rejestruje komendę i składa zależności; zakończenie zadania wraca przez istniejące zdarzenia, a stan UI aktualizuje główny wątek.

Specyfikacja powinna być **małym kontraktem dla naszych wykresów**, bez udostępniania całego API matplotlib:

- `schema_version`, wymiary w cm, fonty i wielkości w punktach, tło;
- osie z identyfikatorami: etykiety, jednostki, zakresy, linear/log, ticki, druga oś Y;
- serie `line`, `scatter`, `segments`, `bands`, `text`, `arrows`, `heatmap`;
- stabilne ID serii, kolor, grubość, kreskowanie, marker, przezroczystość, kolejność rysowania i legenda;
- dla pasm: kierunek i gradient; dla heatmapy: współrzędne siatki, orientacja, skala kolorów i colorbar;
- pozycje adnotacji w danych lub względem osi, przesunięcia tekstu w punktach — bez utrwalania pikseli okna.

C++ przekazuje gotową obwiednię, CTL, przesunięte energie i wybrany przekrój koncentracji. **Python rysuje; nie przelicza fizyki.** Eksport respektuje wybrane μ, T/λ, filtry, continuation lines i widoczność serii. Braki danych zapisujemy jako `null`, zachowując przerwy w liniach; nie zamieniamy ich na zera.

JSON przekazywałbym **przez plik**, a w argumentach procesu tylko ścieżki. Dzisiejszy JSON w argumentach nie nadaje się do większych heatmap.

**Zakres „Origin-light”: ustawienia potrzebne do publikacji.**

- Preset `Thesis`: białe tło, czytelne kolory i kreskowanie, spójne fonty.
- Szerokość i wysokość w cm; startowe presety np. 8,5 cm oraz 16 cm. Szerokość jednokolumnowej pracy A4 dopasowujemy do jej pola tekstowego — A4 samo jej nie określa.
- Edycja etykiet osi, nazw w legendzie, zakresów, ticków, kolorów, grubości i stylu linii; legenda wewnątrz lub poza osiami.
- Font i MathText domyślnie. Pełny LaTeX dopiero jako opcja po sprawdzeniu instalacji: MathText nie wymaga TeX-a, pełny LaTeX wymaga dodatkowych narzędzi i działa wolniej. [Dokumentacja MathText](https://matplotlib.org/stable/users/explain/text/mathtext.html), [LaTeX](https://matplotlib.org/stable/users/explain/text/usetex.html).
- PDF domyślnie, SVG i PNG dodatkowo; DPI dla PNG i elementów rastrowych.
- Zapis `figures/nazwa.figure.json` obok projektu: **snapshot danych + rozwiązane ustawienia stylu**, źródła danych oraz wersja renderera/matplotlib. Ponowny render nie wymaga otwierania obliczeń.
- Aktualizacja snapshotu z bieżących wyników jako jawna akcja. Zapisana rycina nie zmienia się po cichu.

Nie robiłbym edycji danych, fittingu, arkusza, importera dowolnych TSV, konsoli Python ani swobodnego układania obiektów jak w programie graficznym. TSV pozostaje użyteczne do analizy poza aplikacją.

**Podgląd edytora: PNG z matplotlib renderowany w tle i wyświetlany jako tekstura.**

ImPlot zostaje do interaktywnego oglądania wyników. W panelu Figure ważniejsze są rzeczywiste fonty, odstępy, legenda i adnotacje. Podgląd ImPlot nie pokaże wiarygodnie tych ustawień.

Render po zatwierdzeniu pola lub krótkiej bezczynności, np. 400–600 ms; podczas pracy pozostaje poprzednia miniatura. Jedno aktywne zadanie, oczekująca najnowsza wersja specyfikacji i odrzucanie starszych wyników. Początkowo subprocess na render; stały worker dopiero, jeśli pomiar opóźnień to uzasadni. PNG i eksport korzystają z tej samej funkcji budującej figurę; PDF/SVG zapisują właściwe backendy. [Dokumentacja zapisu](https://matplotlib.org/stable/api/_as_gen/matplotlib.pyplot.savefig.html).

**Plan wdrożenia:**

1. **Eksport bez edytora.** Przycisk przy każdym wymienionym wykresie → bieżący snapshot → preset Thesis → PDF i zapis specyfikacji. Najpierw pilot E_f(E_F) i occupation diagram, następnie CTL, koncentracje/heatmapa, convergence/KS oraz FNV. Faza kończy się dopiero po pokryciu wszystkich wykresów i przeniesieniu istniejącego eksportu.
   
   | Nowy plik / grupa | Orientacyjny rozmiar |
   |---|---:|
   | `Presentation/Figures/FigureSpec.hpp` | 120–180 linii |
   | `Presentation/Figures/FigureSpec.cpp` — JSON i walidacja | 200–300 |
   | `Presentation/Figures/FigureExport.{hpp,cpp}` — integracja eksportu | 150–250 |
   | `scripts/python/examples/render_figure.py` | 350–500 |
   | Adaptery przy istniejących modułach wykresów | łącznie 600–1000 |
   | Testy C++/Python i małe fixtures JSON | 250–400 |

   Razem około **1,7–2,6 tys. linii**, plus podłączenie komendy i dystrybucji skryptu. Testujemy szczególnie drugą oś, przerwy danych, logarytmy, orientację heatmapy, gradienty i zajętości/spin. Renderer waliduje specyfikację i zapisuje wynik bez uszkadzania wcześniejszego pliku przy błędzie.

2. **Edytor pojedynczej ryciny.** `Presentation/Panels/FigurePanel.{hpp,cpp}` oraz `Presentation/Figures/FigurePreview.{hpp,cpp}`: ustawienia, podgląd, zapis/otwarcie specyfikacji i eksport PDF/SVG/PNG. Około **650–1000 linii**; `.cpp` dzielone zgodnie z limitem projektu. Zachowujemy zadany rozmiar fizyczny: bez automatycznego przycinania zmieniającego szerokość pliku, marginesy mieszczą opisy wewnątrz figury.

3. **Ryciny a/b/c.** Rozszerzenie istniejącej specyfikacji i panelu o prostą siatkę, kolejność paneli, wspólne osie i etykiety a/b/c. Składamy zapisane specyfikacje, bez ponownych obliczeń. Około **300–500 linii** plus test układu.

Nie edytowałem plików ani nie uruchamiałem renderów; rozmiary są szacunkami projektowymi.  
Ryzyko: dostępność fontów i wersja matplotlib wpływają na wygląd, a opóźnienie zimnego subprocessu trzeba zmierzyć przed dopracowaniem podglądu.