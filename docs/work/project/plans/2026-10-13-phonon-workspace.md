# Fonony: wiele defektów, osobne okna, eksport, ryciny

Status: zatwierdzone przez użytkownika 2026-10-13 (decyzje niżej), konsultacja: codex (read-only).
Branch bazowy: `task/109-phonon-workspace` (z `task/105-ccd-analysis`, niezmergowane).

## Decyzje użytkownika (2026-10-13)

- **Przypadek** = jedno przejście GS→ES jednego defektu w jednym ustawieniu (funkcjonał, komórka).
  Warianty (embedding N×N, σ, wzór ZPL, CCD) to zestawy wewnątrz przypadku. Lista płaska.
- **Podświetlenie**: jeden wiersz (ostatnio otwarty zestaw), wczytane zestawy innych rodzajów tylko
  znacznikiem w wąskiej kolumnie. Zrobione: commit 95c3e16.
- **Okna**: trzy, nad wspólną sesją (wzorzec task 101):
  - `Fonony` – przypadki, zestawy, wejścia, obliczenia (tytuł bez zmian: klucz INI/widoczności);
  - `Fonony: mody` – podsumowanie, tabela modów, wizualizacja aktywnego przypadku;
  - `Fonony: wykresy` – zakładki β, S(ℏω), PL, CCD, PL 1D, dyspersja, porównanie; pasek sterowania nad
    wykresami; pokazują zestaw porównania, nie tylko aktywny przypadek; działają bez aktywnego wyniku.
- Bez pytania przyjęte: wsadowo liczą się fonony (z wariantami embeddingu) i analiza CCD jako wybierane
  kroki, sekwencyjnie; ponowne liczenie tego samego wariantu nadpisuje jego katalog.

## Uwagi codex, które zmieniły projekt

- Wejścia z `summary.json` **proponują** przypięcie zestawu do przypadku, nie definiują go (różne σ,
  embedding, ZPL mają te same GS/ES/FC). Przypięcie przez użytkownika jest wiążące.
- Domyślne katalogi wyjściowe (`DefaultDatasetDirectory`: wspólny przodek + rodzaj) kolidują między
  przypadkami – naprawić przed kolejką wsadową. Duplikat przypadku czyści nadpisanie katalogu.
- Przypadek posiada swoje okno superkomórki po ID okna; tytuł to tylko tekst. Dopasowanie
  tytuł+atomy zostaje jako ratunek po restarcie (ID okien nie przetrwają restartu).
- Zadania, dyspersja, podpinanie okna i błędy żyją dziś w panelu, a `Clone()` je kopiuje – przenieść do
  sesji zanim powstaną widoki. Aktualizacja raz na klatkę + odroczony zapis (jak 101). Zamknięcie okna
  wykresów nie zatrzymuje animacji w oknie modów.
- Eksport i ryciny z **jawnych adapterów danych**, nie z przechwytywania wywołań ImPlot (przerywane
  linie rysują się kawałkami, pasma dyspersji mają wspólną etykietę, punkty CCD omijają wrappery).
- Normalizacja „pole” liczona na pełnej siatce przed przycięciem; każda krzywa zapisuje, którego ZPL
  użyła (dziś niedostępny wybór cicho spada na inny).

## Zadania

| Task | Zakres | Stan (2026-10-14) |
|---|---|---|
| 109a | Wspólna `PhononPanelSession` (zadania, dyspersja, okno, błędy, `Update()`/`FlushChanges()`), trzy widoki, EditorLayer jak w 101 | zrobione `43cd734`, GUI niesprawdzone |
| 109b | Przypadki: `phonon_panel.yaml` v2 (`cases`, `activeCase`, migracja v1 → jeden przypadek), przypinanie zestawów (propozycje ze skanu), katalogi bez kolizji, okno per przypadek | zrobione `e652b74`, GUI niesprawdzone |
| 109c | Kolejka wsadowa: zaznaczone przypadki → fonony / CCD sekwencyjnie, status i przyczyna per przypadek, anulowanie, wynik nie przełącza aktywnego przypadku | zrobione `489a5a8`, GUI niesprawdzone |
| 109d | Porównanie: oś PL E albo E − E_ZPL, normalizacja max/pole, źródło ZPL przy krzywej, tabela zbiorcza przypadków (ZPL, ΔQ, S, DW, ℏω_eff, najsilniejszy mod, mody urojone) | zrobione `802cd18`, GUI niesprawdzone |
| 110 | Adaptery serii (wspólne dla ImPlot, TSV i rycin) + „Eksport danych (TSV/CSV)…” przy każdym wykresie fononów, tabela modów i zbiorcza do TSV; opcjonalny nagłówek Origin | zrobione `90daab5 + f380b18`, GUI niesprawdzone |
| 111 | Zalążek rycin: `FigureSpec` (Presentation, bez ImGui) → `figures/<nazwa>.figure.json` → `render_figure.py` (matplotlib, `thesis.mplstyle`, PDF/SVG/PNG) przez `PythonScriptJob`; „Renderuj wszystkie”; pilot S(ℏω)/PL, potem CCD z adnotacjami (strzałki, klamry w danych) | zrobione `802cd18`, GUI niesprawdzone |

Struktury (chalkopiryt, przyciąganie gizmo) – osobny tor, plan po konsultacji z codex.
