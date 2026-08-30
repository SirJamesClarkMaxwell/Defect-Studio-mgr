# Generacja superkomórek — Plan implementacji i testowania

> **Dla agentów wykonujących:** WYMAGANY PODSKILL: użyj `superpowers:subagent-driven-development`
> (rekomendowane) lub `superpowers:executing-plans` do realizacji tego planu task po tasku. Kroki
> używają składni checkbox (`- [ ]`) do śledzenia postępu.

**Cel:** Umożliwić zbudowanie materiału od zera (układ krystalograficzny + parametry sieci + baza
atomowa) i wygenerowanie z niego superkomórki (proste N×M×K, dowolna macierz całkowitoliczbowa,
orientacja pod powierzchnię hkl), z podglądem na żywo w wielu oknach renderera i trwałą,
projektowo-scoped kolekcją materiałów do ponownego użycia.

**Architektura:** Cała matematyka sieci i superkomórki żyje w `Domain` jako czysty, synchroniczny
C++ (zero Pythona) — to standardowa krystalografia + mnożenie macierzy, nie wymaga pymatgen/ase.
Python (ASE + spglib, przez istniejący `ScriptRunner`+subprocess pattern z `PymatgenBridge`) jest
użyty tylko tam, gdzie realnie potrzebny: dobór macierzy pod orientację powierzchni i odczyt
symetrii. Materials Collection to cienka warstwa IO nad `ase.db` (SQLite) — bez nowego rejestru w
`ProjectWorkspace`; panel woła IO bezpośrednio, tak jak `ProjectTreePanel` rozmawia bezpośrednio z
systemem plików zamiast przez cache'owany rejestr domenowy.

**Tech Stack:** C++23 (Domain/Presentation/Renderer, istniejące wzorce), Python subprocess bridge
(ASE 3.28.0, spglib 2.7.0 — już w `pyproject.toml` grupie `scientific-core`, zweryfikowane
`importlib.metadata` w `.venv` tego repo), ImGui, GoogleTest, yaml-cpp (manifest projektu).

**Spec:** Ten dokument, sekcja "Kontekst i zakres funkcjonalny" niżej. Projekt funkcjonalny
wypracowany w rozmowie z użytkownikiem 2026-08-30 (brainstorming: przegląd `TODO.md`/
`old-ds-functionality.md`/`milestones.md`, audyt bibliotek ASE/pymatgen/spglib/mp-api/doped/
punktukas-tools, dwie iteracje pipeline'ów użytkownika) — zebrany bezpośrednio tutaj, bez
osobnego pliku spec, na wyraźną prośbę użytkownika.

## Global Constraints

- Branch: `task/17-supercell-generation`, utworzony z `main` po merge `task/16-calc-tools`
  (zweryfikowane: Debug+Release build obu exe + 259/259 testów zielonych w obu configach przed
  merge, `06a5709`).
- Merge do main tylko po pełnym Debug+Release build (`DefectStudio.exe` i `DefectStudioTests.exe`,
  testy zielone w obu configach) — skill `full-build-verify`.
- Pliki `.cpp` max ~500 linii — dzielić na moduły przy przekroczeniu.
- `Domain` nie zależy od UI/renderera/`App`. `Renderer` czyta typy domenowe przy budowie
  snapshotu, ale nie jest źródłem prawdy. Komunikacja cross-layer przez `EventBus`, akcje
  użytkownika przez `CommandRegistry`, długie operacje (subprocess Python) przez `JobSystem`.
- Tylko main thread commituje stan widoczny w projekcie/UI.
- Regenerować projekty premake (`scripts\Windows\GenerateProjects.bat`) po dodaniu każdego nowego
  `.cpp`/`.hpp` — premake globuje źródła przy generacji, nie przy buildzie.
- Brak wyjątków w ścieżkach renderowania (udokumentowana granica) — kod w tym planie żyje w
  Domain/ScientificRuntime/Presentation, nie w hot path renderera, więc `Result<T>`/
  `StructuredError` (nie wyjątki) jak wszędzie indziej w tych warstwach.

---

## Kontekst i zakres funkcjonalny

### W zakresie tego taska
- **Budowa materiału od zera**: wybór układu krystalograficznego (7 układów — patrz "Decyzja
  projektowa" niżej) + parametry a/b/c/α/β/γ + baza atomowa (tabela: pierwiastek + współrzędne
  frakcyjne, z opcjonalnym presetem startowym P/I/F/C).
- **Materials Collection**: kolekcja materiałów scoped do PROJEKTU (materiał może być częścią
  projektu/artykułu/pracy doktorskiej — używany wielokrotnie w ramach jednego projektu, nie tylko
  raz) + opcjonalna osobista biblioteka użytkownika (cross-project reuse). Oba scope'y na tym
  samym mechanizmie (`ase.db`), różne pliki `.db`.
- **Generacja superkomórki** z wybranego/zbudowanego materiału: "smart" input kompatybilny z
  wieloma formatami (prosty N×M×K / pełna macierz 3×3 / orientacja hkl+warstwy), jedna kanoniczna
  reprezentacja pod spodem.
- **Podgląd na żywo w 2-3 oknach renderera**: baza atomowa (bez cell boxa) / komórka elementarna (z
  cell boxem) / superkomórka.
- **Info o symetrii** (spglib) jako pomocniczy panel — sanity-check dla ręcznie budowanej struktury
  (spacegroup, point group, Wyckoff per atom).

### Świadomie POZA zakresem (zawężenie z 2026-08-30)
- Convergence testy (ENCUT/k-mesh sweep).
- Generacja defektów (`doped.DefectsGenerator`).
- Obliczenia wielkoskalowe / Calculation Profiles (funkcjonał → parametry supercomórki).
- External Search (Materials Project/COD) i Prototype Library (AFLOW) — materiał tylko "od zera"
  albo z lokalnej kolekcji w tym tasku, nie z zewnętrznych baz.
- "Ideal/auto" dobór supercomórki pod defekty (`doped`/`CubicSupercellTransformation`) — to
  wymaga Calculation Profile z listy wyżej, nie wchodzi teraz.

Wszystko powyższe zostaje w `docs/work/project/TODO.md` (Backlog po T15), ten task ich nie rusza —
tylko czysta mechanika generacji superkomórek + materiał od zera.

### Decyzja projektowa: 7 układów krystalograficznych, nie 14 sieci Bravego osobno
14 sieci Bravego różni się TYLKO pozycją punktów sieciowych w komórce (P/I/F/C/R), nie kształtem
komórki (a/b/c/kąty) — te są identyczne w obrębie jednego z 7 układów krystalograficznych (np.
Cubic P, I, F mają wszystkie a=b=c, α=β=γ=90°; to standardowa krystalografia, nie uproszczenie
tego planu). Więc: "układ krystalograficzny" (7 opcji) steruje RZECZYWISTYMI ograniczeniami
geometrii (które pola a/b/c/kąty są zablokowane), a "P/I/F/C" to tylko preset startowej bazy
atomowej (0-4 atomy wstawione automatycznie jako punkt startowy, użytkownik edytuje dalej) — nie
osobna logika budowy komórki.

### "Smart" input na rozmiar supercomórki
Jedna kanoniczna reprezentacja pod spodem: całkowitoliczbowa macierz 3×3 (`SupercellMatrix`,
Faza 1). Trzy zakładki UI PISZĄ do tej samej macierzy:
- **Simple** — 3 spinnery (a×b×c) → macierz diagonalna.
- **Matrix** — 9 pól int wprost → pełna kontrola, zawsze pokazuje AKTUALNĄ macierz niezależnie od
  tego, która zakładka ją ostatnio ustawiła (jedno źródło prawdy).
- **Surface** — hkl + liczba warstw → ASE (`ase.build.surface`/`cut`) sugeruje macierz (subprocess,
  async przez `JobSystem`), wynik ląduje w tej samej kanonicznej macierzy, użytkownik może dalej
  doedytować w zakładce Matrix.

Odczyt zawsze normalizuje: gdy macierz jest diagonalna, zakładka Simple pokazuje N×M×K; niezależnie
od zakładki zawsze widoczna jest **live liczba atomów wynikowych**
(`unitCell.atoms.size() * determinant`) PRZED zatwierdzeniem — krytyczne, bo duża supercomórka to
wolny render/DFT, użytkownik musi widzieć konsekwencję zanim kliknie Generate.

---

## Faza 1 — Domain: matematyka sieci i supercomórki (czysty C++, TDD, zero Pythona)

### Task 1: `BravaisLattice` — układ krystalograficzny → `LatticeCell`

**Pliki:**
- Create: `src/Domain/Crystal/BravaisLattice.hpp`
- Create: `src/Domain/Crystal/BravaisLattice.cpp`
- Test: `tests/Domain/Crystal/BravaisLatticeTests.cpp`

**Interfejsy:**
- Consumes: `LatticeCell` (`Domain/Crystal/CrystalPrimitives.hpp`, już istnieje —
  `std::array<glm::vec3,3> vectors` + `ToMatrix()`/`ToInverseMatrix()`).
- Produces: `enum class CrystalSystem`, `struct LatticeParameters`,
  `struct LatticeFieldConstraints`, `GetFieldConstraints(CrystalSystem) -> LatticeFieldConstraints`,
  `BuildLatticeCell(CrystalSystem, const LatticeParameters&) -> LatticeCell`,
  `enum class BravaisCenteringPreset`, `GetCenteringPresetBasis(BravaisCenteringPreset) ->
  std::vector<glm::vec3>`. Task 7 (New Structure wizard) konsumuje wszystkie cztery.

- [ ] **Step 1: Napisz failing testy**

```cpp
#include <gtest/gtest.h>

#include "Domain/Crystal/BravaisLattice.hpp"

namespace DefectStudio::Tests
{
	TEST(BravaisLatticeTests, CubicLocksBAndCToAAndAllAnglesTo90)
	{
		LatticeParameters params;
		params.a = 3.5f;
		params.b = 999.0f; // must be ignored - Cubic derives b,c from a
		params.c = 111.0f;
		params.alphaDegrees = 45.0f; // must be ignored - Cubic locks to 90

		const LatticeCell cell = BuildLatticeCell(CrystalSystem::Cubic, params);

		EXPECT_NEAR(glm::length(cell.vectors[0]), 3.5f, 1e-5f);
		EXPECT_NEAR(glm::length(cell.vectors[1]), 3.5f, 1e-5f);
		EXPECT_NEAR(glm::length(cell.vectors[2]), 3.5f, 1e-5f);
		EXPECT_NEAR(glm::dot(glm::normalize(cell.vectors[0]), glm::normalize(cell.vectors[1])), 0.0f, 1e-5f);
		EXPECT_NEAR(glm::dot(glm::normalize(cell.vectors[0]), glm::normalize(cell.vectors[2])), 0.0f, 1e-5f);
	}

	TEST(BravaisLatticeTests, HexagonalGamma120DegreesBetweenAAndB)
	{
		LatticeParameters params;
		params.a = 2.46f; // graphene-like
		params.c = 6.7f;

		const LatticeCell cell = BuildLatticeCell(CrystalSystem::Hexagonal, params);

		const float cosGamma = glm::dot(glm::normalize(cell.vectors[0]), glm::normalize(cell.vectors[1]));
		EXPECT_NEAR(cosGamma, -0.5f, 1e-5f); // cos(120 deg)
		EXPECT_NEAR(glm::length(cell.vectors[2]), 6.7f, 1e-5f);
	}

	TEST(BravaisLatticeTests, TriclinicKeepsAllSixFreeParameters)
	{
		LatticeParameters params;
		params.a = 5.0f; params.b = 6.0f; params.c = 7.0f;
		params.alphaDegrees = 80.0f; params.betaDegrees = 95.0f; params.gammaDegrees = 100.0f;

		const LatticeCell cell = BuildLatticeCell(CrystalSystem::Triclinic, params);

		EXPECT_NEAR(glm::length(cell.vectors[0]), 5.0f, 1e-4f);
		EXPECT_NEAR(glm::length(cell.vectors[1]), 6.0f, 1e-4f);
		EXPECT_NEAR(glm::length(cell.vectors[2]), 7.0f, 1e-4f);
		const float cosAlpha = glm::dot(glm::normalize(cell.vectors[1]), glm::normalize(cell.vectors[2]));
		EXPECT_NEAR(cosAlpha, glm::cos(glm::radians(80.0f)), 1e-4f);
	}

	TEST(BravaisLatticeTests, CubicFieldConstraintsLockBCAndAllAngles)
	{
		const LatticeFieldConstraints constraints = GetFieldConstraints(CrystalSystem::Cubic);
		EXPECT_TRUE(constraints.bLocked);
		EXPECT_TRUE(constraints.cLocked);
		EXPECT_TRUE(constraints.alphaLocked);
		EXPECT_TRUE(constraints.betaLocked);
		EXPECT_TRUE(constraints.gammaLocked);
		EXPECT_FLOAT_EQ(constraints.lockedAngleDegrees, 90.0f);
	}

	TEST(BravaisLatticeTests, TriclinicFieldConstraintsLockNothing)
	{
		const LatticeFieldConstraints constraints = GetFieldConstraints(CrystalSystem::Triclinic);
		EXPECT_FALSE(constraints.bLocked);
		EXPECT_FALSE(constraints.cLocked);
		EXPECT_FALSE(constraints.alphaLocked);
		EXPECT_FALSE(constraints.betaLocked);
		EXPECT_FALSE(constraints.gammaLocked);
	}

	TEST(BravaisLatticeTests, FaceCenteredPresetReturnsFourFractionalPositions)
	{
		const std::vector<glm::vec3> basis = GetCenteringPresetBasis(BravaisCenteringPreset::FaceCentered);
		ASSERT_EQ(basis.size(), 4u);
		EXPECT_EQ(basis[0], glm::vec3(0.0f));
	}
} // namespace DefectStudio::Tests
```

- [ ] **Step 2: Uruchom testy, potwierdź fail**

Run: `"./build/bin/Debug-windows-x86_64/DefectStudioTests/DefectStudioTests.exe" --gtest_filter=BravaisLatticeTests.*`
Expected: błąd linkowania/kompilacji (brak `BravaisLattice.hpp`/`.cpp`) — najpierw dograj puste
deklaracje (patrz Step 3) do premake (`GenerateProjects.bat`), potem realny `FAILED` na testach
(nie błąd kompilacji).

- [ ] **Step 3: Zaimplementuj**

`src/Domain/Crystal/BravaisLattice.hpp`:
```cpp
#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Domain/Crystal/CrystalPrimitives.hpp"

namespace DefectStudio
{
	// 7 crystal systems constrain cell SHAPE (which of a/b/c/angles are independent). The 14
	// Bravais lattices differ only in lattice-point centering (P/I/F/C/R) within a system's cell
	// shape, not in the shape itself (e.g. Cubic P/I/F all have a=b=c, all angles 90 deg) - see
	// "Decyzja projektowa" in the plan this file implements
	// (docs/work/project/plans/2026-08-30-supercell-generation.md).
	enum class CrystalSystem
	{
		Cubic,
		Tetragonal,
		Orthorhombic,
		Hexagonal,
		Trigonal, // rhombohedral axes: a=b=c, alpha=beta=gamma != 90
		Monoclinic,
		Triclinic
	};

	struct LatticeParameters
	{
		float a = 1.0f, b = 1.0f, c = 1.0f; // Angstrom
		float alphaDegrees = 90.0f, betaDegrees = 90.0f, gammaDegrees = 90.0f;
	};

	// Which fields a UI should disable for a given system - does not affect BuildLatticeCell,
	// which always derives locked fields itself regardless of what the caller passed in (so it
	// stays a total function with no invalid-input error path).
	struct LatticeFieldConstraints
	{
		bool bLocked = false;
		bool cLocked = false;
		bool alphaLocked = false;
		bool betaLocked = false;
		bool gammaLocked = false;
		float lockedAngleDegrees = 90.0f; // meaningful only where *Locked is true and system != Trigonal
	};

	[[nodiscard]] LatticeFieldConstraints GetFieldConstraints(CrystalSystem system);

	// Builds the cell vectors for `system`, deriving every locked field (per
	// GetFieldConstraints) from the free ones instead of validating caller input - always succeeds
	// for finite a/b/c > 0. Convention: a along +X, b in the XY plane, c completes the set from the
	// angles (same convention pymatgen/ASE use for Lattice.from_parameters).
	[[nodiscard]] LatticeCell BuildLatticeCell(CrystalSystem system, const LatticeParameters &params);

	// Starting-point atomic basis for the classic centering types, as fractional coordinates in
	// the CONVENTIONAL cell - a convenience preset the New Structure wizard inserts before the
	// user edits further, not a constraint BuildLatticeCell enforces. Species/labels are left to
	// the caller - this only returns positions.
	enum class BravaisCenteringPreset
	{
		Primitive,    // 1 point: (0,0,0)
		BodyCentered, // 2 points: + (0.5,0.5,0.5)
		FaceCentered, // 4 points: (0,0,0) + 3 face centers
		BaseCentered  // 2 points: (0,0,0) + (0.5,0.5,0)
	};
	[[nodiscard]] std::vector<glm::vec3> GetCenteringPresetBasis(BravaisCenteringPreset preset);
} // namespace DefectStudio
```

`src/Domain/Crystal/BravaisLattice.cpp` (kluczowa logika — pełna implementacja):
```cpp
#include "Core/dspch.hpp"

#include "Domain/Crystal/BravaisLattice.hpp"

namespace DefectStudio
{
	LatticeFieldConstraints GetFieldConstraints(CrystalSystem system)
	{
		LatticeFieldConstraints c;
		switch (system)
		{
			case CrystalSystem::Cubic:
				c.bLocked = c.cLocked = c.alphaLocked = c.betaLocked = c.gammaLocked = true;
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Tetragonal:
				c.bLocked = true; // b = a, c free
				c.alphaLocked = c.betaLocked = c.gammaLocked = true;
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Orthorhombic:
				c.alphaLocked = c.betaLocked = c.gammaLocked = true; // a, b, c all free
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Hexagonal:
				c.bLocked = true; // b = a, c free
				c.alphaLocked = c.betaLocked = true; // gamma stays 120, not user-editable
				c.gammaLocked = true;
				c.lockedAngleDegrees = 90.0f; // alpha/beta; gamma is handled specially in BuildLatticeCell
				break;
			case CrystalSystem::Trigonal:
				c.bLocked = c.cLocked = true; // a = b = c
				c.betaLocked = c.gammaLocked = true; // alpha = beta = gamma, alpha itself stays free
				break;
			case CrystalSystem::Monoclinic:
				c.alphaLocked = c.gammaLocked = true; // beta free
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Triclinic:
				break; // nothing locked
		}
		return c;
	}

	LatticeCell BuildLatticeCell(CrystalSystem system, const LatticeParameters &params)
	{
		float a = params.a;
		float b = params.b;
		float c = params.c;
		float alpha = glm::radians(params.alphaDegrees);
		float beta = glm::radians(params.betaDegrees);
		float gamma = glm::radians(params.gammaDegrees);

		switch (system)
		{
			case CrystalSystem::Cubic:
				b = c = a;
				alpha = beta = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Tetragonal:
				b = a;
				alpha = beta = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Orthorhombic:
				alpha = beta = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Hexagonal:
				b = a;
				alpha = beta = glm::radians(90.0f);
				gamma = glm::radians(120.0f);
				break;
			case CrystalSystem::Trigonal:
				b = c = a;
				beta = gamma = alpha;
				break;
			case CrystalSystem::Monoclinic:
				alpha = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Triclinic:
				break;
		}

		// Standard crystallographic convention (matches pymatgen Lattice.from_parameters / ASE
		// cellpar_to_cell): a along +X; b in the XY plane at angle gamma from a; c completes the
		// set so that the angle to a is beta and to b is alpha.
		LatticeCell cell;
		cell.vectors[0] = glm::vec3(a, 0.0f, 0.0f);
		cell.vectors[1] = glm::vec3(b * glm::cos(gamma), b * glm::sin(gamma), 0.0f);

		const float cx = c * glm::cos(beta);
		const float cy = c * (glm::cos(alpha) - glm::cos(beta) * glm::cos(gamma)) / glm::sin(gamma);
		const float czSquared = c * c - cx * cx - cy * cy;
		const float cz = czSquared > 0.0f ? glm::sqrt(czSquared) : 0.0f;
		cell.vectors[2] = glm::vec3(cx, cy, cz);

		return cell;
	}

	std::vector<glm::vec3> GetCenteringPresetBasis(BravaisCenteringPreset preset)
	{
		switch (preset)
		{
			case BravaisCenteringPreset::Primitive:
				return { glm::vec3(0.0f, 0.0f, 0.0f) };
			case BravaisCenteringPreset::BodyCentered:
				return { glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.5f, 0.5f) };
			case BravaisCenteringPreset::FaceCentered:
				return {
					glm::vec3(0.0f, 0.0f, 0.0f),
					glm::vec3(0.5f, 0.5f, 0.0f),
					glm::vec3(0.5f, 0.0f, 0.5f),
					glm::vec3(0.0f, 0.5f, 0.5f)};
			case BravaisCenteringPreset::BaseCentered:
				return { glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.5f, 0.0f) };
		}
		return { glm::vec3(0.0f, 0.0f, 0.0f) };
	}
} // namespace DefectStudio
```

- [ ] **Step 4: Regeneruj projekty, zbuduj, uruchom testy — potwierdź PASS**

Run: `cmd //c "scripts\Windows\GenerateProjects.bat"`, potem
`"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" build/generated/vs2022/DefectStudioTests.vcxproj //p:Configuration=Debug //p:Platform=x64 //m:4 //nr:false //v:minimal`,
potem `"./build/bin/Debug-windows-x86_64/DefectStudioTests/DefectStudioTests.exe" --gtest_filter=BravaisLatticeTests.*`
Expected: `[ PASSED ] 6 tests.`

- [ ] **Step 5: Commit**

```bash
git add src/Domain/Crystal/BravaisLattice.hpp src/Domain/Crystal/BravaisLattice.cpp tests/Domain/Crystal/BravaisLatticeTests.cpp
git commit -m "feat(crystal): add Bravais lattice cell builder for the 7 crystal systems"
```

---

### Task 2: `Supercell` — macierz transformacji → rozszerzona `CrystalStructure`

**Pliki:**
- Create: `src/Domain/Crystal/Supercell.hpp`
- Create: `src/Domain/Crystal/Supercell.cpp`
- Test: `tests/Domain/Crystal/SupercellTests.cpp`

**Interfejsy:**
- Consumes: `CrystalStructure`, `AtomSite`, `LatticeCell` (`CrystalPrimitives.hpp`/
  `CrystalStructure.hpp`, istniejące), `StructuredError`/`Result<T>` (`Core/Diagnostics/
  StructuredError.hpp`, istniejące).
- Produces: `struct SupercellMatrix` (z `Diagonal()`, `Determinant()`, `IsDiagonal()`),
  `BuildSupercell(const CrystalStructure&, const SupercellMatrix&) -> Result<CrystalStructure>`.
  Task 8 (Supercell Builder panel) i Task 3 (surface-orientation bridge) konsumują
  `SupercellMatrix`; Task 6/9 konsumują `BuildSupercell`.

- [ ] **Step 1: Napisz failing testy**

```cpp
#include <gtest/gtest.h>

#include "Domain/Crystal/Supercell.hpp"

namespace DefectStudio::Tests
{
	TEST(SupercellTests, DiagonalDoublingAlongAProducesTwiceTheAtomsAtCorrectPositions)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(2, 0, 0), glm::vec3(0, 2, 0), glm::vec3(0, 0, 2) };
		unitCell.atoms = { AtomSite{"C", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		const SupercellMatrix transform = SupercellMatrix::Diagonal(2, 1, 1);
		const Result<CrystalStructure> result = BuildSupercell(unitCell, transform);

		ASSERT_TRUE(result);
		EXPECT_EQ(result->atoms.size(), 2u);
		EXPECT_NEAR(glm::length(result->cell.vectors[0]), 4.0f, 1e-5f);

		bool foundOrigin = false, foundShifted = false;
		for (const AtomSite &atom : result->atoms)
		{
			if (glm::length(atom.position - glm::vec3(0, 0, 0)) < 1e-4f) foundOrigin = true;
			if (glm::length(atom.position - glm::vec3(2, 0, 0)) < 1e-4f) foundShifted = true;
		}
		EXPECT_TRUE(foundOrigin);
		EXPECT_TRUE(foundShifted);
	}

	TEST(SupercellTests, AtomCountMatchesDeterminantTimesUnitCellAtomsForShearMatrix)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
		unitCell.atoms = {
			AtomSite{"Si", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0},
			AtomSite{"Si", glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f), 1}};

		SupercellMatrix transform;
		transform.rows = { glm::ivec3(2, 1, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1) };
		ASSERT_EQ(transform.Determinant(), 2);

		const Result<CrystalStructure> result = BuildSupercell(unitCell, transform);
		ASSERT_TRUE(result);
		EXPECT_EQ(result->atoms.size(), 4u); // 2 basis atoms * determinant 2
		EXPECT_FALSE(transform.IsDiagonal());
	}

	TEST(SupercellTests, RejectsNonPositiveDeterminant)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
		unitCell.atoms = { AtomSite{"C", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		SupercellMatrix transform;
		transform.rows = { glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 0) }; // det = 0

		const Result<CrystalStructure> result = BuildSupercell(unitCell, transform);
		EXPECT_FALSE(result);
	}

	TEST(SupercellTests, DiagonalHelperIsDiagonalTrueOnlyForZeroOffDiagonalEntries)
	{
		EXPECT_TRUE(SupercellMatrix::Diagonal(3, 2, 1).IsDiagonal());
		SupercellMatrix sheared;
		sheared.rows = { glm::ivec3(2, 1, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1) };
		EXPECT_FALSE(sheared.IsDiagonal());
	}

	TEST(SupercellTests, GeneratedBondsAreEmptyCallerMustRegenerate)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(2, 0, 0), glm::vec3(0, 2, 0), glm::vec3(0, 0, 2) };
		unitCell.atoms = { AtomSite{"C", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };
		unitCell.bonds = { Bond{0, 0, 1.0f, BondOrigin::Auto, true, glm::ivec3(0)} }; // stale, must not carry over

		const Result<CrystalStructure> result = BuildSupercell(unitCell, SupercellMatrix::Diagonal(2, 1, 1));
		ASSERT_TRUE(result);
		EXPECT_TRUE(result->bonds.empty());
	}
} // namespace DefectStudio::Tests
```

- [ ] **Step 2: Uruchom testy, potwierdź fail**

Run: `"./build/bin/Debug-windows-x86_64/DefectStudioTests/DefectStudioTests.exe" --gtest_filter=SupercellTests.*`
Expected: fail (brak `Supercell.hpp`/`.cpp`) po dograniu pustych deklaracji do premake.

- [ ] **Step 3: Zaimplementuj**

`src/Domain/Crystal/Supercell.hpp`:
```cpp
#pragma once

#include <array>
#include <cstdlib>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	// Integer transformation matrix: newLattice.row[i] = sum_j rows[i][j] * unitCell.row[j] - same
	// row convention pymatgen's SupercellTransformation uses (rows={{2,1,0},{0,3,0},{0,0,1}} means
	// a" = 2a+b, b" = 3b, c" = c), so a UI power-user typing a matrix from a paper/pymatgen script
	// maps directly onto this field with no transpose surprises.
	struct SupercellMatrix
	{
		std::array<glm::ivec3, 3> rows = {
			glm::ivec3(1, 0, 0),
			glm::ivec3(0, 1, 0),
			glm::ivec3(0, 0, 1)};

		[[nodiscard]] static SupercellMatrix Diagonal(int a, int b, int c)
		{
			SupercellMatrix m;
			m.rows = { glm::ivec3(a, 0, 0), glm::ivec3(0, b, 0), glm::ivec3(0, 0, c) };
			return m;
		}

		// Also the resulting atom-count multiplier (BuildSupercell produces exactly
		// unitCell.atoms.size() * Determinant() atoms) - must be > 0 for BuildSupercell to accept it.
		[[nodiscard]] int Determinant() const
		{
			const glm::ivec3 &r0 = rows[0];
			const glm::ivec3 &r1 = rows[1];
			const glm::ivec3 &r2 = rows[2];
			return r0.x * (r1.y * r2.z - r1.z * r2.y)
				- r0.y * (r1.x * r2.z - r1.z * r2.x)
				+ r0.z * (r1.x * r2.y - r1.y * r2.x);
		}

		// Drives the UI: when true, the "Simple" N-x-M-x-K tab can show a faithful readout of this
		// matrix instead of falling back to "Matrix" mode.
		[[nodiscard]] bool IsDiagonal() const
		{
			return rows[0].y == 0 && rows[0].z == 0
				&& rows[1].x == 0 && rows[1].z == 0
				&& rows[2].x == 0 && rows[2].y == 0;
		}
	};

	// Pure, sync. Replicates unitCell.atoms across every lattice translation that lands inside the
	// transformed cell, dropping duplicate boundary images. Bonds are NOT carried over or
	// regenerated here (caller runs RegenerateAutoBonds on the result, same as every other
	// structure-building path in this repo - see OpenDefectJob's completion handler in
	// RendererRuntimeOpenCoordinator.cpp) - Supercell.cpp stays pure geometry, no
	// ElementPropertiesTable dependency.
	[[nodiscard]] Result<CrystalStructure> BuildSupercell(
		const CrystalStructure &unitCell,
		const SupercellMatrix &transform);
} // namespace DefectStudio
```

`src/Domain/Crystal/Supercell.cpp`:
```cpp
#include "Core/dspch.hpp"

#include "Domain/Crystal/Supercell.hpp"

#include <cmath>
#include <string>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakeInvalidSupercellMatrixError(int determinant)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"Supercell transform matrix must have a positive determinant.",
				"Determinant was " + std::to_string(determinant) + ".",
				"Pick a transform whose three row vectors form a right-handed, non-degenerate basis "
				"(e.g. swap two rows if the determinant is negative, or fix a zero row/column).",
				"Supercell",
				"domain.supercell.invalid_determinant"};
		}
	} // namespace

	Result<CrystalStructure> BuildSupercell(
		const CrystalStructure &unitCell,
		const SupercellMatrix &transform)
	{
		const int determinant = transform.Determinant();
		if (determinant <= 0)
			return MakeInvalidSupercellMatrixError(determinant);

		CrystalStructure result;
		result.name = unitCell.name + " supercell";
		result.isPeriodic = unitCell.isPeriodic;

		const std::array<glm::vec3, 3> &oldVectors = unitCell.cell.vectors;
		std::array<glm::vec3, 3> newVectors;
		for (int row = 0; row < 3; ++row)
		{
			newVectors[row] =
				static_cast<float>(transform.rows[row].x) * oldVectors[0] +
				static_cast<float>(transform.rows[row].y) * oldVectors[1] +
				static_cast<float>(transform.rows[row].z) * oldVectors[2];
		}
		result.cell.vectors = newVectors;

		// Conservative search bound: any lattice point that can end up inside the new cell has
		// integer coefficients (against the OLD basis) bounded by the sum of absolute entries of
		// the transform matrix. Looser than necessary but always correct and cheap for realistic
		// supercell sizes.
		// ponytail: O((2R+1)^3 * atomCount) brute-force search; R grows with how sheared the
		// transform is, not just its determinant. Tighten via the new lattice's inverse (bounding
		// box of the new cell's 8 corners in old-basis coordinates) if a pathologically sheared
		// matrix ever makes this slow in practice.
		int searchRadius = 1;
		for (const glm::ivec3 &row : transform.rows)
			searchRadius += std::abs(row.x) + std::abs(row.y) + std::abs(row.z);

		constexpr float kFractionalEpsilon = 1e-5f;
		result.atoms.reserve(unitCell.atoms.size() * static_cast<std::size_t>(determinant));

		int nextIndex = 0;
		for (int i = -searchRadius; i <= searchRadius; ++i)
		{
			for (int j = -searchRadius; j <= searchRadius; ++j)
			{
				for (int k = -searchRadius; k <= searchRadius; ++k)
				{
					const glm::vec3 shift =
						static_cast<float>(i) * oldVectors[0] +
						static_cast<float>(j) * oldVectors[1] +
						static_cast<float>(k) * oldVectors[2];

					for (const AtomSite &atom : unitCell.atoms)
					{
						const glm::vec3 cartesian = atom.position + shift;
						const glm::vec3 fractional = result.CartesianToFractional(cartesian);

						const bool inNewCell =
							fractional.x >= -kFractionalEpsilon && fractional.x < 1.0f - kFractionalEpsilon &&
							fractional.y >= -kFractionalEpsilon && fractional.y < 1.0f - kFractionalEpsilon &&
							fractional.z >= -kFractionalEpsilon && fractional.z < 1.0f - kFractionalEpsilon;
						if (!inNewCell)
							continue;

						AtomSite newAtom = atom;
						newAtom.position = cartesian;
						newAtom.fractional = fractional;
						newAtom.index = nextIndex++;
						result.atoms.push_back(std::move(newAtom));
					}
				}
			}
		}

		return result;
	}
} // namespace DefectStudio
```

- [ ] **Step 4: Regeneruj projekty, zbuduj, uruchom testy — potwierdź PASS**

Run jak w Task 1 Step 4, filtr `--gtest_filter=SupercellTests.*`.
Expected: `[ PASSED ] 5 tests.`

- [ ] **Step 5: Commit**

```bash
git add src/Domain/Crystal/Supercell.hpp src/Domain/Crystal/Supercell.cpp tests/Domain/Crystal/SupercellTests.cpp
git commit -m "feat(crystal): add pure-C++ supercell expansion from an integer transform matrix"
```

---

## Faza 2 — Python bridge: tylko tam, gdzie realnie potrzebny (ASE + spglib)

> Weryfikacja przed startem: `SupercellBridge` naśladuje dokładnie wzorzec `PymatgenBridge`
> (`ScriptRunner`, `ResolvePythonExampleScript`, `ExtractJsonLineFromOutput` z
> `ScriptBridgeUtils.hpp`) — otwórz `src/ScientificRuntime/Python/PymatgenBridge.cpp` obok siebie
> przy implementacji, nie zgadywać sygnatur.

### Task 3: `SupercellBridge::SuggestSurfaceOrientedMatrix` (ASE)

**Pliki:**
- Create: `src/ScientificRuntime/Python/SupercellBridge.hpp`
- Create: `src/ScientificRuntime/Python/SupercellBridge.cpp` (metoda ta + Task 4 razem, plik mały)
- Create: `scripts/python/examples/ase_surface_supercell.py`
- Test: `tests/ScientificRuntime/SupercellBridgeTests.cpp`

**Interfejsy:**
- Consumes: `SupercellMatrix` (Task 2), `ScriptRunner`/`ScriptRunOptions`/`ExtractJsonLineFromOutput`
  (istniejące, `ScriptBridgeUtils.hpp`), `ResolvePythonExampleScript` (istniejące).
- Produces: `struct MillerIndices`, `SupercellBridge::SuggestSurfaceOrientedMatrix(const
  CrystalStructure&, MillerIndices, int layers) const -> Result<SupercellMatrix>`. Task 8
  (Supercell Builder, zakładka Surface) konsumuje to przez `JobSystem` (subprocess = latency).

- [ ] **Step 1: Napisz failing test (integracyjny — realny subprocess, jak `PymatgenBridgeTests`)**

```cpp
#include <gtest/gtest.h>

#include "Domain/Crystal/BravaisLattice.hpp"
#include "ScientificRuntime/Python/SupercellBridge.hpp"

namespace DefectStudio::Tests
{
	TEST(SupercellBridgeTests, SuggestsIntegerMatrixForSimpleCubicSurface)
	{
		CrystalStructure unitCell;
		unitCell.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 3.0f});
		unitCell.atoms = { AtomSite{"Cu", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		SupercellBridge bridge;
		const Result<SupercellMatrix> result =
			bridge.SuggestSurfaceOrientedMatrix(unitCell, MillerIndices{0, 0, 1}, 2);

		if (!result)
		{
			GTEST_SKIP() << "ASE unavailable in current environment: " << result.Error().technicalDetails;
		}

		EXPECT_GT(result->Determinant(), 0);
	}
} // namespace DefectStudio::Tests
```

- [ ] **Step 2: Uruchom, potwierdź fail** (brak `SupercellBridge.hpp`/skryptu).

- [ ] **Step 3: Zaimplementuj**

`src/ScientificRuntime/Python/SupercellBridge.hpp`:
```cpp
#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct MillerIndices
	{
		int h = 1, k = 0, l = 0;
	};

	struct SymmetryInfo
	{
		int spacegroupNumber = 0;
		std::string spacegroupSymbol;
		std::string pointGroupSymbol;
		std::vector<std::string> wyckoffLetters; // one per atom, same order as CrystalStructure::atoms
	};

	// ASE/spglib-backed suggestions - both are read-only "tell me a matrix" / "tell me the
	// symmetry", never structure builders themselves. BuildSupercell (Domain/Crystal/Supercell.hpp)
	// is the only code path that actually replicates atoms - keeps exactly one implementation of
	// supercell expansion in the whole app instead of one in C++ and a second, subtly different one
	// in Python.
	class SupercellBridge final
	{
	public:
		[[nodiscard]] Result<SupercellMatrix> SuggestSurfaceOrientedMatrix(
			const CrystalStructure &unitCell,
			MillerIndices hkl,
			int layers) const;
		[[nodiscard]] Result<SymmetryInfo> GetSymmetryInfo(
			const CrystalStructure &structure,
			float symprecAngstrom) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
```

`src/ScientificRuntime/Python/SupercellBridge.cpp` (szkielet payloadu + wywołanie — ten sam
kontrakt co `PymatgenBridge.cpp`: temp JSON payload plik, `ScriptRunner::RunFile`,
`ExtractJsonLineFromOutput` + `nlohmann::json::parse`):
```cpp
#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/SupercellBridge.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "Core/Utils/FileSystem.hpp"
#include "Core/Utils/Uuid.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] Path MakeTempStructurePayloadPath()
		{
			return Path(FileSystem::TempDirectoryPath()) /
				("ds_supercell_bridge_" + ToString(GenerateUuid()) + ".json");
		}

		void WriteStructurePayload(const Path &payloadPath, const CrystalStructure &structure)
		{
			nlohmann::json lattice = nlohmann::json::array();
			for (const glm::vec3 &vector : structure.cell.vectors)
				lattice.push_back({vector.x, vector.y, vector.z});

			nlohmann::json sites = nlohmann::json::array();
			for (const AtomSite &atom : structure.atoms)
				sites.push_back({{"element", atom.species}, {"fractional", {atom.fractional.x, atom.fractional.y, atom.fractional.z}}});

			nlohmann::json payload = {{"lattice", lattice}, {"sites", sites}};
			std::ofstream file(payloadPath.String());
			file << payload.dump();
		}
	} // namespace

	Result<SupercellMatrix> SupercellBridge::SuggestSurfaceOrientedMatrix(
		const CrystalStructure &unitCell,
		MillerIndices hkl,
		int layers) const
	{
		const Path payloadPath = MakeTempStructurePayloadPath();
		WriteStructurePayload(payloadPath, unitCell);

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("ase_surface_supercell.py");
		options.scriptPath = script.scriptPath;
		options.arguments = {
			payloadPath.String(),
			std::to_string(hkl.h), std::to_string(hkl.k), std::to_string(hkl.l),
			std::to_string(layers)};
		options.workingDirectory = script.workingDirectory;

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"ASE surface-supercell suggestion returned no output.",
				"Expected a JSON matrix in stdout but received an empty payload.",
				"Verify scripts/python/examples/ase_surface_supercell.py output contract.",
				"python.ase.surface_supercell.empty_output");
		}

		SupercellMatrix matrix;
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			const auto &rows = payload.at("matrix");
			for (int row = 0; row < 3; ++row)
				matrix.rows[row] = glm::ivec3(rows[row][0].get<int>(), rows[row][1].get<int>(), rows[row][2].get<int>());
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"ASE surface-supercell output parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints exactly one JSON line with a 3x3 integer matrix.",
				"python.ase.surface_supercell.invalid_json");
		}
		return matrix;
	}
} // namespace DefectStudio
```

`scripts/python/examples/ase_surface_supercell.py`:
```python
from __future__ import annotations

import json
import sys

import numpy as np
from ase import Atoms
from ase.build import surface


def main() -> int:
    payload_path, h, k, l, layers = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
    with open(payload_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    bulk = Atoms(
        symbols=[site["element"] for site in payload["sites"]],
        scaled_positions=[site["fractional"] for site in payload["sites"]],
        cell=payload["lattice"],
        pbc=True,
    )
    slab = surface(bulk, (h, k, l), layers, periodic=True)

    # slab.cell = transform @ bulk.cell (row-vector convention) - recover the integer transform by
    # solving the linear system; surface() builds the slab from integer lattice combinations, so
    # the exact solution is integral within float noise, hence the round().
    transform = np.round(
        np.linalg.solve(np.array(payload["lattice"]).T, np.array(slab.cell).T).T
    ).astype(int)

    print(json.dumps({"matrix": transform.tolist()}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Zbuduj, uruchom test** — `--gtest_filter=SupercellBridgeTests.SuggestsIntegerMatrixForSimpleCubicSurface`.
Expected: `PASS` (ASE jest zainstalowane w `.venv` tego repo, zweryfikowane) albo `SKIPPED` z jasnym
powodem, jeśli środowisko Python inne niż deweloperskie.

- [ ] **Step 5: Commit**

```bash
git add src/ScientificRuntime/Python/SupercellBridge.hpp src/ScientificRuntime/Python/SupercellBridge.cpp scripts/python/examples/ase_surface_supercell.py tests/ScientificRuntime/SupercellBridgeTests.cpp
git commit -m "feat(bridge): suggest a surface-oriented supercell matrix via ASE"
```

---

### Task 4: `SupercellBridge::GetSymmetryInfo` (spglib)

**Pliki:**
- Modify: `src/ScientificRuntime/Python/SupercellBridge.cpp` (dopisz drugą metodę)
- Create: `scripts/python/examples/spglib_symmetry_info.py`
- Modify: `tests/ScientificRuntime/SupercellBridgeTests.cpp` (dopisz test)

**Interfejsy:**
- Produces: `SupercellBridge::GetSymmetryInfo(const CrystalStructure&, float symprecAngstrom) const
  -> Result<SymmetryInfo>` (deklaracja już w Task 3's `.hpp`). Task 7 (New Structure wizard)
  konsumuje to jako pomocniczy, nieblokujący sanity-check panel.

- [ ] **Step 1: Dopisz failing test**

```cpp
TEST(SupercellBridgeTests, ReportsSpacegroup225ForSimpleCubicCopper)
{
	CrystalStructure structure;
	structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 3.615f});
	// Cu is FCC (Fm-3m, #225) - use the FaceCentered preset basis from Task 1 directly.
	for (const glm::vec3 &fractional : GetCenteringPresetBasis(BravaisCenteringPreset::FaceCentered))
		structure.atoms.push_back(AtomSite{"Cu", structure.FractionalToCartesian(fractional), fractional, 0});

	SupercellBridge bridge;
	const Result<SymmetryInfo> result = bridge.GetSymmetryInfo(structure, 0.01f);
	if (!result)
		GTEST_SKIP() << "spglib unavailable in current environment: " << result.Error().technicalDetails;

	EXPECT_EQ(result->spacegroupNumber, 225);
	EXPECT_EQ(result->wyckoffLetters.size(), structure.atoms.size());
}
```

- [ ] **Step 2: Uruchom, potwierdź fail.**

- [ ] **Step 3: Zaimplementuj**

Dopisz do `SupercellBridge.cpp` (ten sam plik, ta sama `WriteStructurePayload` helper z Task 3):
```cpp
	Result<SymmetryInfo> SupercellBridge::GetSymmetryInfo(
		const CrystalStructure &structure,
		float symprecAngstrom) const
	{
		const Path payloadPath = MakeTempStructurePayloadPath();
		WriteStructurePayload(payloadPath, structure);

		ScriptRunOptions options;
		const PythonExampleScript script = ResolvePythonExampleScript("spglib_symmetry_info.py");
		options.scriptPath = script.scriptPath;
		options.arguments = { payloadPath.String(), std::to_string(symprecAngstrom) };
		options.workingDirectory = script.workingDirectory;

		Result<ScriptRunResult> runResult = m_ScriptRunner.RunFile(options);
		std::error_code removeError;
		FileSystem::Remove(payloadPath.Native(), removeError);
		if (!runResult)
			return runResult.Error();

		const std::string jsonLine = ExtractJsonLineFromOutput(runResult->standardOutput);
		if (jsonLine.empty())
		{
			return MakePythonExecutionError(
				"spglib symmetry info returned no output.",
				"Expected a JSON payload in stdout but received nothing.",
				"Verify scripts/python/examples/spglib_symmetry_info.py output contract.",
				"python.spglib.symmetry_info.empty_output");
		}

		SymmetryInfo info;
		try
		{
			const nlohmann::json payload = nlohmann::json::parse(jsonLine);
			info.spacegroupNumber = payload.at("spacegroup_number").get<int>();
			info.spacegroupSymbol = payload.at("spacegroup_symbol").get<std::string>();
			info.pointGroupSymbol = payload.at("point_group_symbol").get<std::string>();
			info.wyckoffLetters = payload.at("wyckoffs").get<std::vector<std::string>>();
		}
		catch (const std::exception &exception)
		{
			return MakePythonExecutionError(
				"spglib symmetry info parsing failed.",
				std::string("JSON parse error: ") + exception.what() + "\nPayload: " + jsonLine,
				"Ensure the bridge script prints spacegroup/point-group/wyckoffs as one JSON line.",
				"python.spglib.symmetry_info.invalid_json");
		}
		return info;
	}
```

`scripts/python/examples/spglib_symmetry_info.py`:
```python
from __future__ import annotations

import json
import sys

import spglib


def main() -> int:
    payload_path, symprec = sys.argv[1], float(sys.argv[2])
    with open(payload_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    lattice = payload["lattice"]
    positions = [site["fractional"] for site in payload["sites"]]
    elements = [site["element"] for site in payload["sites"]]
    unique = sorted(set(elements))
    numbers = [unique.index(e) for e in elements]

    dataset = spglib.get_symmetry_dataset((lattice, positions, numbers), symprec=symprec)
    print(json.dumps({
        "spacegroup_number": dataset.number,
        "spacegroup_symbol": dataset.international,
        "point_group_symbol": dataset.pointgroup,
        "wyckoffs": list(dataset.wyckoffs),
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Zbuduj, uruchom test** — `--gtest_filter=SupercellBridgeTests.ReportsSpacegroup225ForSimpleCubicCopper`. Expected: `PASS` lub `SKIPPED` z powodem.

- [ ] **Step 5: Commit**

```bash
git add src/ScientificRuntime/Python/SupercellBridge.cpp src/ScientificRuntime/Python/SupercellBridge.hpp scripts/python/examples/spglib_symmetry_info.py tests/ScientificRuntime/SupercellBridgeTests.cpp
git commit -m "feat(bridge): read spacegroup/point-group/Wyckoff info via spglib"
```

---

## Faza 3 — Materials Collection (project-scoped + user-level, `ase.db`)

### Task 5: `MaterialLibraryIO`

**Pliki:**
- Create: `src/IO/MaterialLibraryIO.hpp`
- Create: `src/IO/MaterialLibraryIO.cpp`
- Create: `scripts/python/examples/ase_material_library.py`
- Modify: `src/IO/ProjectManifestIO.hpp` / `.cpp` — dodaj pole (patrz niżej)
- Test: `tests/IO/MaterialLibraryIOTests.cpp`

**Interfejsy:**
- Consumes: `CrystalStructure`, `ScriptRunner` (istniejące).
- Produces: `struct MaterialLibraryEntry`,
  `MaterialLibraryIO(Path libraryPath)`,
  `AddMaterial(const CrystalStructure&, name, notes) const -> Result<MaterialLibraryEntry>`,
  `ListMaterials() const -> Result<std::vector<MaterialLibraryEntry>>`,
  `LoadMaterial(const std::string &entryId) const -> Result<CrystalStructure>`,
  `RemoveMaterial(const std::string &entryId) const -> Result<void>`.
  Task 9 (Materials Collection panel) konsumuje wszystkie cztery, raz dla scope "projekt" (path z
  `ProjectManifest::materialsLibraryPath`) i raz dla scope "moja biblioteka" (stały path
  użytkownika).

**Rozszerzenie manifestu projektu** (mirror istniejącego wzorca — `ProjectManifest` już ma
`displacementComparisonPath : Path`, `T07.5.1`, przechowywany relatywnie do katalogu projektu):
```cpp
// ProjectManifestIO.hpp, w ProjectManifest:
Path materialsLibraryPath; // "materials/materials.db" relative to the project dir when set; empty = not yet created
```
`CreateNew()` ustawia to na `"materials/materials.db"` (analogicznie do reszty domyślnych pól) —
plik `.db` sam nie jest tworzony przy `CreateNew()`, dopiero przy pierwszym `AddMaterial()`
(`ase.db`'s `connect()` tworzy plik leniwie, przy pierwszym `write()`).

- [ ] **Step 1: Napisz failing testy**

```cpp
#include <gtest/gtest.h>

#include "Core/Utils/FileSystem.hpp"
#include "Core/Utils/Time.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "IO/MaterialLibraryIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] Path MakeTempLibraryPath()
		{
			return Path::FromResolved(FileSystem::TempDirectoryPath()) /
				("defectstudio_material_library_" + std::to_string(Time::NowSteady().time_since_epoch().count()) + ".db");
		}
	}

	TEST(MaterialLibraryIOTests, AddListLoadRemoveRoundtrip)
	{
		CrystalStructure structure;
		structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 3.615f});
		structure.atoms = { AtomSite{"Cu", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		const Path libraryPath = MakeTempLibraryPath();
		MaterialLibraryIO library(libraryPath);

		const Result<MaterialLibraryEntry> added = library.AddMaterial(structure, "Copper bulk", "test entry");
		if (!added)
			GTEST_SKIP() << "ase unavailable in current environment: " << added.Error().technicalDetails;
		EXPECT_EQ(added->name, "Copper bulk");

		const Result<std::vector<MaterialLibraryEntry>> listed = library.ListMaterials();
		ASSERT_TRUE(listed);
		ASSERT_EQ(listed->size(), 1u);
		EXPECT_EQ((*listed)[0].id, added->id);

		const Result<CrystalStructure> loaded = library.LoadMaterial(added->id);
		ASSERT_TRUE(loaded);
		EXPECT_EQ(loaded->atoms.size(), 1u);
		EXPECT_EQ(loaded->atoms[0].species, "Cu");

		const Result<void> removed = library.RemoveMaterial(added->id);
		EXPECT_TRUE(removed);
		const Result<std::vector<MaterialLibraryEntry>> listedAfterRemove = library.ListMaterials();
		ASSERT_TRUE(listedAfterRemove);
		EXPECT_TRUE(listedAfterRemove->empty());

		std::error_code removeError;
		FileSystem::Remove(libraryPath.Native(), removeError);
	}
} // namespace DefectStudio::Tests
```

- [ ] **Step 2: Uruchom, potwierdź fail.**

- [ ] **Step 3: Zaimplementuj**

`src/IO/MaterialLibraryIO.hpp`:
```cpp
#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct MaterialLibraryEntry
	{
		std::string id; // ase.db row id, as string
		std::string name;
		std::string reducedFormula;
		std::string notes;
	};

	// Thin subprocess wrapper over ase.db (SQLite-backed) - one library file per scope (project vs
	// personal), same class for both, only libraryPath differs. No in-memory ProjectWorkspace
	// registry: panels call this directly, the way ProjectTreePanel talks to the filesystem
	// directly instead of through a cached Domain registry - this collection IS the persistence,
	// not a cache of it.
	class MaterialLibraryIO
	{
	public:
		explicit MaterialLibraryIO(Path libraryPath);

		[[nodiscard]] Result<MaterialLibraryEntry> AddMaterial(
			const CrystalStructure &structure,
			const std::string &name,
			const std::string &notes) const;
		[[nodiscard]] Result<std::vector<MaterialLibraryEntry>> ListMaterials() const;
		[[nodiscard]] Result<CrystalStructure> LoadMaterial(const std::string &entryId) const;
		[[nodiscard]] Result<void> RemoveMaterial(const std::string &entryId) const;

	private:
		Path m_LibraryPath;
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
```

`src/IO/MaterialLibraryIO.cpp` — cztery metody, każda: zapisz payload (dla Add) do temp JSON,
zawołaj `ase_material_library.py <db_path> <add|list|load|remove> [payload_or_id]` przez
`ScriptRunner::RunFile`, sparsuj jedną linię JSON (identyczny kontrakt jak `SupercellBridge`/
`PymatgenBridge` — implementować analogicznie, `WriteStructurePayload`-style helper dla `sites`+
`lattice`+`name`+`notes`).

`scripts/python/examples/ase_material_library.py`:
```python
from __future__ import annotations

import json
import sys

from ase import Atoms
from ase.db import connect


def cmd_add(db_path: str, payload_path: str) -> dict:
    with open(payload_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    atoms = Atoms(
        symbols=[site["element"] for site in payload["sites"]],
        scaled_positions=[site["fractional"] for site in payload["sites"]],
        cell=payload["lattice"],
        pbc=True,
    )
    db = connect(db_path)
    row_id = db.write(atoms, name=payload["name"], notes=payload.get("notes", ""))
    return {"id": str(row_id), "name": payload["name"], "reduced_formula": atoms.get_chemical_formula(), "notes": payload.get("notes", "")}


def cmd_list(db_path: str) -> dict:
    db = connect(db_path)
    entries = [
        {
            "id": str(row.id),
            "name": row.key_value_pairs.get("name", ""),
            "reduced_formula": row.formula,
            "notes": row.key_value_pairs.get("notes", ""),
        }
        for row in db.select()
    ]
    return {"entries": entries}


def cmd_load(db_path: str, entry_id: str) -> dict:
    db = connect(db_path)
    atoms = db.get(id=int(entry_id)).toatoms()
    return {
        "lattice": atoms.cell.tolist(),
        "sites": [
            {"element": symbol, "fractional": frac.tolist()}
            for symbol, frac in zip(atoms.get_chemical_symbols(), atoms.get_scaled_positions())
        ],
    }


def cmd_remove(db_path: str, entry_id: str) -> dict:
    db = connect(db_path)
    db.delete([int(entry_id)])
    return {"removed_id": entry_id}


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit("usage: ase_material_library.py <db_path> <add|list|load|remove> [arg]")

    db_path, operation = sys.argv[1], sys.argv[2]
    handlers = {"add": lambda: cmd_add(db_path, sys.argv[3]), "list": lambda: cmd_list(db_path),
                "load": lambda: cmd_load(db_path, sys.argv[3]), "remove": lambda: cmd_remove(db_path, sys.argv[3])}
    if operation not in handlers:
        raise SystemExit(f"unknown operation: {operation}")

    print(json.dumps(handlers[operation]()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

Dopisz w `ProjectManifestIO.hpp`/`.cpp`: pole `Path materialsLibraryPath;` w `ProjectManifest`
(patrz wyżej), `CreateNew()` ustawia `"materials/materials.db"`, `Load`/`Save` (yaml-cpp) — dopisz
klucz `materials_library_path` analogicznie do istniejącego `displacement_comparison_path`.

- [ ] **Step 4: Zbuduj, uruchom testy** — `--gtest_filter=MaterialLibraryIOTests.*`. Expected: `PASS`
lub `SKIPPED`.

- [ ] **Step 5: Commit**

```bash
git add src/IO/MaterialLibraryIO.hpp src/IO/MaterialLibraryIO.cpp scripts/python/examples/ase_material_library.py src/IO/ProjectManifestIO.hpp src/IO/ProjectManifestIO.cpp tests/IO/MaterialLibraryIOTests.cpp
git commit -m "feat(io): persist a materials collection via ase.db, project-scoped and user-level"
```

---

## Faza 4 — Otwieranie okien renderera dla struktur budowanych w appce

### Task 6: `OpenCrystalStructureAsWindow` — reużycie istniejącego pipeline'u

> Weryfikacja przed startem: dokładne sygnatury `BuildRendererStructureData`
> (`Renderer/StructureRendererDataBuilder.hpp`) i `BuildRendererStartupWindows`
> (`Renderer/RendererStartupBootstrap.hpp`) wywnioskowane tu z JEDNEGO call site
> (`RendererRuntimeOpenCoordinator.cpp:98-113`) — otworzyć oba nagłówki i potwierdzić parametry
> zanim napiszesz `.cpp`, nie kopiować na ślepo z tego planu.

**Pliki:**
- Create: `src/Renderer/OpenCrystalStructureAsWindow.hpp`
- Create: `src/Renderer/OpenCrystalStructureAsWindow.cpp`

**Interfejsy:**
- Consumes: `DomainLayer::Workspace().Structures().Add(...)`, `BuildRendererStructureData(...)`,
  `BuildRendererStartupWindows(...)`, `RendererLayer::AddWindow(...)`, `RegenerateAutoBonds(...)`
  (wszystko istniejące — dokładny wzorzec `RendererRuntimeOpenCoordinator::onJobCompleted`).
- Produces: `OpenCrystalStructureAsWindow(CrystalStructure, displayName, DomainLayer&,
  RendererLayer&, const ElementPropertiesTable&, const AtomStyleTable&, bool showCellBox = true,
  bool showGrid = true) -> void`. Task 7/8/9 (wszystkie trzy panele) konsumują to bezpośrednio, bez
  `JobSystem` — funkcja jest synchroniczna (`BuildLatticeCell`/`BuildSupercell` to czysty C++, brak
  subprocess), dokładnie jak `BondSettingsPanel::applySettings()` woła synchronicznie zamiast przez
  Job.

`src/Renderer/OpenCrystalStructureAsWindow.hpp`:
```cpp
#pragma once

#include <string>

#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class RendererLayer;
	struct AtomStyleTable;

	// Registers `structure` in the active project's StructureRegistry and opens it as a new
	// renderer window - the same three-step sequence RendererRuntimeOpenCoordinator::
	// onJobCompleted runs for file-imported structures (RegenerateAutoBonds -> Structures().Add ->
	// BuildRendererStructureData -> AddWindow), factored out so in-app-BUILT structures (New
	// Structure wizard, supercell generation, materials collection "Open") can reuse it without a
	// file-based OpenDefectJob. Synchronous - only wrap the CALLER's own structure-building step in
	// a Job if it needs one (e.g. SupercellBridge's surface-orientation suggestion); this function
	// itself never touches Python.
	void OpenCrystalStructureAsWindow(
		CrystalStructure structure,
		const std::string &displayName,
		DomainLayer &domainLayer,
		RendererLayer &rendererLayer,
		const ElementPropertiesTable &elementPropertiesTable,
		const AtomStyleTable &atomStyleTable,
		bool showCellBox = true,
		bool showGrid = true);
} // namespace DefectStudio
```

`src/Renderer/OpenCrystalStructureAsWindow.cpp` — ciało dokładnie wg
`RendererRuntimeOpenCoordinator::onJobCompleted` (linie 90-115 tego pliku), z trzema różnicami:
`sourcePath` puste (`Path{}`, struktura nie pochodzi z pliku), `elementPropertiesTable` jako
jawny parametr zamiast pola klasy, i ustawienie `window.showCellBox`/`window.showGrid` przed
`AddWindow` (pola już istnieją na `RendererWindowState`, `RendererWindowState.hpp:41-42`).

**Weryfikacja (brak GoogleTest — to jest integracja z żywym `RendererLayer`/`DomainLayer`, jak
reszta kodu w `Renderer/`, manualnie w appce):**
- [ ] Zbuduj Debug, uruchom `DefectStudio.exe`, z poziomu debugger/tymczasowego wywołania (albo
      Task 7's przycisku, jeśli gotowy wcześniej) potwierdź: nowe okno pojawia się w
      `SceneOutlinerPanel`, atomy renderują się, struktura widoczna w `ProjectWorkspace` (np. przez
      Object Properties panel).

- [ ] **Commit**

```bash
git add src/Renderer/OpenCrystalStructureAsWindow.hpp src/Renderer/OpenCrystalStructureAsWindow.cpp
git commit -m "feat(renderer): factor out in-memory structure -> new window opening"
```

---

## Faza 5 — UI

> Wzorzec dla wszystkich trzech paneli: `BondSettingsPanel` (`Presentation/Panels/
> BondSettingsPanel.hpp/.cpp`) — `IPanel` interface, `Render()`/`Clone()`, lokalny edit-buffer
> reseedowany gdy zmienia się kontekst (tu: aktywny projekt / wybrany materiał), auto-apply zamiast
> osobnego "Submit" przycisku gdzie to ma sens. Panele w tym repo nie mają dedykowanych testów
> GoogleTest (ImGui nie jest tak testowalny) — weryfikacja manualna w appce, checklist niżej.

### Task 7: `NewStructureWizardPanel`

**Pliki:**
- Create: `src/Presentation/Panels/NewStructureWizardPanel.hpp`
- Create: `src/Presentation/Panels/NewStructureWizardPanel.cpp`

**Interfejsy:**
- Consumes: `CrystalSystem`/`LatticeParameters`/`GetFieldConstraints`/`BuildLatticeCell`/
  `BravaisCenteringPreset`/`GetCenteringPresetBasis` (Task 1), `DrawPeriodicTableGrid` (istniejące,
  `PeriodicTableGrid.hpp`), `OpenCrystalStructureAsWindow` (Task 6), opcjonalnie
  `SupercellBridge::GetSymmetryInfo` (Task 4, przez `JobSystem` — jedyne miejsce w tym panelu, gdzie
  subprocess wchodzi w grę).
- Produces: gotowa `CrystalStructure` (nazwa + `LatticeCell` + baza atomowa) przekazywana do
  `OpenCrystalStructureAsWindow` z `showCellBox=true` — to jest "unit cell preview" okno z sekcji
  "Podgląd na żywo" wyżej. Task 9 (Materials Collection) może zainicjować ten panel z materiału z
  kolekcji zamiast pustej bazy (edit-then-save-as-new-entry flow).

**Zawartość (`Render()`):**
- Dropdown `CrystalSystem` (7 opcji) → na zmianę woła `GetFieldConstraints`, disable/gray-out
  zablokowanych pól a/b/c/α/β/γ (lock target field = wartość drugiego wolnego pola, live).
  4 sliders/inputs a/b/c + 3 kąty, jak wyżej.
- Rząd przycisków presetu centrowania (P/I/F/C, tylko gdy `system` je wspiera — Cubic ma
  wszystkie 4, Hexagonal/Trigonal tylko P) → klik woła `GetCenteringPresetBasis`, wstawia N wierszy
  do tabeli bazy atomowej z placeholder elementem "X" (user zmienia przez picker).
  Ta różnica jest specific do systemu: dopisać `IsPresetSupportedFor(CrystalSystem,
  BravaisCenteringPreset) -> bool` jako mały helper w `BravaisLattice.hpp` z Task 1, jeśli okaże
  się potrzebny przy realnej implementacji (nie zakładać z góry, sprawdzić czy UI faktycznie tego
  wymaga).
- Tabela bazy atomowej: wiersz = element (przycisk otwierający `DrawPeriodicTableGrid` w popupie) +
  3 float inputy (fractional x/y/z) + usuń-wiersz. "+" dodaje pusty wiersz.
- Live licznik atomów + wzór sumaryczny (`CrystalStructure::UniqueSpecies()`, istniejące) pod
  tabelą.
- Przycisk "Pokaż symetrię" (opcjonalny, nieblokujący) → submituje `SupercellBridge::
  GetSymmetryInfo` przez `JobSystem`, wynik w małym read-only panelu obok (spacegroup/point
  group/Wyckoff per atom) — to jest sanity-check z sekcji "Info o symetrii" wyżej, nie
  wymagany do dalszych kroków.
- Przycisk "Create" → buduje `CrystalStructure` z aktualnego stanu, woła
  `OpenCrystalStructureAsWindow(..., showCellBox=true, showGrid=true)` — to jest okno "komórka
  elementarna" z podglądu. Osobny przycisk "Preview basis only" (opcjonalny) otwiera to samo z
  `showCellBox=false, showGrid=false` — okno "baza atomowa".

**Weryfikacja manualna:**
- [ ] Wybierz Cubic, wpisz a=3.0, potwierdź że b/c/kąty grayed-out i pokazują 3.0/90°/90°/90°.
- [ ] Przełącz na Hexagonal, potwierdź gamma pokazuje 120° (nie edytowalne).
- [ ] Klik "Cubic F" preset (po wybraniu Cubic) → 4 wiersze bazy pojawiają się z poprawnymi
      współrzędnymi (0,0,0)/(0.5,0.5,0)/(0.5,0,0.5)/(0,0.5,0.5).
- [ ] Zmień element pierwszego wiersza na Cu przez periodic table popup.
- [ ] Klik "Create" → nowe okno w `SceneOutlinerPanel`, 4 atomy Cu widoczne, cell box widoczny.
- [ ] "Pokaż symetrię" na tej strukturze → spacegroup 225 (Fm-3m).

- [ ] **Commit**

```bash
git add src/Presentation/Panels/NewStructureWizardPanel.hpp src/Presentation/Panels/NewStructureWizardPanel.cpp
git commit -m "feat(ui): add New Structure wizard (crystal system + atomic basis)"
```

---

### Task 8: `SupercellBuilderPanel` — smart input

**Pliki:**
- Create: `src/Presentation/Panels/SupercellBuilderPanel.hpp`
- Create: `src/Presentation/Panels/SupercellBuilderPanel.cpp`

**Interfejsy:**
- Consumes: `SupercellMatrix`/`BuildSupercell` (Task 2), `SupercellBridge::
  SuggestSurfaceOrientedMatrix` (Task 3, przez `JobSystem`), `OpenCrystalStructureAsWindow`
  (Task 6). Wejściowa `CrystalStructure` (unit cell) przychodzi z aktywnego okna (focused
  viewport, jak `BondSettingsPanel` czyta `RendererLayer::GetFocusedViewportWindowId()`) albo z
  Materials Collection (Task 9, "Generate supercell from this material").
- Produces: nowe okno renderera z `showCellBox=true` (superkomórka) obok źródłowego okna komórki
  elementarnej — oba widoczne naraz, spełnia "podgląd na żywo w wielu oknach" z sekcji kontekstu.

**Zawartość (`Render()`):**
- 3 zakładki (`ImGui::BeginTabBar`) piszące do jednego `SupercellMatrix m_EditedMatrix` (członek
  panelu, reseedowany na `Diagonal(1,1,1)` gdy zmienia się źródłowe okno):
  - **Simple**: 3 int spinnery, widoczne tylko treściwie gdy `m_EditedMatrix.IsDiagonal()` (inaczej
    komunikat "macierz nie jest diagonalna, edytuj w zakładce Matrix" + przycisk "Reset to
    diagonal").
  - **Matrix**: 9 pól `ImGui::InputInt` w siatce 3×3, zapis wprost do `m_EditedMatrix.rows`.
  - **Surface**: 3 pola int (h,k,l) + 1 int (warstwy) + przycisk "Suggest" → submituje
    `SupercellBridge::SuggestSurfaceOrientedMatrix` przez `JobSystem` (subprocess = latency,
    nie blokować UI-thread), wynik nadpisuje `m_EditedMatrix` po powrocie joba.
- Pod zakładkami, zawsze widoczne: `unitCell.atoms.size() * m_EditedMatrix.Determinant()` jako
  duży, czytelny licznik ("Resulting atom count: N") — live, przelicza się na każdą zmianę pola,
  zero opóźnienia (to czysta arytmetyka, `Determinant()` jest tani).
- Przycisk "Generate" (disabled gdy `Determinant() <= 0`) → woła `BuildSupercell` synchronicznie
  (czysty C++, brak subprocess), na sukces `RegenerateAutoBonds` + `OpenCrystalStructureAsWindow`;
  na `StructuredError` (determinant ≤ 0) pokazuje komunikat z `error.userMessage` inline, nie
  crashuje/nie ignoruje po cichu.

**Weryfikacja manualna:**
- [ ] Otwórz strukturę Cubic a=3.0 z jednym atomem (z Task 7). Otwórz Supercell Builder.
- [ ] Zakładka Simple: 2×2×2 → licznik pokazuje 8. Klik Generate → nowe okno, 8 atomów.
- [ ] Zakładka Matrix: wpisz shear (2,1,0)/(0,1,0)/(0,0,1) → licznik pokazuje 2 (determinant=2,
      1 atom bazowy). Generate → nowe okno, 2 atomy w poprawnych pozycjach.
- [ ] Zakładka Surface: hkl=(0,0,1), 2 warstwy, "Suggest" → macierz wypełnia się po odpowiedzi
      Joba (brak zamrożenia UI w trakcie), przełącz na Matrix, potwierdź że pokazuje tę samą
      macierz.
- [ ] Wpisz macierz z determinant=0 w Matrix → Generate disabled, komunikat widoczny.

- [ ] **Commit**

```bash
git add src/Presentation/Panels/SupercellBuilderPanel.hpp src/Presentation/Panels/SupercellBuilderPanel.cpp
git commit -m "feat(ui): add supercell builder panel with simple/matrix/surface input modes"
```

---

### Task 9: `MaterialsCollectionPanel`

**Pliki:**
- Create: `src/Presentation/Panels/MaterialsCollectionPanel.hpp`
- Create: `src/Presentation/Panels/MaterialsCollectionPanel.cpp`

**Interfejsy:**
- Consumes: `MaterialLibraryIO` (Task 5, dwie instancje: projekt i user), `ProjectManifest::
  materialsLibraryPath` (Task 5), `OpenCrystalStructureAsWindow` (Task 6).
- Produces: wybrany/wczytany materiał otwarty jako okno (dla dalszej edycji w Task 7/8), oraz
  "Save current structure to library" akcja z dowolnego otwartego okna (czyta focused window's
  `CrystalStructure`, woła `AddMaterial`).

**Zawartość (`Render()`):**
- 2 zakładki: "This Project" (`MaterialLibraryIO` na `ProjectManifest::materialsLibraryPath`,
  disabled/komunikat gdy brak aktywnego projektu) i "My Library" (`MaterialLibraryIO` na stałym
  path użytkownika, np. `install/users/default/materials/materials.db` — zdecydować dokładny path
  przy starcie taska, konsystentnie z `install/users/default/config/` dla configów, ale to dane nie
  config, więc osobny katalog `materials/`, nie `config/`).
- W każdej zakładce: tabela `ListMaterials()` (nazwa, wzór, notatki) + przyciski per-wiersz "Open"
  (→ `LoadMaterial` + `OpenCrystalStructureAsWindow`) i "Delete" (→ `RemoveMaterial`, confirm popup
  jak istniejący `renderDeleteConfirmPopup` w `ProjectTreePanel` — reużyć wzorzec, nie pisać nowego
  modala od zera).
- Przycisk "Save current structure..." (górna belka panelu) → popup: nazwa + notatki + wybór scope
  (This Project / My Library) → czyta `CrystalStructure` z `RendererLayer::
  GetFocusedViewportWindowId()`'s okna, woła `AddMaterial` na wybranej instancji
  `MaterialLibraryIO`.

**Weryfikacja manualna:**
- [ ] Brak aktywnego projektu → "This Project" tab pokazuje komunikat, nie crashuje.
- [ ] Otwórz projekt, zbuduj strukturę (Task 7), "Save current structure..." → This Project → nazwa
      "Test Cu" → potwierdź wiersz pojawia się w tabeli.
- [ ] "Open" na tym wierszu → nowe okno z tą samą strukturą (atom count/species się zgadzają).
- [ ] "Delete" → confirm popup → wiersz znika, plik `materials/materials.db` w katalogu projektu
      istnieje na dysku.
- [ ] Zapisz coś do "My Library", zamknij projekt, otwórz INNY projekt → "My Library" tab dalej
      pokazuje ten wpis (cross-project), "This Project" tab jest pusty dla nowego projektu.

- [ ] **Commit**

```bash
git add src/Presentation/Panels/MaterialsCollectionPanel.hpp src/Presentation/Panels/MaterialsCollectionPanel.cpp
git commit -m "feat(ui): add Materials Collection panel (project-scoped + personal library)"
```

---

## Faza 6 — Rejestracja paneli, integracja końcowa

### Task 10: Wpięcie do `EditorLayer` + końcowa weryfikacja

**Pliki:**
- Modify: `src/Presentation/EditorLayer.cpp`/`.hpp` — rejestracja trzech nowych paneli (wzorzec
  `BondSettingsPanel`'a rejestracji, `visibleByDefault=false`, toolbar/menu entry analogicznie do
  istniejących paneli).
- Modify: `install/users/default/config/panel_visibility.txt` — jeśli plik enumeruje panele po
  nazwie (sprawdzić format przy starcie taska), dopisać 3 nowe wpisy `false` (spójnie z
  `visibleByDefault=false` powyżej).

- [ ] Zarejestruj `NewStructureWizardPanel`, `SupercellBuilderPanel`, `MaterialsCollectionPanel` w
      `EditorLayer::setupPanels()` (albo analogiczna metoda — sprawdzić dokładną nazwę), z menu
      entry w tym samym miejscu co `BondSettingsPanel`/`ElementCatalogPanel`.
- [ ] Pełen manualny E2E: New Structure (Cubic F, Cu) → Save to Materials Collection → zamknij
      okno → Materials Collection → Open → Supercell Builder (2×2×2) → Generate → 3 okna widoczne
      naraz w `SceneOutlinerPanel` (basis/cell/supercell nie wszystkie muszą być otwarte
      jednocześnie z definicji, ale przepływ całości działa bez crashy/zawieszeń).
- [ ] Uruchom `full-build-verify` skill w całości (regenerate → 4× build → 2× testy) — zero
      regresji względem stanu przed Fazą 1 (259 testów + nowe z Faz 1-3, wszystkie `PASSED`).

- [ ] **Commit**

```bash
git add src/Presentation/EditorLayer.cpp src/Presentation/EditorLayer.hpp install/users/default/config/panel_visibility.txt
git commit -m "feat(ui): wire supercell-generation panels into EditorLayer"
```

---

## Plan testowania — podsumowanie

**Warstwa 1 — Domain (GoogleTest, szybkie, bez subprocess):** `BravaisLatticeTests` (7 testów: 3
układy krystalograficzne + field constraints + preset), `SupercellTests` (5 testów: diagonalna
ekspansja, shear matrix, odrzucenie determinant≤0, `IsDiagonal`, bonds nie są kopiowane). Razem
~12 nowych testów, uruchamiane w Debug i Release (jak cała reszta suity — `full-build-verify`).

**Warstwa 2 — Bridge/IO integracyjne (GoogleTest + realny subprocess, `GTEST_SKIP()` gdy
środowisko Python niedostępne, wzorzec `PymatgenBridgeTests`):** `SupercellBridgeTests` (2 testy:
surface suggestion, symmetry info), `MaterialLibraryIOTests` (1 test: pełny roundtrip add/list/
load/remove). ASE 3.28.0/spglib 2.7.0 zweryfikowane zainstalowane w `.venv` tego repo — te testy
powinny realnie `PASS`, nie tylko `SKIP`, na tej maszynie.

**Warstwa 3 — UI manualna (appka, brak GoogleTest dla ImGui w tym repo — zgodne z istniejącą
konwencją):** checklisty per-task w Fazie 5 + pełny E2E w Task 10. Kluczowe scenariusze:
- Bravais constraints faktycznie blokują pola w UI (nie tylko w Domain logice).
- Live atom-count aktualizuje się na KAŻDĄ zmianę pola, przed zatwierdzeniem.
- Surface-orientation "Suggest" nie blokuje UI-thread (JobSystem).
- Materials Collection: project-scope vs user-scope faktycznie izolowane (test z dwoma projektami).
- Multi-window: co najmniej 2 okna (cell + supercell) otwarte jednocześnie, oba interaktywne.

**Bramka przed merge:** `full-build-verify` skill w całości — Debug+Release, oba exe, testy
zielone w obu configach, liczba testów zgodna między configami (dziś 261 total / 259 passed / 2
znane skip + ~15 nowych z tego planu, jeśli środowisko Python ma ASE/spglib zainstalowane — czyli
realnie `PASSED`, nie `SKIPPED`, dla nowych testów Warstwy 2 na tej maszynie).

**Świadomie NIE testowane w tym tasku** (poza zakresem — patrz "Kontekst i zakres funkcjonalny"):
convergence-test batch generation, defect generation (`doped`), Calculation Profiles, external
database search (MP/COD).
