#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererStructureData MakeStructure()
		{
			RendererStructureData structure;
			RendererAtomData carbon;
			carbon.element = "C";
			carbon.cartesianPosition = glm::vec3(0.0f);
			RendererAtomData oxygen;
			oxygen.element = "O";
			oxygen.cartesianPosition = glm::vec3(1.2f, 0.0f, 0.0f);
			RendererAtomData hydrogen;
			hydrogen.element = "H";
			hydrogen.cartesianPosition = glm::vec3(0.0f, 2.0f, 0.0f);
			structure.atoms = {carbon, oxygen, hydrogen};
			return structure;
		}

		[[nodiscard]] RendererWindowState::SceneOrbital MakeOrbital(OrbitalPreset preset)
		{
			RendererWindowState::SceneOrbital orbital;
			orbital.preset = preset;
			orbital.resolution = 24; // coarse - these tests check structure, not smoothness
			return orbital;
		}

		[[nodiscard]] glm::vec3 MeshCentroid(const std::vector<IsosurfaceVertex> &vertices)
		{
			glm::vec3 sum(0.0f);
			for (const IsosurfaceVertex &vertex : vertices)
				sum += vertex.position;
			return sum / static_cast<float>(vertices.size());
		}

		[[nodiscard]] float MeshRadius(const std::vector<IsosurfaceVertex> &vertices, const glm::vec3 &centre)
		{
			float radius = 0.0f;
			for (const IsosurfaceVertex &vertex : vertices)
				radius = std::max(radius, glm::length(vertex.position - centre));
			return radius;
		}
	} // namespace

	// --- wavefunction assembly ----------------------------------------------------------------

	TEST(SceneOrbitalGeometryTests, UnanchoredOrbitalUsesItsOwnCentres)
	{
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::Sigma);
		orbital.centerA = glm::vec3(5.0f, 0.0f, 0.0f);
		orbital.centerB = glm::vec3(5.0f, 0.0f, 2.0f);

		const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, MakeStructure());
		EXPECT_EQ(centers.centerA, orbital.centerA);
		EXPECT_EQ(centers.centerB, orbital.centerB);
		EXPECT_EQ(centers.centroid, glm::vec3(5.0f, 0.0f, 1.0f));
	}

	TEST(SceneOrbitalGeometryTests, OneAnchorDrivesTheSingleCentre)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::Sp3);
		orbital.centerA = glm::vec3(99.0f);
		orbital.anchorAtoms = {2}; // the hydrogen at (0, 2, 0)

		const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, structure);
		EXPECT_EQ(centers.centerA, glm::vec3(0.0f, 2.0f, 0.0f));
		// A single-centre preset pivots on its one centre.
		EXPECT_EQ(centers.centroid, glm::vec3(0.0f, 2.0f, 0.0f));
	}

	TEST(SceneOrbitalGeometryTests, TwoAnchorsDriveBothCentres)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::Pi);
		orbital.anchorAtoms = {0, 1};

		const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, structure);
		EXPECT_EQ(centers.centerA, glm::vec3(0.0f));
		EXPECT_EQ(centers.centerB, glm::vec3(1.2f, 0.0f, 0.0f));
		EXPECT_EQ(centers.centroid, glm::vec3(0.6f, 0.0f, 0.0f));

		// And the built wavefunction actually sits on those atoms.
		const OrbitalWavefunction wavefunction = BuildOrbitalWavefunction(orbital, structure);
		ASSERT_EQ(wavefunction.terms.size(), 2u);
		EXPECT_EQ(wavefunction.terms[0].center, glm::vec3(0.0f));
		EXPECT_EQ(wavefunction.terms[1].center, glm::vec3(1.2f, 0.0f, 0.0f));
	}

	TEST(SceneOrbitalGeometryTests, StaleAnchorFallsBackToTheStoredCentre)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::P);
		orbital.centerA = glm::vec3(7.0f, 8.0f, 9.0f);
		orbital.anchorAtoms = {17}; // no such atom

		// Not clamped to atom 0 - an orbital whose atom went away stays where the user left it.
		EXPECT_EQ(ResolveSceneOrbitalCenters(orbital, structure).centerA, glm::vec3(7.0f, 8.0f, 9.0f));
	}

	TEST(SceneOrbitalGeometryTests, RotationAppliesToSingleCentrePresetsOnly)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital single = MakeOrbital(OrbitalPreset::P);
		single.rotationEuler = glm::vec3(0.0f, 90.0f, 0.0f);
		const OrbitalWavefunction rotated = BuildOrbitalWavefunction(single, structure);
		ASSERT_FALSE(rotated.terms.empty());
		// p_z turned onto +x by a +90 degree rotation about y.
		EXPECT_GT(EvaluateOrbital(rotated, glm::vec3(2.0f, 0.0f, 0.0f)), 0.0f);
		EXPECT_NEAR(EvaluateOrbital(rotated, glm::vec3(0.0f, 0.0f, 2.0f)), 0.0f, 1e-6f);

		// A two-centre preset takes its axis from the centres, so the euler angles are ignored -
		// otherwise a sigma bond could be rotated off the bond it belongs to.
		RendererWindowState::SceneOrbital two = MakeOrbital(OrbitalPreset::Pi);
		two.centerA = glm::vec3(0.0f);
		two.centerB = glm::vec3(0.0f, 0.0f, 1.5f);
		RendererWindowState::SceneOrbital twoRotated = two;
		twoRotated.rotationEuler = glm::vec3(37.0f, -12.0f, 61.0f);

		const OrbitalWavefunction plain = BuildOrbitalWavefunction(two, structure);
		const OrbitalWavefunction spun = BuildOrbitalWavefunction(twoRotated, structure);
		for (const glm::vec3 probe : {glm::vec3(0.6f, 0.0f, 0.75f), glm::vec3(0.0f, 0.6f, 0.75f)})
			EXPECT_NEAR(EvaluateOrbital(spun, probe), EvaluateOrbital(plain, probe), 1e-6f);
	}

	// Regression: the render frame used to compose rotationEuler as Rx*Ry*Rz by hand, a different
	// Euler convention than glm::quat(vec3)/glm::eulerAngles - the pair RotatedEulerDegrees (the
	// transform gizmo's rotate handler) round-trips a rotate-delta through. A drag on the Z handle
	// wrote a rotationEuler in glm::quat's convention that this mismatch then misread, so it looked
	// like Z rotation "did nothing" while X/Y (closer to that convention already) mostly worked.
	TEST(SceneOrbitalGeometryTests, RotationFrameUsesTheSameEulerConventionOnEveryAxisIncludingZ)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::P);
		// All three axes nonzero, including a 90-degree Z component - would not have been
		// distinguishable from a Y-only rotation under the old Rx*Ry*Rz composition if Z were the
		// one silently dropped.
		orbital.rotationEuler = glm::vec3(20.0f, -35.0f, 90.0f);
		const OrbitalWavefunction rotated = BuildOrbitalWavefunction(orbital, structure);
		ASSERT_FALSE(rotated.terms.empty());

		// p_z's positive lobe sits on local +z. GLM's own quat-from-Euler convention is the
		// contract RotationFrame must match (not a hand-derived direction) - computed independently
		// here via a direct glm call, not by calling the function under test.
		const glm::vec3 expectedLobeDirection =
			glm::mat3_cast(glm::quat(glm::radians(orbital.rotationEuler))) * glm::vec3(0.0f, 0.0f, 1.0f);
		EXPECT_GT(EvaluateOrbital(rotated, expectedLobeDirection * 2.0f), 0.0f);
		EXPECT_LT(EvaluateOrbital(rotated, -expectedLobeDirection * 2.0f), 0.0f);
	}

	// --- meshing -----------------------------------------------------------------------------

	TEST(SceneOrbitalGeometryTests, MeshIsTriangleTriplesWithBothPhases)
	{
		const std::vector<IsosurfaceVertex> mesh =
			BuildSceneOrbitalMesh(MakeOrbital(OrbitalPreset::P), MakeStructure());

		ASSERT_FALSE(mesh.empty());
		EXPECT_EQ(mesh.size() % 3, 0u);
		// A p orbital has one positive and one negative lobe; both must reach the renderer.
		const bool hasPositive =
			std::any_of(mesh.begin(), mesh.end(), [](const IsosurfaceVertex &v) { return v.sign > 0.0f; });
		const bool hasNegative =
			std::any_of(mesh.begin(), mesh.end(), [](const IsosurfaceVertex &v) { return v.sign < 0.0f; });
		EXPECT_TRUE(hasPositive);
		EXPECT_TRUE(hasNegative);
	}

	TEST(SceneOrbitalGeometryTests, MeshSitsOnTheOrbitalCentreNotTheSceneOrigin)
	{
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::S);
		orbital.centerA = glm::vec3(10.0f, -4.0f, 3.0f);
		const std::vector<IsosurfaceVertex> mesh = BuildSceneOrbitalMesh(orbital, MakeStructure());

		ASSERT_FALSE(mesh.empty());
		// An s orbital is a sphere around its centre, so its mesh centroid is that centre.
		EXPECT_LT(glm::length(MeshCentroid(mesh) - orbital.centerA), 0.2f);
	}

	TEST(SceneOrbitalGeometryTests, ScaleGrowsTheMeshAboutItsCentroidWithoutMovingIt)
	{
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::S);
		orbital.centerA = glm::vec3(2.0f, 0.0f, 0.0f);
		const std::vector<IsosurfaceVertex> plain = BuildSceneOrbitalMesh(orbital, MakeStructure());
		ASSERT_FALSE(plain.empty());

		orbital.scale = 2.0f;
		const std::vector<IsosurfaceVertex> scaled = BuildSceneOrbitalMesh(orbital, MakeStructure());
		ASSERT_EQ(scaled.size(), plain.size());

		// Same centre, twice the reach - a figure knob, not a change of physics.
		EXPECT_LT(glm::length(MeshCentroid(scaled) - MeshCentroid(plain)), 1e-3f);
		const float plainRadius = MeshRadius(plain, orbital.centerA);
		ASSERT_GT(plainRadius, 0.0f);
		EXPECT_NEAR(MeshRadius(scaled, orbital.centerA) / plainRadius, 2.0f, 0.02f);
	}

	TEST(SceneOrbitalGeometryTests, AntibondingPresetMeshesIntoTwoSeparatedGroups)
	{
		// sigma* has its node between the nuclei, so no vertex should land on the midpoint - the
		// visible difference between a bonding and an antibonding picture.
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::SigmaStar);
		orbital.centerA = glm::vec3(0.0f);
		orbital.centerB = glm::vec3(0.0f, 0.0f, 1.5f);
		const std::vector<IsosurfaceVertex> mesh = BuildSceneOrbitalMesh(orbital, MakeStructure());

		ASSERT_FALSE(mesh.empty());
		const glm::vec3 midpoint(0.0f, 0.0f, 0.75f);
		const float nearest = std::transform_reduce(
			mesh.begin(), mesh.end(), std::numeric_limits<float>::max(),
			[](float a, float b) { return std::min(a, b); },
			[&](const IsosurfaceVertex &v) { return glm::length(v.position - midpoint); });
		EXPECT_GT(nearest, 0.05f);
	}

	TEST(SceneOrbitalGeometryTests, DegenerateSettingsMeshToNothingRatherThanCrashing)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital tooCoarse = MakeOrbital(OrbitalPreset::P);
		tooCoarse.resolution = 1;
		EXPECT_TRUE(BuildSceneOrbitalMesh(tooCoarse, structure).empty());

		RendererWindowState::SceneOrbital noIso = MakeOrbital(OrbitalPreset::P);
		noIso.isoFraction = 0.0f;
		EXPECT_TRUE(BuildSceneOrbitalMesh(noIso, structure).empty());

		RendererWindowState::SceneOrbital unreachableIso = MakeOrbital(OrbitalPreset::P);
		unreachableIso.isoFraction = 1.5f; // above the grid's own peak
		EXPECT_TRUE(BuildSceneOrbitalMesh(unreachableIso, structure).empty());
	}

	// --- mesh cache key -----------------------------------------------------------------------

	TEST(SceneOrbitalMeshKeyTests, ShapeParametersChangeTheKey)
	{
		const RendererStructureData structure = MakeStructure();
		const RendererWindowState::SceneOrbital base = MakeOrbital(OrbitalPreset::P);
		const SceneOrbitalMeshKey baseKey = MakeSceneOrbitalMeshKey(base, structure);
		EXPECT_EQ(MakeSceneOrbitalMeshKey(base, structure), baseKey) << "key must be stable";

		const auto changed = [&](auto mutate) {
			RendererWindowState::SceneOrbital orbital = base;
			mutate(orbital);
			return MakeSceneOrbitalMeshKey(orbital, structure);
		};
		EXPECT_NE(changed([](auto &o) { o.preset = OrbitalPreset::Sp3; }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.shell = 3; }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.lobeIndex = 1; }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.effectiveCharge = 2.5f; }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.centerA = glm::vec3(1.0f); }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.centerB = glm::vec3(1.0f); }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.rotationEuler = glm::vec3(0.0f, 5.0f, 0.0f); }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.scale = 1.5f; }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.isoFraction = 0.3f; }), baseKey);
		EXPECT_NE(changed([](auto &o) { o.resolution = 32; }), baseKey);
	}

	TEST(SceneOrbitalMeshKeyTests, AppearanceParametersDoNotChangeTheKey)
	{
		// Colours, alpha and visibility are shader uniforms - changing one must not throw away a
		// mesh that took tens of milliseconds to bake.
		const RendererStructureData structure = MakeStructure();
		const RendererWindowState::SceneOrbital base = MakeOrbital(OrbitalPreset::P);
		const SceneOrbitalMeshKey baseKey = MakeSceneOrbitalMeshKey(base, structure);

		const auto unchanged = [&](auto mutate) {
			RendererWindowState::SceneOrbital orbital = base;
			mutate(orbital);
			return MakeSceneOrbitalMeshKey(orbital, structure);
		};
		EXPECT_EQ(unchanged([](auto &o) { o.positiveLobeColor = glm::vec3(0.0f, 1.0f, 0.0f); }), baseKey);
		EXPECT_EQ(unchanged([](auto &o) { o.negativeLobeColor = glm::vec3(1.0f, 1.0f, 0.0f); }), baseKey);
		EXPECT_EQ(unchanged([](auto &o) { o.alpha = 0.2f; }), baseKey);
		EXPECT_EQ(unchanged([](auto &o) { o.visible = false; }), baseKey);
	}

	TEST(SceneOrbitalMeshKeyTests, AnchoredOrbitalRebakesWhenItsAtomMoves)
	{
		RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::Sp3);
		orbital.anchorAtoms = {0};

		const SceneOrbitalMeshKey before = MakeSceneOrbitalMeshKey(orbital, structure);
		structure.atoms[0].cartesianPosition = glm::vec3(0.0f, 0.0f, 3.0f);
		EXPECT_NE(MakeSceneOrbitalMeshKey(orbital, structure), before);
	}

	// --- per-frame anchoring -------------------------------------------------------------------

	TEST(ResolveAnchoredOrbitalsTests, AnchoredOrbitalsFollowTheirAtoms)
	{
		RendererWindowState window;
		window.structure = MakeStructure();

		RendererWindowState::SceneOrbital anchored = MakeOrbital(OrbitalPreset::Sigma);
		anchored.anchorAtoms = {0, 1};
		RendererWindowState::SceneOrbital standalone = MakeOrbital(OrbitalPreset::S);
		standalone.centerA = glm::vec3(4.0f, 4.0f, 4.0f);
		RendererWindowState::SceneOrbital stale = MakeOrbital(OrbitalPreset::S);
		stale.centerA = glm::vec3(-1.0f, -2.0f, -3.0f);
		stale.anchorAtoms = {42};
		window.sceneOrbitals = {anchored, standalone, stale};

		window.structure.atoms[1].cartesianPosition = glm::vec3(0.0f, 0.0f, 2.5f);
		ResolveAnchoredOrbitals(window);

		EXPECT_EQ(window.sceneOrbitals[0].centerA, glm::vec3(0.0f));
		EXPECT_EQ(window.sceneOrbitals[0].centerB, glm::vec3(0.0f, 0.0f, 2.5f));
		EXPECT_EQ(window.sceneOrbitals[1].centerA, glm::vec3(4.0f, 4.0f, 4.0f));
		EXPECT_EQ(window.sceneOrbitals[2].centerA, glm::vec3(-1.0f, -2.0f, -3.0f));
	}

	// --- defaults ----------------------------------------------------------------------------

	TEST(MakeDefaultSceneOrbitalTests, TwoCentrePresetAnchorsToTwoSelectedAtoms)
	{
		RendererWindowState window;
		window.structure = MakeStructure();
		window.selectedAtomIndices = {0, 1};

		const RendererWindowState::SceneOrbital orbital =
			MakeDefaultSceneOrbital(window, OrbitalPreset::Pi, glm::vec3(9.0f));
		EXPECT_EQ(orbital.preset, OrbitalPreset::Pi);
		EXPECT_EQ(orbital.anchorAtoms, std::vector<std::size_t>({0, 1}));
		EXPECT_EQ(orbital.centerA, glm::vec3(0.0f));
		EXPECT_EQ(orbital.centerB, glm::vec3(1.2f, 0.0f, 0.0f));
	}

	TEST(MakeDefaultSceneOrbitalTests, SingleCentrePresetAnchorsToOneSelectedAtom)
	{
		RendererWindowState window;
		window.structure = MakeStructure();
		window.selectedAtomIndices = {2}; // the hydrogen

		const RendererWindowState::SceneOrbital orbital =
			MakeDefaultSceneOrbital(window, OrbitalPreset::Sp3, glm::vec3(9.0f));
		EXPECT_EQ(orbital.anchorAtoms, std::vector<std::size_t>({2}));
		EXPECT_EQ(orbital.centerA, glm::vec3(0.0f, 2.0f, 0.0f));
	}

	TEST(MakeDefaultSceneOrbitalTests, WithoutAMatchingSelectionItLandsOnTheSeedPosition)
	{
		RendererWindowState window;
		window.structure = MakeStructure();

		const glm::vec3 seed(3.0f, 1.0f, -2.0f);
		const RendererWindowState::SceneOrbital single =
			MakeDefaultSceneOrbital(window, OrbitalPreset::P, seed);
		EXPECT_TRUE(single.anchorAtoms.empty());
		EXPECT_EQ(single.centerA, seed);

		// A two-centre preset with nothing to anchor to still needs two distinct centres.
		const RendererWindowState::SceneOrbital two =
			MakeDefaultSceneOrbital(window, OrbitalPreset::SigmaStar, seed);
		EXPECT_TRUE(two.anchorAtoms.empty());
		EXPECT_GT(glm::length(two.centerB - two.centerA), 0.0f);

		// One selected atom is not enough for a two-centre preset, and three is too many for either.
		window.selectedAtomIndices = {0};
		EXPECT_TRUE(MakeDefaultSceneOrbital(window, OrbitalPreset::Sigma, seed).anchorAtoms.empty());
		window.selectedAtomIndices = {0, 1, 2};
		EXPECT_TRUE(MakeDefaultSceneOrbital(window, OrbitalPreset::Sp3, seed).anchorAtoms.empty());
		EXPECT_TRUE(MakeDefaultSceneOrbital(window, OrbitalPreset::Sigma, seed).anchorAtoms.empty());
	}

	TEST(MakeDefaultSceneOrbitalTests, DefaultsAreSizedForTheAnchoredElement)
	{
		RendererWindowState window;
		window.structure = MakeStructure();

		window.selectedAtomIndices = {2}; // hydrogen
		const RendererWindowState::SceneOrbital onHydrogen =
			MakeDefaultSceneOrbital(window, OrbitalPreset::S, glm::vec3(0.0f));
		window.selectedAtomIndices = {0}; // carbon
		const RendererWindowState::SceneOrbital onCarbon =
			MakeDefaultSceneOrbital(window, OrbitalPreset::S, glm::vec3(0.0f));

		// Carbon holds its valence electrons tighter than hydrogen and sits a shell higher.
		EXPECT_GT(onCarbon.effectiveCharge, onHydrogen.effectiveCharge);
		EXPECT_GT(onCarbon.shell, onHydrogen.shell);
	}

	// The path the Add menu actually takes: MakeDefaultSceneOrbital, then straight to the mesher.
	// Every preset a user can pick has to come back with something to draw, from a cold scene with
	// nothing selected and from an anchored atom - "I clicked sp2 and nothing happened" is exactly
	// what this catches, and the geometry tests above did not, because they all set their own
	// centres and resolutions instead of using the defaults.
	TEST(MakeDefaultSceneOrbitalTests, EveryPresetFromTheMenuProducesAVisibleMesh)
	{
		constexpr OrbitalPreset kAll[] = {OrbitalPreset::S, OrbitalPreset::P, OrbitalPreset::D,
			OrbitalPreset::Sp, OrbitalPreset::Sp2, OrbitalPreset::Sp3, OrbitalPreset::Sigma,
			OrbitalPreset::SigmaStar, OrbitalPreset::Pi, OrbitalPreset::PiStar, OrbitalPreset::Delta,
			OrbitalPreset::DeltaStar, OrbitalPreset::SpSigma, OrbitalPreset::SpSigmaStar,
			OrbitalPreset::Sp2Sigma, OrbitalPreset::Sp2SigmaStar, OrbitalPreset::Sp3Sigma,
			OrbitalPreset::Sp3SigmaStar};

		// RendererWindowState owns a Unique<RendererViewCamera> and so is non-copyable - three
		// separate windows rather than copies of one.
		RendererWindowState empty;
		empty.structure = MakeStructure();
		RendererWindowState onOneAtom;
		onOneAtom.structure = MakeStructure();
		onOneAtom.selectedAtomIndices = {0};
		RendererWindowState onTwoAtoms;
		onTwoAtoms.structure = MakeStructure();
		onTwoAtoms.selectedAtomIndices = {0, 1};

		for (const OrbitalPreset preset : kAll)
		{
			for (const RendererWindowState *window : {&empty, &onOneAtom, &onTwoAtoms})
			{
				const RendererWindowState::SceneOrbital orbital =
					MakeDefaultSceneOrbital(*window, preset, glm::vec3(0.0f));
				const std::vector<IsosurfaceVertex> mesh = BuildSceneOrbitalMesh(orbital, window->structure);
				EXPECT_FALSE(mesh.empty())
					<< OrbitalPresetName(preset) << " with " << window->selectedAtomIndices.size()
					<< " atom(s) selected meshed to nothing";
			}
		}
	}

	// A sampling box far bigger than the orbital is the quiet way to mesh nothing: the shape ends up
	// spanning a handful of samples and the iso value falls between them. Pin the box to the size of
	// what is actually in it.
	TEST(MakeDefaultSceneOrbitalTests, DefaultBoxIsNotWildlyBiggerThanTheOrbitalInIt)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState window;
		window.structure = structure;

		for (const OrbitalPreset preset : {OrbitalPreset::S, OrbitalPreset::Sp2, OrbitalPreset::Pi})
		{
			const RendererWindowState::SceneOrbital orbital =
				MakeDefaultSceneOrbital(window, preset, glm::vec3(0.0f));
			const std::vector<IsosurfaceVertex> mesh = BuildSceneOrbitalMesh(orbital, structure);
			ASSERT_FALSE(mesh.empty()) << OrbitalPresetName(preset);

			const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, structure);
			const float drawnRadius = MeshRadius(mesh, centers.centroid);
			const float boxRadius = SuggestOrbitalExtent(BuildOrbitalWavefunction(orbital, structure));
			ASSERT_GT(drawnRadius, 0.0f) << OrbitalPresetName(preset);
			// The visible lobe should fill a decent share of the box it was sampled in.
			EXPECT_LT(boxRadius / drawnRadius, 3.0f)
				<< OrbitalPresetName(preset) << ": box radius " << boxRadius << " for a lobe of "
				<< drawnRadius;
		}
	}

	TEST(ValenceTests, KnownElementsDifferAndUnknownFallsBackToHydrogen)
	{
		EXPECT_EQ(ValenceShell("H"), 1);
		EXPECT_EQ(ValenceShell("C"), 2);
		EXPECT_EQ(ValenceShell("Si"), 3);
		EXPECT_GT(ValenceEffectiveCharge("O"), ValenceEffectiveCharge("C"));

		EXPECT_EQ(ValenceShell("Xx"), 1);
		EXPECT_FLOAT_EQ(ValenceEffectiveCharge("Xx"), 1.0f);
		EXPECT_EQ(ValenceShell(""), 1);
	}

	// --- picking --------------------------------------------------------------------------------

	namespace
	{
		[[nodiscard]] RendererWindowState::SceneOrbital PlacedOrbital(
			OrbitalPreset preset, const glm::vec3 &center, SceneObjectId id)
		{
			RendererWindowState::SceneOrbital orbital = MakeOrbital(preset);
			orbital.id = id;
			orbital.centerA = center;
			orbital.centerB = center + glm::vec3(1.5f, 0.0f, 0.0f);
			return orbital;
		}
	} // namespace

	TEST(SceneOrbitalBoundsTests, TheSphereCoversTheMeshItIsStandingInFor)
	{
		const RendererStructureData structure = MakeStructure();
		const RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::Sp3);

		const SceneOrbitalBounds bounds = SceneOrbitalWorldBounds(orbital, structure);
		ASSERT_GT(bounds.radius, 0.0f);

		// Every drawn vertex has to be inside the sphere used to pick it, or clicking the tip of a
		// lobe would select nothing. Small slack for the mesher landing a vertex on the box face.
		const std::vector<IsosurfaceVertex> mesh = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_FALSE(mesh.empty());
		for (const IsosurfaceVertex &vertex : mesh)
			EXPECT_LE(glm::length(vertex.position - bounds.center), bounds.radius * 1.05f);
	}

	TEST(SceneOrbitalBoundsTests, ScaleGrowsTheSphereWithTheDrawing)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState::SceneOrbital orbital = MakeOrbital(OrbitalPreset::P);
		const float unscaled = SceneOrbitalWorldBounds(orbital, structure).radius;

		orbital.scale = 3.0f;
		const float scaled = SceneOrbitalWorldBounds(orbital, structure).radius;
		EXPECT_NEAR(scaled, unscaled * 3.0f, unscaled * 0.05f);
	}

	TEST(PickSceneOrbitalTests, ARayThroughTheOrbitalFindsIt)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState windowState;
		windowState.sceneOrbitals.push_back(
			PlacedOrbital(OrbitalPreset::P, glm::vec3(0.0f), SceneObjectId{1}));

		const SceneOrbitalBounds bounds =
			SceneOrbitalWorldBounds(windowState.sceneOrbitals[0], structure);
		const glm::vec3 origin = bounds.center + glm::vec3(0.0f, 0.0f, bounds.radius * 4.0f);

		const std::optional<std::size_t> hit =
			PickSceneOrbital(windowState, structure, origin, glm::vec3(0.0f, 0.0f, -1.0f));
		ASSERT_TRUE(hit.has_value());
		EXPECT_EQ(*hit, 0u);

		// A ray that never approaches it misses, and a direction that need not be normalised is
		// still handled.
		EXPECT_FALSE(
			PickSceneOrbital(windowState, structure, origin, glm::vec3(0.0f, 0.0f, 17.0f)).has_value());
		EXPECT_TRUE(
			PickSceneOrbital(windowState, structure, origin, glm::vec3(0.0f, 0.0f, -17.0f)).has_value());
	}

	TEST(PickSceneOrbitalTests, HiddenOrbitalsAreNotPickable)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState windowState;
		windowState.sceneOrbitals.push_back(
			PlacedOrbital(OrbitalPreset::P, glm::vec3(0.0f), SceneObjectId{1}));
		windowState.sceneOrbitals[0].visible = false;

		const SceneOrbitalBounds bounds =
			SceneOrbitalWorldBounds(windowState.sceneOrbitals[0], structure);
		const glm::vec3 origin = bounds.center + glm::vec3(0.0f, 0.0f, bounds.radius * 4.0f);
		EXPECT_FALSE(
			PickSceneOrbital(windowState, structure, origin, glm::vec3(0.0f, 0.0f, -1.0f)).has_value());
	}

	TEST(PickSceneOrbitalTests, TheNearerOfTwoOverlappingOrbitalsWins)
	{
		const RendererStructureData structure = MakeStructure();
		RendererWindowState windowState;
		windowState.sceneOrbitals.push_back(
			PlacedOrbital(OrbitalPreset::P, glm::vec3(0.0f), SceneObjectId{1}));

		const float radius = SceneOrbitalWorldBounds(windowState.sceneOrbitals[0], structure).radius;
		// Second one squarely in front of the first along the ray, and listed after it - so index
		// order cannot be what decides the answer.
		windowState.sceneOrbitals.push_back(PlacedOrbital(
			OrbitalPreset::P, glm::vec3(0.0f, 0.0f, radius * 1.5f), SceneObjectId{2}));

		const glm::vec3 origin(0.0f, 0.0f, radius * 8.0f);
		const std::optional<std::size_t> hit =
			PickSceneOrbital(windowState, structure, origin, glm::vec3(0.0f, 0.0f, -1.0f));
		ASSERT_TRUE(hit.has_value());
		EXPECT_EQ(*hit, 1u);
	}

	TEST(PickSceneOrbitalTests, AnEmptyScenePicksNothing)
	{
		const RendererStructureData structure = MakeStructure();
		const RendererWindowState windowState;
		EXPECT_FALSE(PickSceneOrbital(
			windowState, structure, glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f))
						 .has_value());
	}
} // namespace DefectStudio::Tests
