# SceneArrow rework plan

> Revised after review: corrected width/radius semantics, unified Arrow2D units across orientations, made property-edit undo snapshot-safe, constrained Arrow3D overlap geometrically, and made 3D transparency/depth-write requirements explicit.

Dokument zapisany w `docs/`, bo zadanie wskazuje dokładną ścieżkę `docs/scene_arrow_rework_plan.md`. Repozytorium ma też `docs/work/project/plans/`, ale ten plan jest jednorazową instrukcją wykonawczą dla kolejnego agenta i ma pozostać w ścieżce wymaganej w zadaniu.

Uwaga nawigacyjna: `AGENTS.md` każe użyć graphify, gdy istnieje `graphify-out/graph.json`. W tym checkoutcie plik nie istnieje, a `graphify query ...` zwraca błąd braku grafu, więc analiza poniżej opiera się na dokumentacji repo i bezpośrednim czytaniu źródeł.

## 1. Executive summary

`SceneArrow` jest obecnie małym renderer-local annotation obiektem przechowywanym w `RendererWindowState`, bez ECS i bez project persistence. To jest dobry model i należy go zachować. Problemy nie wymagają przebudowy domeny: wynikają głównie z płaskiego UI, braku osobnego compact quick-edit, prostokątnego `Arrow2D` renderowanego shaderem tła labeli, widocznego capu stożka w `Arrow3D`, reużycia bond shadera bez sensownej semantyki alpha/materialu oraz zbyt prostego hit-testu.

Rekomendowana architektura: zostawić `SceneArrow` jako value type w `RendererWindowState::sceneArrows`, zostawić wspólny label/arrow local undo stack, dodać tryb edytora `SceneArrowEditorMode::{Compact, Full}`, dodać dedykowany fragment shader `arrow_quad.frag` dla prawdziwego 2D arrow SDF, poprawić istniejące mesh/render path dla 3D minimalnie: cone bez base cap na ścieżce `SceneArrow`, geometrycznie bezpieczny overlap shaft/head oraz matowsze annotation shading przez małą specjalizację bond/annotation uniforms. Nie dodawać ECS, managera obiektów sceny ani nowego undo systemu.

Przed implementacją trzeba naprawić dwie niespójności semantyczne obecnego kodu. Po pierwsze, user-facing `shaftWidth` i `headWidth` mają oznaczać pełną szerokość/średnicę, a renderer ma dopiero wyprowadzać z nich promienie. Obecnie `shaftWidth` jest używane jak promień cylindra, a `headWidth` jak średnica podstawy stożka, co daje mylące proporcje i nie powinno zostać utrwalone. Po drugie, pola geometrii `Arrow2D` nie mogą zmieniać jednostek przy przełączaniu `Billboard <-> FixedPlane`. W całym `Arrow2D` `shaftWidth`, `headWidth`, `headLength` i `outlineWidth` mają oznaczać wartości screen-space w pikselach; `FixedPlane` zmienia wyłącznie orientację płaszczyzny, a renderer przelicza piksele na world-space lokalnie na każdej klatce. Ponieważ te same pola mają world-space semantykę dla `Line/Arrow3D`, zmiana `ArrowKind` pomiędzy 2D i world-space kinds musi jawnie ustawić/przeliczyć geometrię dla nowego rodzaju; nigdy nie wolno reinterpretować np. `8 px` jako `8 world units`.

Default nowej strzałki powinien być `Arrow2D + Billboard`, amber `#F2B51D`, opacity `1.0`, outline włączony dla 2D. Start/end powinny używać 15-25% przekątnej aktualnej struktury, jeśli caller ma `RendererWindowState`; fallback zostaje długością `1.0`.

## 2. Current architecture

```text
creation
-> defaults
-> storage
-> editor
-> viewport interaction
-> renderer
-> undo/redo
-> deletion
```

Creation:
- `src/Presentation/Panels/RendererPanel.cpp::renderViewportContextMenu` dodaje Arrow przez PPM -> Add -> Arrow.
- `src/Presentation/Panels/RendererPanelToolbar.cpp::drawAddMenu` dodaje Arrow z menu Shift+A / toolbar Add.
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp::Render` dodaje Arrow przez `+ Add arrow`.
- Wszystkie trzy ścieżki wołają `PushPinnedMeasurementUndoSnapshot`, potem `MakeDefaultSceneArrow`, potem ustawiają `selectedSceneArrows = {newIndex}`, `sceneArrowQuickEditActive = true`, `sceneArrowQuickEditIndex = newIndex`.

Defaults:
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp::MakeDefaultSceneArrow(const glm::vec3&)` ustawia `start = seedPosition`, `end = seedPosition + (0,0,1)`.
- Domyślne pola modelu są w `src/Renderer/RendererWindowState.hpp::ArrowStyle` i `SceneArrow`: obecnie `kind = Arrow3D`, `orientation2D = Billboard`, `color = (0.95,0.75,0.1)`, `alpha = 1.0`, `shaftWidth = 0.06`, `outlineWidth = 0.0`, `headWidth = 0.14`, `headLength = 0.22`.

Storage:
- `src/Renderer/RendererWindowState.hpp::std::vector<SceneArrow> sceneArrows`.
- Selection i drag state: `selectedSceneArrows`, `sceneArrowDragging`, `sceneArrowDragLastMouse`, `sceneArrowDragTarget`.
- Quick edit state: `sceneArrowQuickEditActive`, `sceneArrowQuickEditIndex`.
- `SceneArrow` nie jest encją ECS. `SceneSystem::SyncLabelEntities` obejmuje pinned/free labels, nie tworzy arrow entities.

Editor:
- Publiczny header: `src/Presentation/Panels/SceneArrowEditorWidget.hpp`.
- Implementacja widgetu jest w `src/Presentation/Panels/ObjectPropertiesPanel.cpp`: `DrawSceneArrowEditor` oraz prywatny helper `drawArrowStyleEditor`.
- `DrawSceneArrowEditor` jest używany przez pełny panel `ObjectPropertiesPanel` i przez quick-edit popup `RendererPanel::renderSceneArrowQuickEditPanel`.
- Obecnie UI pokazuje Start/End, `Kind`, conditional `Orientation/Plane` dla 2D oraz `TreeNode("Style")`; head controls są widoczne tylko dla `Arrow3D`, mimo że docelowo `Arrow2D` też ma mieć head.

Viewport interaction:
- `src/Presentation/Panels/RendererPanel.cpp::handleSceneArrowInteraction`.
- Gating: `pickLabels` i `camera != nullptr`.
- Hit-test click: project start/end do screen space, clamped point-to-segment distance, radius `12px`.
- Endpoint drag target: start/end proximity `14px`, inaczej `Both`.
- Drag delta: camera-right/up pixel-to-world conversion, jak free labels; single selection może przesuwać start albo end, multi-select zawsze przesuwa całe strzałki.
- Region selection: `hitTestRectSceneArrows` i `hitTestCircleSceneArrows` testują tylko start/end/midpoint, nie cały segment ani head.

Renderer:
- Entry: `src/Renderer/OpenGl/OpenGlRendererBackend.cpp::RenderWindow`.
- Kolejność: grid -> cell -> bonds -> sceneArrows -> atoms -> isosurfaces -> labels.
- `renderSceneArrows` buduje transient vectors: `OpenGlBondInstance` dla shaft/head i `OpenGlArrowQuadInstance` dla 2D.
- `Line` i shaft `Arrow3D` używają `m_CylinderMesh`, `buildBondTransform`, program `"bonds"`.
- `Arrow3D` head używa `m_ConeMesh`, także przez program `"bonds"`.
- `Arrow2D` używa `m_ArrowQuadMesh`, program `"arrow_quad"` = `arrow_quad.vert` + obecnie `label_background.frag`.

Undo/redo:
- `src/Renderer/RendererWindowState.hpp::LabelUndoSnapshot` ma `pinnedMeasurements`, `freeLabels`, `sceneArrows`.
- `src/Renderer/RendererLayer.cpp::PushPinnedMeasurementUndoSnapshot` kopiuje wszystkie trzy wektory do `pinnedMeasurementUndoHistory` i czyści redo.
- `UndoLabelsChange` i `RedoLabelsChange` przywracają wszystkie trzy wektory, czyszczą selection/drag/quick-edit i wołają `SceneSystem::SyncLabelEntities`.
- Komendy `renderer.labels.undo` / `renderer.labels.redo` są lokalne dla viewportu, przez `RendererEvents::Viewport::{UndoLabelsRequested, RedoLabelsRequested}`. Keybindings w `install/users/default/config/keybindings.yaml`: `Ctrl+Alt+U`, `Ctrl+Alt+Shift+U`.

Deletion:
- Viewport Delete: `RendererPanel.cpp` sortuje `selectedSceneArrows` malejąco, usuwa z `sceneArrows`, czyści selection i quick-edit.
- Object properties X: `ObjectPropertiesPanel.cpp` zbiera `arrowToRemove`, po pętli robi snapshot, erase, czyści selection, a quick-edit zamyka tylko gdy usuwany index jest równy `sceneArrowQuickEditIndex`.
- Right-click context menu Delete jest komendą atom-selection (`hasSelection = !selectedAtomIndices.empty()`), nie usuwa arrow selection.

## 3. Project conventions to preserve

- Renderer-only annotations mogą żyć w `RendererWindowState`, nie w Domain. Przykłady: `FreeLabel`, `PinnedMeasurement`, `SceneArrow` w `RendererWindowState.hpp`.
- Local viewport undo dla label-like annotations jest osobnym stackiem, nie globalnym `Core/Undo`. Przykład: `PushPinnedMeasurementUndoSnapshot`, `UndoLabelsChange`, `RedoLabelsChange`.
- Mutacje domeny idą przez `CommandRegistry`, ale lokalny stan UI/renderera może być mutowany lokalnie. Przykłady: `ObjectPropertiesPanel` używa komend dla atomów, ale free labels/arrows mutuje w `RendererWindowState`.
- ImGui style preferuje proste controls, `TreeNode`/conditional visibility i małe helpery zamiast frameworka widgetów. Przykłady: `drawLabelStyleEditor`, `drawArrowStyleEditor`, `SceneOutlinerPanel::drawLabelsGroup`.
- Compact color controls używają `ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel`, gdy panel jest ciasny. Przykłady: `ElectronicStructurePanel.cpp`, `ExportImagePanel.cpp`.
- Wspólne editory są preferowane nad duplikacją kodu. Przykład: `drawLabelStyleEditor` obsługuje pinned i free labels; `DrawSceneArrowEditor` jest wspólny dla properties i quick-edit.
- Drag z preview powinien mieć snapshot na początku logicznej operacji. Przykłady: label drag i arrow drag wołają `PushPinnedMeasurementUndoSnapshot` przy starcie.
- Hit-test utilities już istnieją w `Renderer/Scene/SelectionHitTest.hpp`; nie dodawać drugiego niespójnego helpera do project-to-screen/point-region, jeśli można rozbudować ten.
- Mesh assets są walidowane przez `RendererAssetBundle`/`RendererMeshIO`, ale backend proceduralnie refineduje cylinder/cone. Przykłady: `BuildRefinedCylinderMesh`, `BuildRefinedConeMesh`.
- Build/test entrypoints są script-first. Przykłady w `README.md`, `CLAUDE_REPO_CONTEXT.md`, `docs/mdbook/test-strategy.md`.

Uwaga dla Claude: `docs/work/engineering/cpp-guidelines.md` mówi, żeby nie dodawać anonymous namespace w nowym kodzie. W istniejących plikach są takie namespace, ale nowy helper może być `static` file-local albo metodą prywatną.

## 4. Root-cause analysis

### UI

Dowód:
- `DrawSceneArrowEditor` pokazuje Start/End, `Kind`, conditional orientation/plane, a resztę wrzuca pod `TreeNode("Style")`.
- `drawArrowStyleEditor` używa user-facing `Alpha` i `Border`.
- Quick-edit w `RendererPanel::renderSceneArrowQuickEditPanel` woła dokładnie `DrawSceneArrowEditor`.

Root cause:
- Brak semantycznej struktury Type -> Placement -> Geometry -> Appearance.
- Brak rozdzielenia compact/full przy zachowaniu shared implementation.
- UI ukrywa head controls dla `Arrow2D`, bo renderer ich dziś nie używa.

### Arrow2D

Dowód:
- `OpenGlArrowQuadInstance` ma tylko `worldCenter`, `right`, `up`, `halfSize`, `color`, `outlineColor`, `outlineWidth`.
- `arrow_quad.vert` tworzy prostokątny quad i przekazuje varyings nazwane jak label background.
- Program `"arrow_quad"` ładuje `arrow_quad.vert` + `label_background.frag`.
- `label_background.frag` rysuje rounded rect SDF, nie arrow shape.
- `renderSceneArrows` dla `Arrow2D` ustawia `halfSize = (length * 0.5f, style.shaftWidth)` i nigdy nie używa `headWidth/headLength`.

Root cause:
- 2D strzałka nie ma geometrii ani SDF główki; jest prostokątem.
- Sizing jest world-space; `Billboard` nie zachowuje stabilnej grubości przy zoomie.
- Outline width jest w tych samych local/world units co background rect, nie w pixelach.

### Arrow3D geometry

Dowód:
- `BuildRefinedCylinderMesh` tworzy cap vertices/centers, ale nie dodaje cap indices; shaft jest otwartym cylindrem.
- `BuildRefinedConeMesh` dodaje base cap vertices, base center i cap triangles.
- `renderSceneArrows` ustawia `coneBase = end - direction * headLength`, shaft kończy się dokładnie w `coneBase`, head renderuje od `coneBase` do `end`.

Root cause:
- Widoczny dysk pochodzi z base capu stożka, nie z cylindra.
- Head i shaft są osobnymi meshami stykającymi się w tej samej płaszczyźnie, bez overlapu. To zwiększa ryzyko widocznej szczeliny/z-fightingu na styku po zmianach proporcji lub globalnego `bondRadiusMultiplier`.

### Geometry parameter semantics

Dowód:
- W bieżącym `Arrow3D` `style.shaftWidth` jest przekazywane do ścieżki cylindra jako promień.
- `style.headWidth` jest następnie dzielone przez `2.0f` i używane jako promień podstawy stożka.
- Z punktu widzenia UI oba pola są nazywane `width`, mimo że reprezentują różne wielkości fizyczne.

Root cause:
- Model użytkownika i semantyka renderera są niespójne: `shaftWidth` zachowuje się jak radius, `headWidth` jak diameter.
- Przykładowe obecne wartości `shaftWidth = 0.06`, `headWidth = 0.14` oznaczają w praktyce średnicę trzonu `0.12` i średnicę główki `0.14`, więc head jest tylko około 17% szerszy od shaft. To jest jedną z przyczyn ciężkich, mało czytelnych proporcji Arrow3D.
- Nie należy utrwalać tej niespójności. Po reworku oba pola `shaftWidth` i `headWidth` mają mieć tę samą user-facing semantykę: pełna szerokość/średnica. Renderer wyprowadza `shaftRadius = 0.5f * shaftWidth` i `headRadius = 0.5f * headWidth`.

### Arrow2D unit semantics

Dowód:
- Obecny model ma tylko jeden zestaw pól `ArrowStyle`, niezależnie od `Arrow2DOrientation`.
- Przechowywanie `8.0` jako pikseli w `Billboard`, a następnie interpretowanie tej samej wartości jako `8.0` world units po przełączeniu do `FixedPlane`, dałoby gigantyczną strzałkę.
- W przeciwnym kierunku typowe `0.045` world units stałoby się `0.045 px` i praktycznie zniknęłoby.

Root cause:
- Jedno pole nie może zmieniać jednostki wraz z enumem orientation bez jawnej konwersji danych.
- Rekomendacja: dla całego `Arrow2D` geometria wizualna jest screen-space. `shaftWidth`, `headWidth`, `headLength` i `outlineWidth` są przechowywane i pokazywane jako piksele zarówno dla `Billboard`, jak i `FixedPlane`.
- `FixedPlane` zmienia wyłącznie bazę/orientację. Renderer wylicza lokalny pixel-to-world scale w wybranej płaszczyźnie na każdej klatce.
- Dzięki temu przełączenie `Billboard <-> FixedPlane` nie modyfikuje stylu i nie powoduje skoku rozmiaru.
- Osobny problem pozostaje przy zmianie `ArrowKind`: `Arrow2D` używa px, a `Line/Arrow3D` world units. Type selector musi więc używać jawnego helpera zmiany rodzaju, który ustawia sensowne geometry defaults dla target kind zamiast reinterpretować stare liczby.

### Arrow3D shading

Dowód:
- Shaft/head używają programu `"bonds"`.
- `bonds.frag` liczy Blinn-Phong specular z `u_SpecularIntensity` i `u_Shininess` z global lighting.
- `bonds.frag` zwraca `vec4(..., 1.0)`, ignorując alpha z `OpenGlBondInstance::colorA/colorB`.
- `renderSceneArrows` wysyła `style.alpha` do `colorA/colorB`, ale shader go nie używa.

Root cause:
- Annotation material jest związany z bond materialem i globalnym specularem.
- `alpha` działa dla `Arrow2D`, ale nie działa dla `Line`/`Arrow3D`.

### FixedPlane

Dowód:
- `ComputeArrowQuadBasis` daje normal równą plane normalowi; `arrow_quad.vert` emituje pojedynczą twarz.
- `configureOpenGlState` włącza `GL_CULL_FACE`; render `arrow_quad` nie wyłącza cullingu.
- `Arrow2D FixedPlane` leży w rzeczywistej płaszczyźnie świata, więc z drugiej strony może zostać wycięty back-face cullingiem; edge-on widok ma prawie zerowy projected area.

Root cause:
- Fixed-plane quad jest jednostronny i rysowany przy włączonym back-face cullingu.
- Edge-on przypadek wymaga fallbacku wizualnego albo świadomego zachowania, inaczej użytkownik widzi „znikanie”.

### Picking

Dowód:
- Click hit-test bierze tylko screen-space segment start->end i stałe `12px`.
- Nie uwzględnia `style.shaftWidth`, `headWidth`, `headLength`, orientation2D, outline ani światowej grubości 3D.
- Region select dla arrows testuje tylko start/end/midpoint.

Root cause:
- Pick shape nie odpowiada render shape. Jest wystarczający dla cienkiej linii, ale nie dla szerokiej strzałki, główki i zoom-stable 2D.

### Delete / X

Dowód:
- Viewport Delete dla selected arrows istnieje i czyści quick-edit.
- ObjectPropertiesPanel X istnieje i usuwa po pętli.
- Po usunięciu w ObjectPropertiesPanel quick-edit jest zamykany tylko dla exact index match; jeśli usunięto wcześniejszy index, `sceneArrowQuickEditIndex` może wskazywać inny element.
- Right-click menu Delete jest aktywne tylko dla atom selection, więc nie obsługuje arrow selection.

Root cause:
- Brakuje jednego helpera do bezpiecznego usuwania arrows i normalizacji indices.
- Zgłoszone „X nie usuwa” wymaga runtime potwierdzenia; statycznie w bieżącym branchu X ma kod erase, ale stan indices/quick-edit po erase jest kruchy.

### Defaults

Dowód:
- `MakeDefaultSceneArrow` używa stałej długości `1.0` i domyślnie tworzy `Arrow3D`.
- Funkcja dostaje tylko `seedPosition`, mimo że call sites mają `RendererWindowState`.
- W repo istnieje wzorzec liczenia bounds struktury dla kamery w `RendererStartupBootstrap.cpp::BuildWindowFromStructure`.

Root cause:
- Default nie używa skali sceny i nie jest zoptymalizowany pod zwykłą figure annotation.
- Dobra strzałka wymaga ręcznego ustawiania typu/proporcji po każdym dodaniu.

## 5. Target behavior

- Nowa strzałka po dodaniu wygląda dobrze bez ręcznej edycji.
- Default: `Arrow2D + Billboard`, amber, pełna opacity, czytelna triangular head, outline.
- Full properties editor ma mental model: Type -> Placement -> Geometry -> Appearance.
- Quick-edit jest compact „Adjust Last Operation”: type, podstawowa geometria i appearance na wierzchu; Placement/Advanced collapsed.
- `Line` pokazuje tylko shaft controls, bez head/orientation.
- `Arrow3D` pokazuje shaft/head geometry, bez 2D orientation/plane.
- `Arrow2D + Billboard` nie pokazuje plane; rozmiary mają być wizualnie stabilne przy zoomie.
- `Arrow2D + FixedPlane` pokazuje plane i nie znika jednostronnie przez culling; edge-on zachowanie ma być świadomie obsłużone.
- `Arrow3D` nie pokazuje base disk, gapu ani z-fightingu przy typowych kamerach.
- `Opacity` działa dla 2D, Line i 3D.
- Picking trafia w widzialny shaft i head, nie tylko w matematyczny segment.
- Delete/X usuwa poprawnie, normalizuje selection i quick-edit state, undo przywraca poprzedni stan.

## 6. Data-model decision

Zostaje bez zmian:
- `RendererWindowState::SceneArrow` pozostaje renderer-window-local value type.
- `sceneArrows` pozostaje `std::vector<SceneArrow>`.
- `selectedSceneArrows`, drag state i quick-edit bool+index zostają w `RendererWindowState`.
- `LabelUndoSnapshot` nadal przechowuje `sceneArrows` razem z labels.
- Nazwa pola `ArrowStyle::alpha` zostaje; w UI pokazywać `Opacity`.
- Nazwa pola `outlineWidth` zostaje; w UI pokazywać `Outline`, nie `Border`.

Zmienić:
- Ujednolicić semantykę `ArrowStyle::shaftWidth` i `ArrowStyle::headWidth`: oba pola oznaczają pełną szerokość/średnicę widzianą przez użytkownika. W `Arrow3D` renderer wylicza `shaftRadius = 0.5f * shaftWidth` oraz `headRadius = 0.5f * headWidth`.
- Dla całego `Arrow2D` pola `shaftWidth`, `headWidth`, `headLength`, `outlineWidth` mają semantykę screen-space w pikselach zarówno w `Billboard`, jak i `FixedPlane`. Nie przechowuj world-space wartości w tych samych polach zależnie od orientation.
- `Line` i `Arrow3D` używają world-space geometrii. Dlatego zmiana `ArrowKind` pomiędzy `Arrow2D` a `Line/Arrow3D` musi przejść przez helper w rodzaju `ApplySceneArrowKindChange(...)`, który ustawia geometrię odpowiednią dla target kind. Nie wykonuj surowego `arrow.kind = newKind` pozostawiając te same liczby w polach rozmiaru.
- Domyślne wartości `SceneArrow`/`ArrowStyle` lub `MakeDefaultSceneArrow` ustawić tak, aby nowy default był `Arrow2D + Billboard`.
- Dodać tylko render-time/editor-time helpery, nie nowy system danych.
- Rozszerzyć `OpenGlArrowQuadInstance` o parametry potrzebne shaderowi 2D arrow SDF, np. `arrowLength`, `shaftWidth`, `headWidth`, `headLength`, `outlineWidth` oraz dane potrzebne do pixel-to-world conversion / local basis. To nie jest publiczny model danych, tylko GPU instance layout.
- Dodać `SceneArrowEditorMode` w `SceneArrowEditorWidget.hpp`.

Nie zmieniać:
- Nie robić `SceneArrow` encją ECS.
- Nie dodawać persistence/migration tylko dla tego reworku.
- Nie przepinać arrow undo na globalny `Core/Undo`.
- Nie zmieniać nazw pól modelu tylko kosmetycznie.

Backward compatibility:
- `SceneArrow` nie jest obecnie znajdowany w IO/Storage poza render/export/undo paths, więc nie ma starych workspace migrations.
- Zmiana domyślnych wartości dotyczy nowych arrows.
- Ujednolicenie `shaftWidth` z radius-like na pełną szerokość jest świadomą zmianą semantyki bieżącego renderer-local modelu. Ponieważ `SceneArrow` nie ma project persistence, nie dodawać migracji; zaakceptować zmianę wyglądu strzałek istniejących tylko w aktualnej sesji/undo snapshotach.
- `Arrow2D` po reworku ma jeden spójny system jednostek: px dla obu orientation. Przełączenie `Billboard <-> FixedPlane` nie może reinterpretować już zapisanych wartości.
- Przełączenie `ArrowKind` 2D <-> Line/3D jest świadomą zmianą systemu jednostek i musi resetować/konwertować geometrię do target defaults; appearance (`color`, `alpha`, `outlineColor`) oraz `start/end` mają pozostać zachowane.

## 7. Exact implementation plan

### Step 1 - Add shared editor modes without changing behavior yet

Files:
- `src/Presentation/Panels/SceneArrowEditorWidget.hpp`
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp`
- `src/Presentation/Panels/RendererPanel.cpp`

Symbols:
- `SceneArrowEditorMode`
- `DrawSceneArrowEditor`
- `drawArrowStyleEditor`
- `RendererPanel::renderSceneArrowQuickEditPanel`

Change:
- Add:
  ```cpp
  enum class SceneArrowEditorMode
  {
      Full,
      Compact
  };
  void DrawSceneArrowEditor(RendererWindowState::SceneArrow &arrow, SceneArrowEditorMode mode = SceneArrowEditorMode::Full);
  void DrawSceneArrowEditor(RendererWindowState &windowState, std::size_t arrowIndex, SceneArrowEditorMode mode);
  ```
- Keep the existing call signature source-compatible through default argument.
- The indexed overload must validate `arrowIndex < windowState.sceneArrows.size()` and then draw/edit that arrow. It exists so the widget can push undo snapshots before mutations without making callers duplicate per-control undo logic.
- Initially route both modes through the old layout or minimal branching. This makes later UI changes mechanical and keeps compile errors small.

Preserve:
- `DrawSceneArrowEditor` remains the shared editor entry point.
- No duplicate quick-edit implementation.
- The direct `SceneArrow&` overload remains available for non-undoable preview-only use, but `ObjectPropertiesPanel` and quick-edit should move to the indexed overload.

Why:
- Quick-edit must not be the full editor, but both must share controls and mutation behavior.

Verification:
- Build compiles.
- `ObjectPropertiesPanel` still calls full mode by default.
- `RendererPanel::renderSceneArrowQuickEditPanel` can call `Compact` explicitly.

### Step 2 - Refactor SceneArrow UI into semantic sections

Files:
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp`
- `src/Presentation/Panels/SceneArrowEditorWidget.hpp`

Symbols:
- `DrawSceneArrowEditor`
- `drawArrowStyleEditor`
- New file-local helpers: `drawArrowTypeSelector`, `drawArrowPlacementSection`, `drawArrowGeometrySection`, `drawArrowAppearanceSection`

Change:
- Implement `Full` layout:
  - Header text: `Arrow`.
  - Type selector as three `RadioButton`s or small button group, reusing existing ImGui style. Labels: `Line`, `2D Arrow`, `3D Arrow`.
  - Type selector **nie może** wykonywać tylko `arrow.kind = newKind`. Wywołaj wspólny helper zmiany kind, który zachowuje `start/end`, `color`, `alpha`, `outlineColor`, ale ustawia geometrię w jednostkach właściwych dla target kind:
    - target `Arrow2D`: `8/22/28/1.25 px`
    - target `Line`: world-space `shaftWidth` z długości `L`
    - target `Arrow3D`: world-space `shaftWidth/headWidth/headLength` z długości `L`
    - przy `Line <-> Arrow3D` można zachować `shaftWidth`, bo oba używają tej samej world-space/full-diameter semantyki; przy wejściu do `Arrow3D` ustaw tylko brakujące head defaults.
  - `orientation2D` i `fixedPlane` mogą pozostać zapamiętane w strukturze podczas przejścia przez `Line/Arrow3D`, aby powrót do 2D przywracał poprzednią orientację; nie wpływa to na jednostki geometrii.
  - `TreeNodeEx` or `CollapsingHeader` sections: `Placement`, `2D Orientation`, `Geometry`, `Appearance`.
  - Placement: Start/End `InputFloat3`, read-only length display computed with `glm::length(end-start)`.
  - 2D Orientation visible only for `Arrow2D`: `Billboard` / `Fixed plane`; Plane combo only for `FixedPlane`.
  - Geometry: `Shaft width`; `Head width` and `Head length` only for `Arrow2D` and `Arrow3D`.
  - Appearance: `Color`, `Opacity`; `Outline` only for `Arrow2D` initially. If 3D outline is not implemented, do not show it for `Line`/`Arrow3D`.
- Implement `Compact` layout:
  - Type selector.
  - Always-visible `Geometry` and `Appearance`.
  - `Placement` collapsed by default.
  - `Advanced` collapsed; contains 2D orientation/plane and any rarely changed numeric controls.
- Rename UI labels only: `Alpha` -> `Opacity`, `Border` -> `Outline`.
- Keep `ArrowStyle::alpha` and `outlineWidth` field names.

Preserve:
- Conditional visibility style from current `DrawSceneArrowEditor`.
- Existing `PushID` ownership by callers.
- Direct mutation behavior for local renderer state.

Why:
- Fixes debug-form feel without adding a widget framework.

Verification:
- Full editor shows exactly target controls per kind.
- Quick edit is compact and does not duplicate code.
- No head controls for `Line`; no plane for `Arrow3D`; no plane for `Arrow2D + Billboard`.

### Step 3 - Make property edits undoable in one logical step

Files:
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp`
- `src/Presentation/Panels/SceneArrowEditorWidget.hpp`

Symbols:
- `DrawSceneArrowEditor`
- `PushPinnedMeasurementUndoSnapshot`

Change:
- Current code mutates arrow properties every frame without snapshots for typed edits/sliders; drag undo only exists for viewport drag.
- Implement undo inside the indexed overload, ale **nie pozwalaj ImGui edytować modelu bezpośrednio przed wykonaniem snapshotu**.
- `DrawSceneArrowEditor(RendererWindowState &windowState, std::size_t arrowIndex, SceneArrowEditorMode mode)` validates the index and passes a small edit-context / `pushUndoOnce` helper into section helpers.
- Dla każdego kontrolowanego pola użyj temporary UI value:
  ```cpp
  float edited = arrow.style.shaftWidth;
  const bool changed = ImGui::DragFloat("Shaft width", &edited, ...);

  if (ImGui::IsItemActivated())
      pushUndoOnce(); // snapshot nadal zawiera starą wartość modelu

  if (changed)
      arrow.style.shaftWidth = edited;
  ```
  Analogicznie dla `InputFloat3`, `SliderFloat`, `ColorEdit3/4`, enum selectors itd.
- Powód użycia temporary: `ImGui::DragFloat(..., &arrow.style.shaftWidth)` może zmienić pole już wewnątrz wywołania widgetu. Snapshot wykonany dopiero po widgetcie mógłby wtedy zawierać pierwszą zmienioną wartość zamiast prawdziwego stanu sprzed edycji.
- `pushUndoOnce()` powinien tworzyć tylko jeden snapshot dla całej ciągłej interakcji. Nie wystarczy lokalny `bool` resetowany co klatkę, jeżeli widget pozostaje aktywny przez wiele ramek. Najprostsze poprawne rozwiązanie ma wykorzystać `ImGui::IsItemActivated()` jako początek operacji i wykonać snapshot wyłącznie w tej ramce aktywacji.
- Dla kontrolek typu button/radio/combo, które przypisują wartość dopiero po zwrocie `true`, snapshot wykonaj bezpośrednio przed assignment.
- Do not add `operator==` just for change detection.
- Change `ObjectPropertiesPanel` per-row editor call from `DrawSceneArrowEditor(arrow)` to `DrawSceneArrowEditor(*windowState, rowIndex, SceneArrowEditorMode::Full)`.
- Change quick edit from direct reference call to `DrawSceneArrowEditor(windowState, windowState.sceneArrowQuickEditIndex, SceneArrowEditorMode::Compact)`.
- Keep the direct `SceneArrow&` overload as a thin no-undo path used only where a `RendererWindowState` is not available.
- Keep quick-edit edits undoable the same way; quick edit has `windowState` available.

Preserve:
- One undo step per drag/typed edit, not one per frame.
- Existing `Ctrl+Alt+U`/`Ctrl+Alt+Shift+U` label undo stack.

Why:
- Existing add/delete/drag are undoable; property edits should not be irreversible.

Verification:
- Add arrow, change kind, `Ctrl+Alt+U` returns previous kind.
- Drag slider continuously, one undo returns pre-drag value.
- Typing Start X and committing returns with one undo.

### Step 4 - Centralize arrow deletion/index normalization

Files:
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp`
- `src/Presentation/Panels/RendererPanel.cpp`
- Optional: `src/Presentation/Panels/SceneArrowEditorWidget.hpp` only if helper must be shared

Symbols:
- New helper `EraseSceneArrows`
- `RendererPanel` viewport Delete block
- `ObjectPropertiesPanel` X block
- `selectedSceneArrows`
- `sceneArrowQuickEditActive`
- `sceneArrowQuickEditIndex`

Change:
- Add a file-local helper in a common source reachable by both panels, or duplicate only the tiny normalization if header sharing would create coupling. Preferred: expose a small function in `SceneArrowEditorWidget.hpp`:
  ```cpp
  void EraseSceneArrows(RendererWindowState &windowState, std::vector<std::size_t> indices);
  ```
- Helper behavior:
  - sort descending
  - unique indices
  - ignore out-of-range
  - erase valid indices
  - clear `selectedSceneArrows`
  - set `sceneArrowDragging = false`
  - if quick-edit target was erased, close quick-edit
  - if an erased index was lower than quick edit index, decrement quick edit index accordingly; if uncertain, simpler safe behavior is close quick-edit on any erase.
- Always call `PushPinnedMeasurementUndoSnapshot` before helper if at least one valid index will be erased.
- Right-click context menu Delete can remain atom-command-only unless the menu item is meant to support annotations. If implementing, add a separate enabled state for annotation selection and call the helper for arrows/free labels/pins; do not route through `renderer.selection.delete`.

Preserve:
- Current descending erase safety.
- Selection reset on undo/redo.

Why:
- Prevent dangling index states and makes X/Delete semantics identical.

Verification:
- Delete selected multiple arrows from viewport.
- Delete arrow before active quick-edit index.
- Delete from ObjectProperties X.
- Undo restores all arrows and clears stale quick-edit.

### Step 5 - Improve defaults with scene-relative length

Files:
- `src/Presentation/Panels/SceneArrowEditorWidget.hpp`
- `src/Presentation/Panels/ObjectPropertiesPanel.cpp`
- `src/Presentation/Panels/RendererPanel.cpp`
- `src/Presentation/Panels/RendererPanelToolbar.cpp`

Symbols:
- `MakeDefaultSceneArrow`
- New overload `MakeDefaultSceneArrow(const RendererWindowState&, const glm::vec3&)`
- New helper `ComputeSceneArrowDefaultLength`
- New helper `ApplySceneArrowKindChange` / `ApplySceneArrowGeometryDefaultsForKind` (name zgodna z project naming)

Change:
- Keep existing `MakeDefaultSceneArrow(const glm::vec3&)` as fallback.
- Add overload taking `const RendererWindowState &`.
- Compute bounds from `windowState.structure.atoms` using `RendererAtomData::cartesianPosition`.
- Use all atoms, not only visible atoms, for consistency with `RendererStartupBootstrap::BuildWindowFromStructure` camera fit. If product wants hidden-only framing later, make that a separate decision.
- `diagonal = length(maximum - minimum)`.
- `length = clamp(diagonal * 0.20f, 0.75f, 4.0f)`, fallback `1.0f` if no atoms or non-finite diagonal.
- Direction default: choose camera-facing horizontal if camera exists? `MakeDefaultSceneArrow` currently has no camera. Do not add camera dependency. Use `+X` or existing `+Z`. Recommendation: `+X` for 2D billboard readability in default camera; if runtime shows it edge-on or hidden by atoms, switch to `+Z`. Mark as runtime check.
- Set `kind = ArrowKind::Arrow2D`, `orientation2D = Billboard`, `fixedPlane = XY`.
- Set style values from section 8.
- Implement one shared target-kind geometry helper used both by `MakeDefaultSceneArrow` and by the UI type selector. It takes current arrow length `L` (or receives it from caller) and:
  - for `Arrow2D` sets px geometry defaults,
  - for `Line` sets world-space full-diameter shaft width,
  - for `Arrow3D` sets world-space full-diameter shaft/head widths and head length,
  - preserves appearance and endpoints.
- Update three add call sites to use the `RendererWindowState` overload.

Preserve:
- Seed behavior: cursor/context/add-menu position remains start point.
- No dependency on Domain, ProjectWorkspace, or ECS.

Why:
- Callers already have window state, so scene-relative sizing does not add architectural coupling.

Verification:
- Empty/synthetic structure still creates visible 1.0-length arrow.
- Small and large structures create arrows of proportional length.
- All add paths produce same defaults.

### Step 6 - Replace Arrow2D rounded-rect shader with arrow SDF shader

Files:
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`
- `src/Renderer/OpenGl/OpenGlRendererBackend.hpp`
- `src/Renderer/OpenGl/Shaders/arrow_quad.vert`
- New `src/Renderer/OpenGl/Shaders/arrow_quad.frag`

Symbols:
- `OpenGlArrowQuadInstance`
- `OpenGlRendererBackend::Initialize`
- `OpenGlRendererBackend::createArrowQuadMesh`
- `OpenGlRendererBackend::renderSceneArrows`
- `ComputeArrowQuadBasis`

Change:
- Load `"arrow_quad"` with `arrow_quad.vert` + `arrow_quad.frag`, no longer `label_background.frag`.
- Extend `OpenGlArrowQuadInstance` with enough shape parameters:
  - local arrow length
  - shaft width
  - head width
  - head length
  - outline width
  - optionally local-to-world scale if using pixel units for billboard
- Update VAO attributes in `createArrowQuadMesh`.
- In `renderSceneArrows`, for `Arrow2D`:
  - compute projected screen length from start/end.
  - interpret `style.shaftWidth`, `style.headWidth`, `style.headLength`, `style.outlineWidth` as pixels **for both `Billboard` and `FixedPlane`**.
  - `Billboard`: convert pixels to world-space expansion at arrow center using the camera-facing right/up basis and the same projection-probe idea already used in `handleSceneArrowInteraction` / `handleFreeLabelInteraction`.
  - `FixedPlane`: keep the geometric arrow plane fixed in XY/XZ/YZ, but compute how much displacement along the local in-plane `right` and `up` axes corresponds to one screen pixel at the arrow center. Use these two local pixel-to-world scales to build the quad. Do not reinterpret stored style values as world units.
  - changing `orientation2D` must not rewrite geometry fields and must not change their units.
  - clamp head length to `min(requestedPx, 0.45 * projectedLengthPx)` to avoid head consuming short arrows.
  - quad extents must include max shaft/head half width plus outline after px->world conversion.
- Fragment shader:
  - local x runs from tail to tip; use current centered coordinates or convert internally.
  - body SDF: rounded rectangle from tail to `length - headLength`, half-height `shaftWidth * 0.5`.
  - head SDF: triangle with base at `length - headLength`, half-height `headWidth * 0.5`, tip at `length`.
  - fill is union of body/head.
  - outline is outside stroke around union; use `fwidth` AA.
  - discard only when outside outline+AA.
- Disable culling or draw two-sided for arrow quads:
  - Minimal: around quad draw, `glDisable(GL_CULL_FACE)` then restore `glEnable(GL_CULL_FACE)`.
  - Do this for all `Arrow2D` to fix billboard/fixed-plane back-face cases.

Preserve:
- `m_ArrowQuadMesh` as separate VAO/VBO.
- CPU-side basis model.
- `sceneOffset` uniform.
- Blending path.

Why:
- `label_background.frag` cannot draw a real arrow head.

Verification:
- `Arrow2D Billboard` shows triangular head.
- Outline appears only when width > 0.
- Billboard width/head remain approximately stable on zoom.
- `Arrow2D FixedPlane` also keeps approximately stable visual shaft/head/outline dimensions on zoom while remaining in the selected world plane.
- Switching `Billboard -> FixedPlane -> Billboard` does not change stored geometry values and does not cause a size jump.
- FixedPlane visible from both sides; edge-on behavior is documented/manual checked.

### Step 7 - Normalize world-space width semantics and fix Arrow3D joint geometry

Files:
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`
- Optional: `src/Renderer/OpenGl/OpenGlRendererBackend.hpp` if adding separate mesh handle

Symbols:
- `BuildRefinedConeMesh`
- `createConeMesh`
- `m_ConeMesh`
- `renderSceneArrows`

Change:
- Preferred minimal implementation:
  - Add parameter to cone refinement to include/exclude base cap, e.g. `BuildRefinedConeMesh(meshData, bool includeBaseCap)`.
  - Keep `m_ConeMesh` capless for `SceneArrow` if cone mesh is only used by arrows. Current grep shows cone is only loaded/commented for SceneArrow, so one capless `m_ConeMesh` is enough.
  - Remove base cap vertices/indices when capless.
  - Ujednolić semantykę rozmiarów przed budowaniem transformów:
    - dla `Line` i shaft `Arrow3D`: `shaftRadius = 0.5f * style.shaftWidth`
    - dla head `Arrow3D`: `headRadius = 0.5f * style.headWidth`
    - od tego miejsca wszystkie obliczenia mesha używają jawnych `...Radius`.
  - To jest świadoma zmiana obecnego zachowania także dla `Line`; zaktualizuj defaults tak, aby wizualna grubość nowych linii pozostała sensowna po przejściu z radius-like field do full diameter.
  - Nie wymuszaj `headRadius` przez ukrytą zmianę user value typu `max(..., style.shaftWidth * 1.6f)`. Zamiast tego clampuj wartości w editor/defaultach tak, aby `headWidth > shaftWidth` z rozsądnym minimum. Renderer może mieć safety clamp tylko przeciw wartościom zerowym/ujemnym.
  - In `renderSceneArrows`, make shaft extend slightly into the head, ale overlap musi respektować zwężanie stożka. Dla stożka o `headRadius`, długości `headLength` i cylindra `shaftRadius` maksymalna głębokość, zanim cylinder przebije boczną powierzchnię stożka, wynosi:
    ```cpp
    maxSafeOverlap = headLength * (1.0f - shaftRadius / headRadius);
    ```
    dla `headRadius > shaftRadius`.
  - Użyj marginu bezpieczeństwa, np.:
    ```cpp
    const float maxSafeOverlap =
        headRadius > shaftRadius
            ? headLength * (1.0f - shaftRadius / headRadius)
            : 0.0f;

    const float desiredOverlap =
        std::max(headLength * 0.05f, shaftRadius * 0.25f);

    const float overlap =
        std::clamp(desiredOverlap, 0.0f, maxSafeOverlap * 0.8f);
    ```
    Dopasuj dokładne minimum tylko jeśli runtime pokaże szczelinę; nie przekraczaj geometrycznie bezpiecznego overlapu.
  - `shaftEnd = coneBase + direction * overlap`; build shaft to `shaftEnd`.
  - Ensure `headLength` <= `length * 0.6f`, and if `length` is very short, keep a small shaft or render cone-only intentionally.
- Do not implement one procedural combined arrow mesh in this pass unless capless+overlap still looks bad in runtime.

Preserve:
- Existing cylinder mesh and instancing path.
- Existing `buildBondTransform`.
- No new mesh asset.

Why:
- The disk is proven to be cone base cap. Removing it and overlapping gives the biggest visual fix with minimal code.

Verification:
- View Arrow3D from front, side, grazing, behind.
- No base disk at shoulder.
- No gap and no z-fighting at shaft/head.
- Cylinder surface never protrudes through the narrowing cone when headWidth/headLength are edited through their allowed ranges.
- `shaftWidth` and `headWidth` visibly correspond to full diameters in the editor.
- Cylinder ends remain invisible enough.

### Step 8 - Add annotation-friendly 3D alpha/shading

Files:
- `src/Renderer/OpenGl/Shaders/bonds.frag`
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp`
- Optional: duplicate shader program `annotation_bonds` using existing `bonds.vert`

Symbols:
- `"bonds"` program
- Possible new `"annotation_bonds"` program
- `OpenGlBondInstance::colorA/colorB`
- `renderSceneArrows`

Change:
- Minimal low-risk variant:
  - Modify `bonds.frag` to output alpha from mixed `vColorA.a/vColorB.a` instead of hardcoded `1.0`.
  - This affects bonds too, but all bond instances currently send alpha `1.0`, so opaque bond behavior remains unchanged.
  - Add uniforms with defaults if needed:
    - `u_SpecularScale`
    - or `u_AnnotationMaterial`
  - For normal bonds, set `u_SpecularScale = 1.0`.
  - In `renderSceneArrows`, set `u_SpecularScale = 0.25` or lower before drawing shafts/heads.
- **Do not treat fragment alpha alone as complete transparency support.** Verify the current OpenGL state (`GL_BLEND`, `GL_DEPTH_TEST`, depth write mask) for the SceneArrow pass.
- For `Line`/`Arrow3D` with `alpha < 1.0f`, use an annotation-transparent draw path that:
  - keeps depth testing as appropriate for world-space arrows,
  - disables depth writes during transparent arrow drawing (`glDepthMask(GL_FALSE)`) and restores them afterwards,
  - uses the existing blend function,
  - restores all modified GL state after the pass.
- If current renderer ordering makes correct transparent overlap with atoms impossible without a larger sorted-transparency system, do **not** build that system in this rework. In that case document the limitation and either:
  1. keep 3D arrow opacity clamped to `1.0` in the UI for this pass, or
  2. accept simple unsorted transparency only after runtime verification confirms it is visually adequate for annotations.
- If uniform changes in shared `bonds` program feel risky, load second program `"annotation_bonds"` with same vertex shader and small `annotation_bonds.frag`, reusing `OpenGlBondInstance` layout. This costs one small shader, not a pipeline system.

Preserve:
- Lighting directions/intensities and saturation remain shared.
- No PBR/material subsystem.

Why:
- Arrow is an annotation; it should not look like shiny atom/bond material, and opacity must work.

Verification:
- Existing bonds remain visually unchanged with alpha 1.0.
- If 3D opacity remains enabled, `Line`/`Arrow3D` opacity changes transparency without transparent arrows incorrectly occluding atoms solely because of depth writes.
- GL depth-write/cull/blend state is restored after SceneArrow rendering.
- Specular highlight is subdued on 3D arrows.

### Step 9 - Make picking match visible arrow shape

Files:
- `src/Renderer/Scene/SelectionHitTest.hpp`
- `src/Renderer/Scene/SelectionHitTest.cpp`
- `src/Presentation/Panels/RendererPanel.cpp`
- `tests/Renderer/Scene/SelectionHitTestTests.cpp`

Symbols:
- New `SelectionHitTest::DistancePointToSegment`
- New `SelectionHitTest::PointInTriangle` or `DistancePointToTriangle2D`
- `RendererPanel::handleSceneArrowInteraction`
- `RendererPanel::hitTestRectSceneArrows`
- `RendererPanel::hitTestCircleSceneArrows`

Change:
- Move clamped point-to-segment screen-space math into `SelectionHitTest`.
- For click hit-test:
  - Project start/end.
  - Compute screen direction/perp.
  - Shaft tolerance = max(12px, rendered shaft half-width px + outline + 4px).
  - For `Arrow2D`, test triangle head in screen space using projected or computed screen head base.
  - For `Arrow3D`, approximate head with circle/triangle near end: tolerance = max(14px, projected head radius if cheap, else `headWidth` converted via probe).
  - For `Line`, no head test.
- Endpoint selection still uses start/end proximity, but scale endpoint radius with rendered width: `max(14px, shaftHalfPx + 8px)`.
- Region selection:
  - Improve from start/end/midpoint to segment-vs-rect/circle approximation, or sample at 5 points including head base. Keep it simple; this is selection convenience.

Preserve:
- Ctrl additive toggle behavior.
- Multi-select rigid group drag.
- No drawn transform gizmo for arrows.

Why:
- Fixes „hit-test/gizmo does not catch arrow” without adding a new gizmo system.

Verification:
- Click shaft at zoomed-in and zoomed-out sizes.
- Click 2D head.
- Drag start/end/middle.
- Box/circle select catches long arrows even when endpoints are outside region but shaft crosses it.

### Step 10 - Manual/render verification and small automated tests

Files:
- `tests/Renderer/Scene/SelectionHitTestTests.cpp`
- Optional new tests under `tests/Renderer/Scene/`
- No production code outside touched symbols.

Symbols:
- `SelectionHitTest` helpers
- `MakeDefaultSceneArrow` overload if testable without ImGui/OpenGL

Change:
- Add unit tests for new geometry helpers:
  - point-to-segment distance.
  - point-in-arrow-head triangle or distance.
  - default length helper on empty and known bounds if extracted to a non-ImGui helper.
- Shader output is manual/runtime validated; do not attempt OpenGL screenshot tests unless repo already has harness.

Preserve:
- Existing tests.

Why:
- Most risk is visual, but hit-test math can be deterministic.

Verification:
- `python scripts/python/build.py --target DefectStudioTests`
- Run `DefectStudioTests.exe --gtest_brief=1` from build output.
- Manual checklist in section 13.

## 8. Suggested default values

Use these exact values as the implementation baseline. Keep clamping in UI/render path so user edits cannot create NaN/negative geometry.

### Global semantic rule

- User-facing `shaftWidth` and `headWidth` always mean **full width / diameter**, never radius.
- `Arrow3D` renderer derives:
  ```cpp
  shaftRadius = 0.5f * style.shaftWidth;
  headRadius  = 0.5f * style.headWidth;
  ```
- For **all `Arrow2D` orientations**, `shaftWidth`, `headWidth`, `headLength`, `outlineWidth` are stored in **pixels**. `FixedPlane` does not switch these fields to world units.
- `Line`/`Arrow3D` geometry remains world-space. Therefore changing `ArrowKind` 2D <-> world-space kind must apply target-kind geometry defaults; never reinterpret the same numeric values.

Common:
- Color: `glm::vec3(0.949f, 0.710f, 0.114f)` (`#F2B51D`).
- Opacity: `1.0f`.
- Default world-space arrow length: `clamp(sceneDiagonal * 0.20f, 0.75f, 4.0f)`, fallback `1.0f`.

Line:
- `kind = ArrowKind::Line`
- `shaftWidth = clamp(0.045f * L, 0.025f, 0.20f)` as **full diameter**.
- `outlineWidth = 0.0f`.
- `headWidth/headLength` can retain valid struct defaults but UI hidden and renderer unused.

Arrow2D Billboard:
- `kind = ArrowKind::Arrow2D`
- `orientation2D = Arrow2DOrientation::Billboard`
- `shaftWidth = 8.0f` px.
- `headWidth = 22.0f` px.
- `headLength = 28.0f` px.
- `outlineWidth = 1.25f` px.
- `outlineColor = glm::vec3(0.06f, 0.055f, 0.05f)`.
- Head length render clamp: `min(style.headLength, 0.45f * projectedLengthPx)`.

Arrow2D FixedPlane:
- `kind = ArrowKind::Arrow2D`
- `orientation2D = Arrow2DOrientation::FixedPlane`
- `fixedPlane = WorldPlane::XY`
- **Use the same pixel defaults as Billboard**:
  - `shaftWidth = 8.0f` px
  - `headWidth = 22.0f` px
  - `headLength = 28.0f` px
  - `outlineWidth = 1.25f` px
- Same outline color as Billboard.
- Renderer converts pixel thickness/head dimensions into local world displacement inside the selected plane each frame. Switching orientation must not rewrite these values.

Arrow3D:
- `kind = ArrowKind::Arrow3D`
- `shaftWidth = clamp(0.045f * L, 0.025f, 0.20f)` as full diameter.
- `headWidth = clamp(0.12f * L, 0.07f, 0.50f)` as full base diameter.
- `headLength = clamp(0.18f * L, 0.10f, 0.70f)`.
- `outlineWidth = 0.0f`.
- Enforce a sensible geometry relation in the editor/default helper, e.g. `headWidth >= 1.8f * shaftWidth`, with a target default ratio around `2.5-3.0`. Do not silently change the user value inside the renderer except for a minimal safety clamp against degenerate geometry.

Default created by `MakeDefaultSceneArrow`:
- Prefer `Arrow2D Billboard` with the values above.
- If runtime verification shows the product primarily uses arrows as world-space vectors, switch only the default kind to `Arrow3D`; keep the corrected width semantics and all UI/render fixes.

## 9. UI specification

Full properties editor:

```text
Arrow
────────────────────────────────

Type
[ Line ] [ 2D Arrow ] [ 3D Arrow ]

▼ Placement
Start    X [...]  Y [...]  Z [...]
End      X [...]  Y [...]  Z [...]
Length   1.234

▼ 2D Orientation          // only Arrow2D
[ Billboard ] [ Fixed plane ]
Plane                    // only Arrow2D + FixedPlane
[ XY ▼ ]

▼ Geometry
Shaft width    [...]
Head width     [...]     // only Arrow2D / Arrow3D
Head length    [...]     // only Arrow2D / Arrow3D

▼ Appearance
Color          [swatch]
Opacity        [...]
Outline        [x]       // only Arrow2D
    Color      [swatch]
    Width      [...]
```

Quick-edit popup:

```text
Add Arrow
────────────────────────

[ Line ] [ 2D ] [ 3D ]

Geometry
  Shaft width
  Head width        // only 2D / 3D
  Head length       // only 2D / 3D

Appearance
  Color
  Opacity

▸ Placement
▸ Advanced

[ Done ]
```

Visibility rules:
- `Line`: no orientation, no plane, no head, no outline unless 3D/line outline is actually implemented.
- `Arrow2D`: orientation visible.
- `Arrow2D + Billboard`: no plane; geometry units displayed as px.
- `Arrow2D + FixedPlane`: plane visible; geometry units are still displayed as px. Orientation changes plane/basis only, never unit semantics.
- `Arrow3D`: no orientation/plane; head visible; outline hidden.
- Changing Type 2D <-> Line/3D resets only incompatible geometry dimensions to target-kind defaults; it preserves endpoints and appearance. This prevents px values from becoming world-space values or vice versa.
- `Alpha` must never appear in user-facing text; use `Opacity`.
- `Border` must never appear for arrows; use `Outline`.

## 10. Rendering specification

### Arrow2D

Geometry/SDF:
- Render one quad per arrow, but shape is defined in `arrow_quad.frag` as SDF union of shaft rounded rect and triangular head.
- The quad must cover shaft + head + outline + AA margin.
- Use `fwidth` on SDF distance for anti-aliasing.

Head:
- Triangle with base at `tip - headLength`, base half width `headWidth * 0.5`, tip at `end`.
- Clamp `headLength` for short arrows.
- Head joins shaft by union SDF; optionally slightly overlap shaft into head by `shaftWidth * 0.5` in local SDF to avoid shoulder cracks.

Outline:
- Outline only when `style.outlineWidth > 0`.
- Outline color from `style.outlineColor`.
- Width is in px for both Billboard and FixedPlane.

Sizing:
- `Arrow2D` style geometry is screen-space for both orientations.
- Billboard: convert px to world at arrow center each frame using camera-right/up projection probes.
- FixedPlane: retain the selected world-plane basis, but derive independent pixel-to-world scales along the local in-plane `right` and `up` axes from projection probes at the arrow center. Use those scales to expand the quad while keeping its visual width/head approximately stable on screen.
- Do not mutate or reinterpret the stored style values when orientation changes.

Billboard:
- Plane normal follows camera; basis from projected arrow direction. If direction projects near zero, fallback axis must still be perpendicular to camera normal.

FixedPlane:
- Plane normal is XY -> +Z, XZ -> +Y, YZ -> +X.
- Disable culling for arrow quad draw or draw two-sided.
- Edge-on: if projected area falls below a small threshold, either show a thin billboard fallback or accept near-invisible geometry. Recommendation: fallback to a minimum screen-space strip for hit-test only, not rendering, unless runtime shows user confusion.

Anti-aliasing:
- SDF `aa = max(fwidth(dist), 0.0001)`.
- Discard outside outline+AA.

### Arrow3D

Shaft:
- Reuse cylinder mesh and `buildBondTransform`.
- Correct the semantic mismatch: `style.shaftWidth` is full diameter; renderer uses `shaftRadius = style.shaftWidth * 0.5f`.
- Extend shaft slightly under head to avoid gap after cap removal.

Shoulder/head:
- Cone base cap removed for SceneArrow.
- `style.headWidth` is also full base diameter; renderer uses `headRadius = style.headWidth * 0.5f`.
- Head length is world-space.
- Use a small **geometry-limited** overlap, not exact coplanar contact. Clamp overlap below `headLength * (1 - shaftRadius / headRadius)` with a safety margin so the cylinder cannot protrude through the cone surface.

Caps:
- Cylinder caps are not indexed in `BuildRefinedCylinderMesh`; leave as-is.
- Cone base cap should not be indexed for SceneArrow.

Normals:
- Keep lateral cone normals from `SafeNormalize(vec3(cos, sin, radius / height), +Z)`.
- If the cap is removed, no cap normal remains.

Shading:
- Either shared `bonds.frag` with alpha and specular scale, or small `annotation_bonds.frag`.
- Target: lower specular, same diffuse lighting, alpha honored.
- If `Line`/`Arrow3D` alpha below 1 is supported, transparency must also manage depth writes; fragment alpha alone is insufficient.

Depth:
- Current arrows render before atoms with depth test enabled. Keep for opaque 3D if arrows should be spatial objects.
- For transparent `Line`/`Arrow3D`, verify blend/depth state. Prefer depth test ON and depth writes OFF for the transparent arrow draw, restoring the previous state afterwards. Do not introduce a global sorted-transparency subsystem in this rework.
- For 2D annotation readability, consider drawing Arrow2D after atoms/isosurfaces with depth disabled like labels. RUNTIME VERIFICATION REQUIRED: choose based on whether arrows should point through structures in figure exports or be occluded as world objects.

## 11. Undo/redo and state safety

Add:
- Before pushing a new arrow, call `PushPinnedMeasurementUndoSnapshot`.
- After push, set selection and quick edit to new index.
- Redo history is cleared by snapshot helper.

Property edit:
- Push snapshot once at edit activation before mutating the model.
- ImGui controls must edit temporary local values; assign the temporary value to `SceneArrow` only after the activation snapshot has been created.
- Never call `DragFloat/InputFloat3/ColorEdit...` directly on a `SceneArrow` field and then snapshot afterwards.
- Do not push every frame while a slider is active.
- For enum/button/combo assignments, snapshot directly before assignment.

Drag:
- Current `handleSceneArrowInteraction` snapshots at drag start. Preserve this.
- Drag release does not need another snapshot.
- Escape/cancel is not implemented for arrow drag; do not add unless requested.

Delete:
- Snapshot before erase.
- Sort descending and unique indices.
- Clear `selectedSceneArrows`, `sceneArrowDragging`.
- Close quick edit on erase, or adjust index carefully.

Multi-select:
- Style bulk edit can continue to copy representative style to selected arrows, but must snapshot once before first style mutation.
- Do not add multi-edit for start/end unless explicitly scoped; avoid surprising geometry edits across several arrows.
- After undo/redo, keep existing behavior: clear selections and quick-edit.

Quick edit:
- Add snapshot already covers creation.
- Subsequent quick-edit property changes need their own snapshots by Step 3.
- Closing quick edit is UI state only; no snapshot.

Undo/redo:
- Keep `UndoLabelsChange` / `RedoLabelsChange` restoring all three vectors.
- Keep `SceneSystem::SyncLabelEntities`; arrows have no entities, so no arrow sync is needed.

## 12. Bug matrix

| Issue | Current status | Root cause | Affected files | Required fix | Included in this rework? |
|---|---|---|---|---|---|
| Arrow2D has no proper head | Present in code | Uses rectangular `label_background.frag`; `headWidth/headLength` unused for Arrow2D | `OpenGlRendererBackend.cpp`, `arrow_quad.vert`, `label_background.frag` | Add `arrow_quad.frag`, extend instance data, use SDF arrow shape | yes |
| Arrow2D size changes with zoom | Present in code | `halfSize` and outline are world-space | `OpenGlRendererBackend.cpp`, `OpenGlRendererBackend.hpp` | Treat all Arrow2D geometry fields as px and convert px->world per frame for Billboard and FixedPlane | yes |
| Arrow2D orientation could reinterpret units | Design bug in prior plan / must not be introduced | One shared `ArrowStyle` cannot safely mean px in Billboard and world units in FixedPlane | `RendererWindowState.hpp`, `OpenGlRendererBackend.cpp`, editor | Keep px semantics for both orientations; orientation only changes basis | yes |
| Changing ArrowKind could reinterpret px as world units | Must be prevented in rework | Same geometry fields are px for Arrow2D and world-space for Line/Arrow3D | `SceneArrowEditorWidget.hpp`, `ObjectPropertiesPanel.cpp`, defaults helper | Route kind changes through target-kind geometry helper; preserve appearance/endpoints | yes |
| `shaftWidth` / `headWidth` have inconsistent semantics | Present in code | Shaft width is used as cylinder radius while head width is used as cone diameter | `RendererWindowState.hpp`, `OpenGlRendererBackend.cpp` | Make both fields full width/diameter; derive radii in renderer | yes |
| FixedPlane sometimes disappears | Likely present | Single quad with `GL_CULL_FACE` enabled; edge-on projected area can vanish | `OpenGlRendererBackend.cpp`, `arrow_quad.vert` | Disable culling for arrow quads; document edge-on behavior | yes |
| Click/hit-test misses arrow | Present for thick/head cases | Hit-test uses fixed point-to-segment radius and ignores visual head/width | `RendererPanel.cpp`, `SelectionHitTest.*` | Match screen-space shaft/head tolerance | yes |
| Region select misses long arrow crossing selection box | Present | Only start/end/midpoint tested | `RendererPanel.cpp` | Segment-vs-region or denser simple sampling | yes |
| Arrow3D shows disk/suction cup | Present by code | Cone base cap is generated and indexed; shaft/head meet at cap plane | `OpenGlRendererBackend.cpp` | Capless cone for arrows + overlap | yes |
| Arrow3D/Line opacity slider does nothing | Present by code | `bonds.frag` outputs alpha `1.0` | `bonds.frag`, `OpenGlRendererBackend.cpp` | Use mixed instance alpha; if transparency is enabled, also handle depth writes/restoration explicitly | yes |
| Arrow3D too shiny/plastic | Present depending settings | Reuses global bond specular material | `bonds.frag`, `RendererSettings.hpp`, `OpenGlRendererBackend.cpp` | Add annotation specular scale or small annotation shader | yes |
| X in ObjectProperties does not delete | RUNTIME VERIFICATION REQUIRED | Static code has erase; possible stale quick-edit/index/overlap issue remains | `ObjectPropertiesPanel.cpp`, `RendererPanel.cpp` | Central erase helper, close/normalize quick edit | yes |
| Right-click Delete does not delete arrows | Present if user expects it | Context menu Delete is atom-selection command gated by `selectedAtomIndices` | `RendererPanel.cpp` | Optional annotation-aware delete branch; otherwise document as out of scope | no/optional |
| Property edit undo missing | Present in code | Style/placement fields mutate directly without snapshot | `ObjectPropertiesPanel.cpp`, `RendererPanel.cpp` | Snapshot once on item activation | yes |
| Defaults require manual fixing | Present | `MakeDefaultSceneArrow` length fixed 1.0 and kind Arrow3D | `ObjectPropertiesPanel.cpp`, `SceneArrowEditorWidget.hpp`, add call sites | Scene-relative default Arrow2D Billboard | yes |

## 13. Tests / verification

Automatic:
- Build: `python scripts/python/build.py --target DefectStudioTests`
- Run tests: `build/bin/Release-windows-x86_64/DefectStudioTests/DefectStudioTests.exe --gtest_brief=1` or matching config output.
- Add tests to `tests/Renderer/Scene/SelectionHitTestTests.cpp` for point-to-segment, head triangle, segment-region helper.
- If default length helper is non-ImGui and accessible, add a small renderer test using known atom bounds.

Manual checklist:

Line:
- Add via Shift+A menu.
- Add via right-click Add.
- Add via ObjectProperties.
- Edit start/end and shaft width.
- Drag middle.
- Delete via viewport Delete and ObjectProperties X.
- Undo and redo add/edit/drag/delete with `Ctrl+Alt+U` / `Ctrl+Alt+Shift+U`.

Arrow2D Billboard:
- Add and verify default is readable.
- Rotate camera: arrow remains camera-facing.
- Zoom in/out: shaft/head/outline stay visually stable.
- Head visible and triangular.
- Outline visible at 1-1.5 px and absent at 0.
- Hit-test shaft and head.
- Drag start/end/middle.

Arrow2D FixedPlane:
- Test XY, XZ, YZ.
- View from positive and negative side of plane; no one-sided culling disappearance.
- Zoom in/out: visual shaft/head/outline size remains approximately stable.
- Switch repeatedly `Billboard -> FixedPlane -> Billboard`; stored geometry values stay unchanged and no size jump occurs.
- View parallel to plane.
- View almost edge-on.
- No NaN, no exploding quad, no flicker.

Arrow3D:
- View from multiple camera directions.
- No visible base disk.
- No gap at shaft/head.
- No z-fighting at shoulder.
- Cylinder does not poke through the narrowing cone for min/default/max allowed proportions.
- `shaftWidth` and `headWidth` behave as full diameters.
- Normals look smooth enough.
- Head proportions match defaults.
- If opacity below 1 is enabled: transparent arrow does not incorrectly hide later-drawn atoms because of depth writes, and GL state is restored after draw.
- Specular is subdued compared with bonds.

Multi-selection:
- Select several arrows in viewport and outliner.
- Ctrl-click toggle.
- Drag group.
- Delete group.
- Undo group drag/delete.
- Bulk style edit snapshots once and applies to selected arrows.

Quick edit:
- Create arrow.
- Popup opens at viewport bottom-left.
- Change `Arrow2D -> Arrow3D -> Line -> Arrow2D`: no gigantic/tiny geometry caused by unit reinterpretation; target defaults are applied as specified.
- Verify color/opacity/endpoints survive kind changes.
- Change geometry.
- Change appearance.
- Expand/collapse Placement/Advanced.
- Close with Done.
- Undo creation and each property edit.

Export/preview:
- Open export dialog path that copies `dialog.previewState.sceneArrows`.
- Verify arrows render with `sceneOffset` and are included in captured output.

## 14. Files expected to change

| File | Purpose of change | Risk | Approximate scope |
|---|---|---|---|
| `src/Presentation/Panels/SceneArrowEditorWidget.hpp` | Add editor mode, default overload and erase helper declaration | Low | Small API extension |
| `src/Presentation/Panels/ObjectPropertiesPanel.cpp` | UI sections, defaults implementation, full editor, delete helper use, undoable property edits | Medium | Moderate localized UI work |
| `src/Presentation/Panels/RendererPanel.cpp` | Compact quick edit call, hit-test update, delete helper use | Medium | Local interaction changes |
| `src/Presentation/Panels/RendererPanelToolbar.cpp` | Use new default overload in add menu | Low | Few lines |
| `src/Renderer/OpenGl/OpenGlRendererBackend.hpp` | Extend `OpenGlArrowQuadInstance` layout | Medium | Struct/VAO layout |
| `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` | Arrow2D SDF instance setup, shader load, culling, capless cone, 3D material uniforms | High | Main render risk |
| `src/Renderer/OpenGl/Shaders/arrow_quad.vert` | Pass new varyings for arrow SDF | Medium | Shader interface |
| `src/Renderer/OpenGl/Shaders/arrow_quad.frag` | New 2D arrow SDF shader | High visual | New shader |
| `src/Renderer/OpenGl/Shaders/bonds.frag` | Honor alpha and optional specular scale | Medium | Shared shader but low behavior change if defaults set |
| `src/Renderer/Scene/SelectionHitTest.hpp` | Expose small screen-space math helpers | Low | Small helper API |
| `src/Renderer/Scene/SelectionHitTest.cpp` | Implement hit-test helpers | Low | Deterministic math |
| `tests/Renderer/Scene/SelectionHitTestTests.cpp` | Regression tests for picking helpers | Low | Focused tests |

## 15. Files that must NOT need changes

- `src/Domain/**`: `SceneArrow` is not domain data.
- `src/Storage/**` and `src/IO/**`: no persistence/migration in this rework.
- `src/Renderer/Scene/SceneSystem.*`: no arrow ECS entities.
- `src/Core/Undo/**`: keep local label undo stack.
- `src/Core/Commands/**` except no change needed for existing label undo commands.
- `install/app/assets/renderer/meshes/*.obj`: cap fix should be procedural in backend, not by editing assets.
- `premake5.lua`: adding a shader file under existing shader directory should not require project generation changes unless build packaging explicitly enumerates shaders. Verify first before editing.

## 16. Implementation order for Claude

1. Add `SceneArrowEditorMode` and route quick-edit to `Compact` with no visual behavior change.
2. Centralize deletion/index cleanup and make X/Delete use the same helper.
3. Ujednolić semantykę geometrii: `shaftWidth/headWidth` = pełna szerokość; całe Arrow2D używa px niezależnie od orientation; dodać helper zmiany ArrowKind, aby px nigdy nie były reinterpretowane jako world units.
4. Add scene-relative default overload and switch all add call sites.
5. Refactor full/compact UI sections and labels.
6. Add undo snapshots for property edits using temporary UI values and snapshot-before-assignment.
7. Implement Arrow2D SDF shader and extended instance layout with px->world conversion for Billboard and FixedPlane.
8. Disable culling around Arrow2D quad draw and verify FixedPlane.
9. Remove cone base cap for SceneArrow and add geometry-limited shaft/head overlap.
10. Honor alpha and reduce 3D arrow specular; if alpha < 1 is enabled, handle depth writes/restoration explicitly.
11. Improve hit-test math and add `SelectionHitTest` tests.
12. Run automated tests and complete the manual rendering checklist.

## RUNTIME VERIFICATION REQUIRED

- Confirm whether default direction should be `+X` or existing `+Z` after seeing the app's default camera/framing. Recommendation before runtime: `+X` for a 2D billboard annotation, fallback `+Z` only if `+X` clashes with common scenes.
- Confirm whether Arrow2D should render after atoms with depth disabled like labels, or remain depth-tested before atoms. Recommendation before runtime: draw `Arrow2D Billboard` late/annotation-like; keep `Arrow3D` depth-tested.
- Confirm the reported ObjectProperties `X` failure. Static code currently erases, but index/quick-edit state is brittle; the central helper is still recommended.
- Confirm chosen Arrow2D pixel defaults (`8/22/28/1.25`) on real high-DPI viewport for both Billboard and FixedPlane; adjust only within narrow ranges if they look too heavy/light.
- Confirm whether simple unsorted 3D annotation transparency (depth test ON, depth writes OFF) is visually acceptable. If not, keep `Line`/`Arrow3D` opaque in this rework rather than introducing a general transparency sorting subsystem.
