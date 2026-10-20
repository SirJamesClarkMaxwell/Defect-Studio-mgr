# Python: inwentarz i powrót do skryptowania (sesja, zmienne, skrypty z szablonu, debugger VS Code)

> 2026-10-20. **Status: propozycja do decyzji, nic nie zaimplementowane.** Powstało na prośbę:
> „zrób inwestygację tego co jest a czego nie ma w pythonie… IPython local variables, model do pisania
> skryptów (z template) z podpowiadaniem składni i możliwością uruchomienia, podpięcie debuggera z
> vscode. Podyskutuj z codex”. Dwie rundy dyskusji z codex (read-only) + własne spike'i w `.venv`.

## 1. Co było chciane (historia)

| Data | Co | Gdzie |
|---|---|---|
| 2026-08-21 | Python scripting panel: REPL w stylu IPython (podpowiedzi składni i ścieżek), hot reload, debugger VS Code/debugpy, moduł `ds` | `docs/archive/work/project/TODO.md:1382` |
| 2026-08-23 | Rozszerzenie: model obiektowy projektu (`project.okeanos["6-7"]["exc_ms"]…`), konsola, `.py`, `.ipynb`, terminal | `docs/archive/work/project/plans/2026-08-23-python-scripting-console.md` |
| 2026-08-24 | **Zamrożone** („duże, trudne, wolałbym nie zepsuć projektu”). Zamiast tego terminal + konsola-„kalkulator” | `docs/archive/work/project/plans/2026-08-24-calc-tools.md` §3–4 |
| 2026-09-02 | Projekt kreatora struktur: „IPython-style debugging with live variables”, „later: the Python daemon” | `docs/new-structure-wizard-design-2026-09-02.md:204` |
| ADR-003 | „Python is not a first-class user scripting model in MVP” | `docs/adr/ADR-003-python-as-scientific-runtime.md:22` |

Interpretacja prośby: „IPython local variables” = podgląd zmiennych sesji (jak *Variables* w VS Code
albo Spyder). „Model do pisania skryptów” = mechanizm pisania skryptów, nie model AI.

## 2. Inwentarz

**Jest:**
- Około 30 mostów subprocess (`ScriptRunner`/`PythonScriptJob`, skrypty w `scripts/python/examples/`):
  JSON na stdout, zimny start Pythona przy każdym wywołaniu.
- `Core/Platform/InteractiveProcess`: potoki, stdout i stderr w **jednym** potoku, job kill-on-close.
- `ConPtyProcess` + `TerminalPanel`: prawdziwy terminal (VT), wiele paneli.
- `CalculatorConsolePanel` (442 linie): `ipython --simple-prompt` przez potok.
  - Komórka z podświetlaniem Pythona, historia, wstrzyknięte `project_root` i `puntukas`.
  - Podpowiedzi tylko statyczne: słowa kluczowe, builtins i nazwy z `x = …`.
  - Próba prawdziwych podpowiedzi `obj.attr` została wycofana, bo każde zapytanie szło przez REPL i
    zalewało numerację `In[]/Out[]` (`CalculatorConsolePanel.cpp:237`).
- `TextEditorPanel`: jeden plik, Ctrl+S.
  - Brak języka (podświetlania).
  - Otwarcie innego pliku po cichu zastępuje niezapisane zmiany (`TextEditorPanel.hpp:27`).
- `Vendor/ImGuiColorTextEdit`: gotowy `Language::Python()` i **asynchroniczne podpowiedzi**
  (`suggestionsPromise` + `SetAutoCompleteSuggestions`, `TextEditor.h:665,720`).
  `extras/TrieAutoComplete` i `LspBridge` nie są kompilowane.
- Embedded CPython (`PythonInterpreter`, GIL): kod istnieje, ale jest wyłączony
  (`DS_PYTHON_CAPI_AVAILABLE=0`). Moduł nanobind to zabawka (`add`, `BridgeVector3`).
- Datasety na dysku `ds.*.v1` (fonony, dyspersja, CCD, mod bulku), skanowane obok obliczeń
  (`IO/AnalysisDatasetIO`).
- `render_figure.py`: FigureSpec v1, styl pracy dyplomowej.
- `.venv`: IPython 9.16.1, jedi 0.20.0, parso, matplotlib (+ `matplotlib_inline`), ruff, traitlets.
- VS Code: ms-python, ms-python.debugpy (dołączony debugpy 1.8.20), Pylance, Jupyter.

**Nie ma:**
- podglądu zmiennych;
- podpowiedzi zależnych od kontekstu (`obj.attr`) w konsoli i w edytorze;
- uruchamiania pliku `.py` z edytora we wspólnej sesji;
- szablonów i katalogu skryptów projektu;
- `debugpy` w `.venv` i jakiegokolwiek podpinania debuggera;
- przerwania komórki (Ctrl+C); dziś da się tylko zamknąć panel;
- modułu `ds` (ani read-only, ani sceny);
- wykresów z konsoli (`plt.show()`);
- zakładek w edytorze;
- Jupytera.

## 3. Sprawdzone 2026-10-20 (spike w `.venv`, Windows)

- `InteractiveShell.run_cell` + `Completer.completions` pod `provisionalcompleter()`:
  `execution_count` się **nie zmienia**. Przyczyna wycofania podpowiedzi znika, jeśli pytamy powłokę
  bezpośrednio, a nie przez REPL.
- `jedi.Interpreter("zażółć.sh", [user_ns])` podpowiada `shape`. Snapshot zmiennych
  (`user_ns − user_ns_hidden`) daje nazwę, typ i kształt tablic numpy.
- Polskie nazwy i `print` działają z UTF-8 (`PYTHONIOENCODING=utf-8`, docelowo `-X utf8`).
- debugpy 1.8.20 z paczki VS Code nasłuchuje na `127.0.0.1` z naszego `.venv`. Ostrzega, żeby uruchamiać
  z `-Xfrozen_modules=off`, bo inaczej breakpointy mogą nie trafiać.
- `_thread.interrupt_main()` z wątku pobocznego:

  | Kod | Przerwany po |
  |---|---|
  | pętla Pythona | 1,0 s |
  | pętla numpy (matmul) | 1,1 s |
  | `time.sleep(10)` | nieprzerwany (10 s) |

  Wniosek: potrzebny jest przycisk **Restart** (zabicie procesu, utrata zmiennych).
- Codex sprawdził źródło: `ipykernel/parentpoller.py`, `ParentPollerWindows.run()` na Windows też woła
  `_thread.interrupt_main()`. `ipykernel` nie przerwie więc niczego, czego nie przerwie własny host.

## 4. Dyskusja z codex (dwie rundy)

**Runda 1.** Codex zaproponował prawdziwy `ipykernel` za małym proxy Pythona (ZMQ ↔ JSON), żeby C++
nie potrzebował ZMQ. Słuszne uwagi, przyjęte:
- `InteractiveProcess` daje stdout i stderr ten sam potok (`InteractiveProcess.cpp:53–55`), więc sztuczka
  z deskryptorami w hoście nie oddzieli protokołu od wypisów bibliotek C/Fortran. Potrzebny opcjonalny
  osobny stderr.
- `InteractiveProcess::Terminate()` robi `join` wątku czytającego, zanim zamknie job zabijający wnuki
  (`InteractiveProcess.cpp:133`). Wnuk trzymający potok zawiesi zamykanie. Ukryty błąd, ważny przed
  dodaniem debuggera albo kernela.
- `repr()` wykonuje kod użytkownika i może rzucić albo trwać długo. Podgląd zmiennych powinien dawać
  bezpieczne podsumowania znanych typów, a dla reszty tylko `<typ>`.
- Edytor potrzebuje ostrzeżenia o niezapisanych zmianach przed rozbudową.
- ADR-003 wymaga poprawki.
- VS Code nie podłączy się do działającego kernela z pliku połączenia. Da się to zrobić tylko przez
  `jupyter_server` z `allow_external_kernels` i niesprawdzone.

**Runda 2.** Przedstawiłem pomiary i dwa argumenty:
- to samo przerywanie przez `interrupt_main`;
- koszt: 3 nowe zależności (`ipykernel`, `jupyter_client`, `pyzmq`), 2 procesy, a korzyść (wspólna
  przestrzeń nazw z VS Code) niesprawdzona.

Codex zmienił rekomendację: **na pierwszy etap własny host**. Jego poprawki do hosta:
- deskryptor protokołu `os.dup(1)` przed `os.dup2(2, 1)`, nieprzekazywany procesom potomnym;
- kod użytkownika dostaje stdin zwracający EOF (samo podmienienie `builtins.input` nie wystarcza,
  `sys.stdin.readline()` zostaje);
- `sys.breakpointhook` ustawiany jawnie, bo `PYTHONBREAKPOINT` jest ignorowany po nadpisaniu hooka;
- limit liczby i rozmiaru obrazków, bez ponownego zapisywania niezmienionych figur.

**Odrzucone na teraz:**
- `ipykernel`/Jupyter;
- nazwy wiadomości zgodne z Jupyterem (codex: sama pisownia nie daje zgodności, granicą wymiany jest API
  `ConsoleSession` w C++);
- uruchamianie mostów pod debugpy (`DS_PYTHON_DEBUG`);
- automatyczne scalanie `launch.json`.

## 5. Propozycja

### Protokół host ↔ aplikacja (linie JSON, osobny stderr)

| Wiadomość | Pola |
|---|---|
| `request` → host | `id`, `op`, `args` |
| `stream` → app | `id` lub null, `name: stdout\|stderr`, `text` |
| `reply` → app | `id`, `ok`, `data`, opcjonalnie `error{type,message,traceback}` |
| `state` → app | `phase: ready\|busy\|idle`, `active_id`; pierwszy `ready` niesie `protocol: 1` |

**Operacje:**
- `execute{code}`, `run_file{path}`;
- `complete{code, cursor, source: console|editor, path?}` (offsety w znakach Unicode, nie bajtach);
- `interrupt{target_id}`;
- `debug_listen{port}`: zwraca adres i nie czeka na podłączenie.

**Odpowiedzi i zasady:**
- Odpowiedź na wykonanie zawiera `{result_text, variables, figures}`, także po błędzie.
- Wiersz zmiennej: `{name, type, shape?, length?, preview}`.
- Jedno wykonanie naraz; równoległe żądanie dostaje „busy”.
- Restart obsługuje C++: zabija i uruchamia host od nowa.

### Etapy

| Etap | Zakres | Rozmiar |
|---|---|---|
| **P0** | `InteractiveProcess`: opcjonalny osobny stderr (scalony zostaje domyślny) + kolejność w `Terminate()` (najpierw job, potem `join`). Testy. | mały |
| **P1** | Sesja: `scripts/python/console_host.py` + `ScientificRuntime/Python/ConsoleSession` (posiada `InteractiveProcess`, parsuje linie, wystawia DTO do odpytywania). | największy |
| **P2** | Skrypty w edytorze, szablony, „Uruchom” w tej samej sesji. | średni |
| **P3** | Debugger VS Code. Potrzebuje tylko P1, więc może iść przed P2. | mały |
| **P4** | `ds` read-only. Osobna decyzja. | średni |

**P1 w szczegółach:**
- Panel konsoli przechodzi na sesję i dostaje:
  - prawdziwe podpowiedzi (Ctrl+Space, potem debounce, asynchronicznie przez `suggestionsPromise`);
  - przyciski Przerwij i Restart.
- Nowy panel **Zmienne**: nazwa, typ, kształt/długość, podgląd; dwuklik wstawia nazwę.
- `input()` daje czytelny błąd.
- matplotlib na Agg; figury zapisywane do PNG i pokazywane jako ścieżki (podgląd obrazka później).
- Jedna sesja współdzielona przez panele, odpytywana raz na klatkę niezależnie od widoczności
  (wzorzec własności `ElectronicStructureSession`). `ScriptRunner` zostaje bez zmian.

**P2 w szczegółach:**
- `TextEditorPanel`: `Language::Python()` dla `.py` i ostrzeżenie o niezapisanych zmianach.
- „Nowy skrypt z szablonu” tworzy plik w `<projekt>/scripts/`.
- „Uruchom” (F5) zapisuje plik i wywołuje `run_file` we wspólnej sesji, więc zmienne skryptu trafiają
  do panelu Zmienne. Prawdziwe `__file__`, katalog roboczy i `sys.path` zgodne z podpowiedziami.
- Podpowiedzi w edytorze: `jedi.Script(bufor, path, project=jedi.Project(root, added_sys_path=[…]))`.
  Skrypt nie powinien zależeć od przypadkowych zmiennych konsoli.

**P3 w szczegółach:**
- `debugpy` do `pyproject.toml` (dev), host uruchamiany z `-Xfrozen_modules=off`.
- „Debuguj w VS Code” wywołuje `debug_listen` na `127.0.0.1` na wolnym porcie.
- `<projekt>/.vscode/launch.json` z konfiguracją `attach` powstaje **tylko gdy pliku nie ma**.
  W przeciwnym razie panel pokazuje fragment do skopiowania.
- „Otwórz w VS Code”: `code <projekt> -g plik:linia`. Podłączenie w VS Code startuje użytkownik (F5).
- Breakpointy ustawione w VS Code trafiają przy „Uruchom” w aplikacji.
- `breakpoint()` przechodzi do debugpy, gdy debugger jest podłączony; w przeciwnym razie daje czytelny
  błąd (nigdy pdb na stdin protokołu).

**P4 w szczegółach:**
- Pakiet czytający `manifest.yaml`, katalogi główne projektu i datasety `ds.*.v1` (te same schematy i
  reguły ścieżek względnych co `AnalysisDatasetIO.cpp:19`).
- API: `project.datasets(kind=…)` z ładowaniem tablic. Wstępnie zaimportowany w konsoli jako `project`.
- Spełnia ideę `project[...]` z 08-23 bez działającej aplikacji. Wyniki obliczeń dalej czyta puntukas,
  a nie nowy parser.

**Szablony na start** (każdy korzysta z istniejących datasetów i czytników, bez nowej analizy):
1. Porównanie datasetów: wybrane `ds.*.v1` (fonony, CCD, dyspersja, mod bulku) → CSV.
2. Energie VASP przez puntukas: TOTEN, energie względne, przerwa.
3. Podgląd fononów/CCD: wkłady modów albo punkty CCD z jednostkami i pochodzeniem danych.
4. Rycina do pracy: FigureSpec z tablic datasetu → istniejące `render()` z `render_figure.py:149`
   (PDF/SVG/PNG).

**Później lub dalej zamrożone:**
- `ds.scene` (mutacje sceny przez komendy i undo);
- hot reload;
- `.ipynb`;
- wspólny kernel z VS Code (`jupyter_server`);
- zakładki w edytorze;
- debugpy dla mostów;
- ciepły „daemon” Pythona dla mostów;
- komunikaty host → aplikacja w stylu `ds.app.open(path)`. Protokół jest dwukierunkowy, więc to
  naturalne przedłużenie, ale poza pierwszym zakresem.

## 6. Ryzyka i granice

- Wyjście natywne (C/Fortran) trafia do osobnego stderr jako diagnostyka; nie zawsze da się je przypisać
  do komórki.
- Port debuggera tylko na loopback i tylko na żądanie. Debugger potrafi wykonywać kod, więc loopback
  ogranicza ekspozycję, ale nie autoryzuje.
- Skrypty mają pełny dostęp do plików, tak jak dziś konsola.
- Warstwy: host i `ConsoleSession` w `ScientificRuntime`; panele tylko renderują i zbierają intencje;
  zapis skryptu przez `TextFileIO`. `.cpp` ≤ ~500 linii, więc logika sesji poza panelem konsoli.
- ADR-003 do poprawki: dopuścić ograniczone skryptowanie użytkownika (sesja, skrypty, `ds` read-only),
  mutacja sceny dalej zamrożona.

## 7. Pytania do użytkownika

1. Czy interpretacja z §1 się zgadza (podgląd zmiennych sesji; „model” = mechanizm)?
2. Kolejność P0 → P1 → P2 → P3, P4 osobno? Jeśli debugger jest ważniejszy niż szablony, P3 może iść
   zaraz po P1.
3. Skrypty w `<projekt>/scripts/`, czy gdzie indziej (np. obok obliczeń)?
4. Który szablon najpierw?
5. Branch: nowy `task/113-python-session` od `dev` (112 zajęte przez gizmo). Dotyka głównie innych plików niż fonony, wspólna jest
   tylko rejestracja paneli w `EditorLayer`.
