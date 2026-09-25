# Defect Studio — kierunki rozwoju inspirowane Blenderem

## Kontekst

Projekt: `SirJamesClarkMaxwell/Defect-Studio-mgr`

Aktualnie najbardziej rozwinięta gałąź podczas tej analizy:
`task/40-arrow-module-fixes`

Stan aplikacji już dziś sugeruje, że Defect Studio przestaje być jedynie viewerem struktur i zaczyna przypominać specjalistyczny **scientific scene editor** dla fizyki defektów:

- C++ + OpenGL
- Dear ImGui
- ImGuizmo
- GLFW / GLAD
- GLM
- EnTT
- yaml-cpp
- GoogleTest
- ImPlot
- nanobind / Python bridge
- Outliner
- Object Properties
- 3D cursor
- G / R / S
- selection modes
- atomy i wiązania
- scene arrows
- planes
- procedural orbitals
- WAVECAR orbitals
- labels
- displacement comparison
- occupation diagram
- point-group analysis
- eksport obrazów

Z tego powodu warto projektować dalsze funkcje nie jako kolejne lokalne dodatki, lecz jako spójny system sceny.

---

# 1. Najważniejszy kierunek: nowy system Path / Annotation

Najważniejsza decyzja dotycząca strzałek:

**nie projektować osobnych bytów `Arrow2D`, `Arrow3D`, `CurvedArrow2D`, `CurvedArrow3D`, `Line`, `DashedLine` itd.**

Lepszy model:

```text
ScenePath
 ├─ PathGeometry
 │   ├─ spline/control points
 │   ├─ handles
 │   ├─ cyclic
 │   └─ anchors
 │
 ├─ StrokeStyle
 │   ├─ profile
 │   ├─ width / width profile
 │   ├─ color ramp
 │   ├─ dash pattern
 │   ├─ caps / joins
 │   └─ depth mode
 │
 ├─ EndpointStyle
 │   ├─ start decoration
 │   └─ end decoration
 │
 └─ Placement
     ├─ World3D
     ├─ FixedPlane
     ├─ CameraFacing
     └─ ScreenSpace
```

Wtedy:

```text
Line        = ScenePath preset
Arrow       = ScenePath + end decoration
DoubleArrow = ScenePath + 2 decorations
CurvedArrow = ScenePath with Bezier/Arc
Rotation    = ArcPath + Arrow decoration
DashedLine  = ScenePath + DashPattern
```

To jest znacznie bardziej skalowalne niż mnożenie typów obiektów.

---

# 2. Co konkretnie warto przejąć z systemu krzywych Blendera

## 2.1 Curve / Path jako podstawowy obiekt

`Arrow`, `Line`, `Arc`, `RotationArrow` powinny być konfiguracjami jednego systemu.

Path powinien wspierać co najmniej:

- Line
- Polyline
- Arc
- Bezier
- Circle
- później ewentualnie Spiral

Obecne `points + optional controlPoint` jest dobrym etapem przejściowym, ale docelowo jest zbyt ograniczone.

---

## 2.2 Cubic Bézier handles

Każdy punkt powinien móc mieć:

- `handleLeft`
- `handleRight`

Typy uchwytów:

- Auto
- Aligned
- Free
- Vector

To daje:

- gładkie krzywe
- ostre narożniki
- kontrolę ciągłości stycznej
- ręczną edycję krzywizny

---

## 2.3 Primitive paths

Przydatne prymitywy:

- Line
- Arc
- Circle
- Bezier
- Spiral

Dla operacji symetrii szczególnie ważny jest `Arc`.

Przykład:

```text
RotationArrow
    center
    axis
    radius
    startAngle
    endAngle
    clockwise / counterclockwise
```

---

## 2.4 Rotation / Symmetry Arrow jako preset

Nie nowy renderer.

To powinno być:

```text
ArcPath + ArrowTip + optional Label
```

Wtedy `C3` może automatycznie generować:

```text
angle = 120 degrees
```

---

## 2.5 Jeden system 2D / 3D

Zamiast osobnych implementacji:

- Arrow2D
- Arrow3D

lepiej:

```text
Path + RenderMode
```

Render modes:

- FlatRibbon
- CameraFacingRibbon
- Tube3D
- ScreenSpaceStroke

---

## 2.6 Profile zamiast „cylinder + cone”

Profile:

- Round
- Flat
- Square
- Custom2D

Jedna ścieżka może wtedy być:

- cienką linią
- szeroką wstęgą
- rurką
- płaską curved arrow jak na przykładzie do operacji symetrii

---

## 2.7 Width jako funkcja po ścieżce

Zamiast jednego `shaftWidth`:

```text
width(t)
```

Profile:

- Constant
- Linear
- TaperStart
- TaperEnd
- Custom keypoints

---

## 2.8 Taper profile

Przykład:

```text
WidthProfile::Evaluate(t)
```

Dzięki temu można uzyskać eleganckie scientific arrows, zamiast mechanicznego cylindra.

---

## 2.9 Tilt / Twist

Dla ribbonów 3D trzeba określić orientację przekroju wzdłuż ścieżki.

Każdy control point może mieć:

```text
tilt
```

Bez tego szerokie curved ribbons będą się skręcać w sposób trudny do kontrolowania.

---

## 2.10 Moving frame

`PathEvaluator` powinien zwracać np.:

```text
position
tangent
normal
binormal
arcLength
normalizedT
```

Dla 3D lepiej używać parallel-transport frame niż niezależnego `MakeBasis()` dla każdego segmentu.

---

## 2.11 Join styles

Dla polyline / ribbon:

- Miter
- Bevel
- Round

---

## 2.12 Cap styles

Dla linii bez tipów:

- Butt
- Square
- Round

---

# 3. Endpoint decorations / arrow tips

Obecne:

- None
- Plain
- Barbed
- Open
- Bar
- Circle

to dobry start.

Docelowo abstrakcja lepiej nazywałaby się:

```text
EndpointDecoration
```

Możliwe style:

- Arrow
- OpenArrow
- Stealth
- Triangle
- Diamond
- Circle
- Square
- Bar
- DoubleBar
- Hook

Każdy koniec niezależny.

---

## 3.1 Parametry tipów

Przydatne:

```text
tipLength
tipWidth
tipInset
tipAngle
tipScaleMode = World | Screen
tipColorMode = Inherit | Custom
```

---

## 3.2 Custom tip geometry

Później tip może być assetem zamiast enumem.

Nie trzeba implementować od razu, ale architektura nie powinna tego blokować.

---

# 4. Stroke system

## 4.1 Zamiast `bool dashed`

Lepsze:

```text
StrokePattern
    Solid
    DashPattern
```

`DashPattern`:

```text
[on, off, on, off, ...]
offset
```

Daje:

- dashed
- dotted
- dash-dot
- custom

---

## 4.2 Dash phase / offset

Możliwość przesuwania wzoru wzdłuż ścieżki.

---

## 4.3 Dash liczony po arc length

To akurat obecny system robi dobrze i warto to zachować.

Pattern nie powinien resetować się na każdym segmencie.

---

## 4.4 Dotted jako realne kropki

Nie bardzo krótkie dash.

Tryb:

- circle billboard
- sphere

zależnie od render mode.

---

# 5. Kolor i styl

## 5.1 Multi-stop Color Ramp

Zamiast tylko dwóch kolorów:

```text
ColorRamp:
  - position
  - color
  - alpha
```

Przykłady:

- blue → white → red
- low displacement → high displacement
- phase gradient
- opacity fade

---

## 5.2 Gradient coordinate modes

Np.:

- AlongPath
- WorldX
- WorldY
- WorldZ
- Custom

---

## 5.3 Opacity profile

```text
alpha(t)
```

Fade in / fade out bez tworzenia osobnych obiektów.

---

## 5.4 Shared styles

Warto mieć reusable:

```text
PathStyle
```

Presety:

- SymmetryOperation
- Displacement
- Spin
- ElectricField
- CoordinateAxis
- DefectAnnotation
- Measurement

---

# 6. Edit Mode jak w Blenderze

## Object Mode

`G/R/S` działa na cały obiekt.

## Edit Mode

Edytuje:

- control points
- handles
- width
- tilt
- selected segments

Przydatne skróty:

- `Tab` — Object/Edit mode
- `G` — move point
- `E` — extrude point
- `Delete` — remove point
- `V` — handle type
- `1/2/3` — point/segment/spline selection

To jest dużo bardziej skalowalne niż `Start / End / Both`.

---

# 7. Operacje na krzywych

Przydatne:

- Extrude point
- Dissolve point
- Reverse path
- Cyclic / non-cyclic
- Trim path
- Resample path
- Split
- Join

---

## 7.1 Adaptive tessellation

Obecne `curveSegments = 24` może zostać jako manual override.

Domyślnie warto tessellować adaptacyjnie według:

- curvature
- projected screen size
- zoom
- export resolution

---

# 8. Modifier Stack

To jedna z najlepszych rzeczy do zapożyczenia z Blendera.

Dla path:

```text
Path
 -> Smooth
 -> Resample
 -> Trim
 -> Dash
 -> Taper
 -> Endpoint decorations
 -> Camera-facing / Tube / Ribbon
```

Dla plane:

```text
Plane
 -> FitToAtoms
 -> Offset
 -> Clip
```

Dla orbital:

```text
Orbital
 -> Stretch
 -> Rotate
 -> PhaseColor
 -> Clip
```

Nie trzeba od razu robić node editora.

---

# 9. Snapping i anchors

Obecne atom anchoring warto rozwinąć w ogólny system.

Snapping:

- atom
- bond midpoint
- bond axis
- plane
- plane intersection
- cell corner
- fractional grid
- lattice site
- object origin
- curve point
- active object
- 3D cursor

---

# 10. Constraint system

Zamiast osobnych:

```text
startAnchorAtom
endAnchorAtom
anchorAtoms
```

warto mieć:

```text
Constraint
```

Przykłady:

- CopyPosition(atom)
- Midpoint(atomA, atomB)
- AlignToBond
- AlignToPlane
- TrackToAtom
- MaintainDistance
- AttachToStructure
- FollowSelection

Constrainty mogą działać jako stack.

---

# 11. Scientific Transform Orientations

Oprócz zwykłego XYZ:

- Cartesian
- lattice a,b,c
- reciprocal a*,b*,c*
- bond frame
- plane frame
- defect frame
- symmetry-axis frame

Masz już fundament przez alignment do a/b/c i a*/b*/c*.

---

# 12. Screen-space vs World-space

To powinno być jawne:

```text
WidthSpace::World
WidthSpace::ScreenPixels
```

Nie przez zmianę znaczenia tych samych pól zależnie od `ArrowKind`.

---

# 13. Depth modes

Przydatne:

- DepthTest
- AlwaysOnTop
- XRay
- FadeWhenOccluded

Scientific annotations często wymagają niezależnej kontroli względem geometrii.

---

# 14. Attached labels

Path może mieć label:

```text
t = 0.5
offset
orientation
```

Przykłady:

- `C₃`
- `120°`
- `σ_v`
- bond / displacement value

Label podąża za ścieżką po zmianie promienia czy kąta.

---

# 15. Semantic scientific metadata

Ponad geometrią:

```text
semanticRole = SymmetryOperation
```

Przykład:

```text
SymmetryOperationData
    operation = C3
    axis
    angle = 120°
```

Renderer pozostaje neutralny.

Group Theory Panel może generować takie obiekty automatycznie.

---

# 16. Procedural symmetry annotations

Automatycznie:

```text
C_n -> curved arrow + axis
σ   -> plane
i   -> inversion-center marker
S_n -> rotation + reflection
```

---

# 17. Vector Glyph system

Nie tworzyć setek pełnych `SceneArrow`.

Zrobić instanced:

```text
VectorGlyphSet
```

Zastosowania:

- forces
- phonons
- displacements
- magnetic moments
- electric fields
- dipoles

Styl arrowheads może być wspólny z ScenePath.

---

# 18. Geometry cache

Nowy system powinien mieć:

```text
PathGeometryCacheKey
```

obejmujący:

- spline data
- evaluated width
- tip geometry
- render mode
- tessellation

Kolor i alpha często nie muszą invalidować mesh cache.

---

# 19. Undo na poziomie operacji

Zamiast tylko snapshotów widgetów:

- MoveControlPoint
- SetHandleType
- AddSplinePoint
- DeleteSplinePoint
- ChangeStrokeStyle
- ChangeConstraint
- ChangeModifier

To będzie dużo mniej kruche.

---

# 20. Scene Architecture v2 — najważniejsza większa przebudowa

Obecny `RendererWindowState` już zawiera bardzo wiele obiektów sceny i stanu interakcji.

Przy dalszym rozwoju warto przejść do separacji:

```text
SceneObject
    transform
    visibility
    parent
    constraints
    data_id
    style_id

ObjectData
    PathData
    PlaneData
    OrbitalData
    VolumeData
    GlyphData

StyleData
    StrokeStyle
    SurfaceStyle
    TextStyle

ScientificBinding
    AtomBinding
    BondBinding
    CalculationBinding
    SymmetryBinding
    OrbitalBinding
```

Przykład:

```text
Arrow = SceneObject
        Data = PathData
        Style = ArrowStyle
```

Operacja `C3` może być group objectem:

- axis
- curved path
- arrowhead
- label
- optional ghost atoms

---

# 21. Collections i View Layers

Bardzo wysoki priorytet.

Collections:

- Structure
- Orbitals
- Symmetry
- Annotations
- Displacements
- Measurements
- Reference structure

Każda:

- visible
- renderable
- selectable
- lock
- solo

View Layers:

- Geometry
- C3v symmetry
- Electronic states
- Figure 3a
- Figure 3b

Ten sam projekt, różne widoki tej samej sceny.

---

# 22. Parenting i obiekty złożone

Przykład:

```text
C3 Operation
 ├─ axis
 ├─ curved arrow
 ├─ C3 label
 └─ ghost atoms
```

Przesunięcie / ukrycie / styling całej grupy jednym kliknięciem.

---

# 23. Asset Browser

Scientific assets:

- arrow styles
- arrowheads
- color ramps
- orbital presets
- symmetry glyphs
- label styles
- camera presets
- coordinate systems
- figure themes
- reusable object groups

---

# 24. Linked duplicates / shared data

Wiele obiektów może współdzielić:

- geometry data
- style
- material-like appearance

Zmiana assetu aktualizuje wszystkie instancje.

---

# 25. Camera Objects i Figure Shots

Kamery jako pełnoprawne scene objects.

Named shots:

- Fig_2a
- Fig_2b
- C3_axis
- Top_hBN

Każdy shot może zapisywać:

- camera transform
- projection
- View Layer
- background
- resolution
- crop
- render settings

---

# 26. Composition guides

Przydatne w camera mode:

- thirds
- center
- diagonals
- golden ratio
- safe frame
- passepartout

---

# 27. Batch “Render All Figures”

Przykład:

```text
Fig_1a -> PNG
Fig_1b -> PNG
Fig_2_symmetry -> PNG
Fig_S1 -> PNG
```

Jednym kliknięciem regenerujesz wszystkie grafiki po zmianie geometrii lub stylu.

---

# 28. Render passes

Możliwe osobne warstwy:

- atoms
- bonds
- orbitals
- annotations
- labels
- symmetry
- depth
- object ID

---

# 29. Raster + vector overlay

Bardzo interesujący kierunek publikacyjny.

Raster:

- atoms
- bonds
- orbitals
- volumetric data

Vector:

- paths
- arrows
- labels
- axes
- measurement lines

Eksport:

- PNG + SVG
- PDF
- layered compositing

---

# 30. Clipping Plane / Section Plane

Masz już `ScenePlane`.

Dodać tryby:

```text
Use as clipping plane
Use as slice plane
```

Można przecinać:

- atoms
- orbitals
- charge density
- spin density
- potential
- full scene

---

# 31. Volume / Slice objects

Uniwersalny:

```text
VolumeGridData
```

Tryby:

- isosurface
- XY slice
- XZ slice
- YZ slice
- arbitrary plane slice
- contour lines
- line profile

Zastosowania:

- WAVECAR
- CHGCAR
- PARCHG
- LOCPOT
- ELF
- spin density

---

# 32. Timeline jako scientific trajectory

Nie animator do filmów, tylko oś fizycznych stanów.

Źródła:

- OUTCAR ionic steps
- XDATCAR
- NEB images
- GS → ES relaxation
- phonon eigenmode
- molecular dynamics

---

# 33. Onion skinning / ghost structures

Świetne do:

- relaxation
- Jahn-Teller
- GS vs ES
- displacement
- NEB

Przykład:

- previous geometry — faded red
- current — normal
- next — faded blue

---

# 34. Annotation Layers

Osobne warstwy:

- Symmetry
- Notes
- Labels
- Measurements
- Mechanism

Każda z:

- opacity
- lock
- solo
- masks
- visibility

---

# 35. Attribute / Field system

Każdy atom/bond/path/point może mieć attributes:

- charge
- spin
- displacement
- localization
- coordination
- force
- Bader charge

Potem:

```text
color  <- charge
radius <- |spin|
length <- |force|
alpha  <- localization
```

To jest bardziej uniwersalne niż dodawanie osobnej logiki dla każdego rodzaju danych.

---

# 36. Geometry Nodes Lite

Nie robić teraz node editora.

Ale można przyjąć logikę:

```text
Selection
 -> Generator
 -> Modifier
 -> Style
```

Przykłady:

```text
Selected atoms -> put pz orbital
Atoms with displacement > threshold -> vector arrows
Point-group operations -> symmetry overlays
Nearest neighbours -> labels
```

UI może być zwykłym panelem lub Python API.

---

# 37. Symmetry Objects jako specjalność Defect Studio

Tutaj aplikacja może być lepsza niż Blender.

Kliknięcie `C3`:

- pokaż axis
- curved arrow 120°
- label `C₃`
- optional transformed ghost structure

Kliknięcie `σv`:

- plane
- normal
- reflected ghost structure

Kliknięcie `S3`:

- rotation
- reflection

---

# 38. Interactive representation mapping

Klikasz element grupy:

```text
C3
```

i aplikacja pokazuje:

```text
a -> b
b -> c
c -> a
```

oraz:

- matrix representation
- character
- basis mapping

Klikasz `σv`:

- widać permutację
- widać zmianę fazy orbitalu
- widać basis vectors

---

# 39. SALC visualizer

Na podstawie projection operators:

```text
A1 = c1 σN + c2(σ1 + σ2 + σ3)
```

Można wizualizować:

- coefficient magnitude jako size
- sign jako phase color
- degeneracy partners
- basis site labels

---

# 40. Selection Sets

Named selections:

- C-dimer
- first coordination shell
- defect core
- equivalent N atoms
- E basis
- mirror-plane atoms

Analiza odnosi się do nazwanego `SelectionSet`, nie chwilowego zaznaczenia.

---

# 41. Measure Tool 2.0

Masz bond length i angle.

Dodać:

- dihedral angle
- point-plane distance
- plane-plane angle
- atom-axis distance
- projected distance
- vector components
- lattice coordinates
- cell angles
- polygon area
- local coordination

---

# 42. Local View / isolate selection

Jednym skrótem:

- tylko defect
- defect + first shell
- tylko wybrane orbitale
- tylko symmetry objects

Bez ręcznego klikania visibility.

---

# 43. Structure modifier stack

Bardzo ciekawy długoterminowy kierunek:

```text
Primitive hBN
 -> Supercell 9x9x1
 -> Vacancy B
 -> Vacancy N
 -> Insert C2
 -> Strain
 -> Symmetrize
```

Każdy krok:

- niedestrukcyjny
- parametryczny
- z provenance
- możliwy do zmiany

To może bardzo dobrze współgrać z `DefectModel`.

---

# 44. Scientific provenance

Każdy visual object może wiedzieć, skąd pochodzi.

Przykłady:

```text
WAVECAR:
calc X
band 314
spin up
```

```text
Displacement:
POSCAR -> CONTCAR
```

```text
Symmetry plane:
sigma_v from C3v analysis
```

Styling pozostaje niezależny.

---

# 45. Workspaces

Gotowe układy UI:

- Structure
- Defects
- Electronic Structure
- Symmetry
- Compare
- Figure

Przykład:

```text
Symmetry workspace:
viewport + Group Theory + Outliner + Properties
```

```text
Figure workspace:
large viewport + camera shots + assets
```

---

# 46. Publication Composer

Długoterminowo:

- multi-panel figures
- (a), (b), (c)
- shared legends
- scale bars
- alignment guides
- consistent margins
- SVG/PDF/PNG export

To może zamknąć workflow:

```text
VASP -> analysis -> scene -> figure -> thesis
```

---

# 47. Priorytety architektoniczne

Nie wdrażałbym wszystkiego naraz.

## Faza 1 — fundamenty

1. ScenePath / Annotation v2
2. SceneObject / Data / Style separation
3. general Constraints / Anchors
4. Collections / View Layers
5. Object Mode / Edit Mode

## Faza 2 — scientific core

6. symmetry visualization
7. vector glyphs
8. scientific attributes
9. trajectory / ghost structures
10. clipping and volume slices

## Faza 3 — publication workflow

11. camera objects
12. Figure Shots
13. batch rendering
14. vector overlays
15. publication composer

---

# 48. Najważniejsze ryzyko

Nie warto dodawać kolejnych funkcji przez rozbudowywanie jednego `RendererWindowState` i dopisywanie następnych specjalnych pól.

Przykładowe symptomy:

```text
startAnchorAtom
endAnchorAtom
anchorAtoms
fixedPlane
orientation2D
sceneArrowDragTarget
...
```

Każde takie pole jest poprawne lokalnie, ale razem sugerują, że abstrakcja robi się zbyt konkretna.

Nowe funkcje powinny powstawać przez ogólne systemy:

```text
SceneObject
Data
Style
Constraint
Modifier
Collection
Binding
```

---

# 49. Czego nie kopiować bezpośrednio z Blendera

Blender warto traktować jako wzorzec designu i UX, nie źródło kodu.

Główny kod Blendera jest GPL.

Bezpieczniejsze podejście:

- inspirować się modelem danych
- inspirować się interakcją
- inspirować się workflow
- implementować własny kod

---

# 50. Ogólna wizja

Docelowo Defect Studio może być czymś w rodzaju:

```text
Blender
  +
VESTA
  +
OVITO
  +
point-group / defect analysis
  +
VASP workflow
  +
publication tool
```

ale specjalizowanym pod:

- quantum defects
- group theory
- electronic structure
- symmetry
- orbitals
- displacements
- defect transformations
- scientific figures

To może być znacznie ciekawsze niż dokładne kopiowanie funkcji Blendera.

Najbardziej wartościowy kierunek to wykorzystanie jego dojrzałych pomysłów dotyczących:

- sceny
- krzywych
- edycji
- constraints
- collections
- view layers
- modifiers
- assetów
- kamer
- timeline

i nadbudowanie nad nimi rzeczy, których Blender naturalnie nie posiada:

- symmetry operations
- SALC
- irreps
- point-group mapping
- defect provenance
- WAVECAR integration
- VASP trajectories
- scientific vector fields
- orbital / density analysis

---

# Następny sensowny dokument

Po dyskusji z Codexem / Claude warto przygotować osobny projekt:

```text
Scene Architecture v2
```

Powinien objąć:

- audit `RendererWindowState`
- audit `SceneRegistry`
- audit `SceneSystem`
- audit Outlinera
- audit Object Properties
- audit persistence
- audit transformów
- audit render pipeline
- model SceneObject/Data/Style
- model Constraint
- model Collection/ViewLayer
- ScenePath v2
- YAML migration
- podział refactoru na małe taski

Dopiero potem warto zaczynać większą implementację.
