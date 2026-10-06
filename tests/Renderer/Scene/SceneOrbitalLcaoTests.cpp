#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/SceneOrbitalLcao.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		using SceneOrbital = RendererWindowState::SceneOrbital;

		[[nodiscard]] glm::mat3 Frame(const glm::vec3 &eulerDegrees)
		{
			return glm::mat3_cast(glm::quat(glm::radians(eulerDegrees)));
		}

		// NV-: vacancy at the origin, N above it, three C below.
		[[nodiscard]] RendererWindowState NvWindow()
		{
			RendererWindowState window;
			const glm::vec3 positions[] = {
				glm::vec3(0.0f, 0.0f, 1.6f), glm::vec3(1.5f, 0.0f, -0.5f),
				glm::vec3(-0.75f, 1.3f, -0.5f), glm::vec3(-0.75f, -1.3f, -0.5f)};
			const char *elements[] = {"N", "C", "C", "C"};
			for (int i = 0; i < 4; ++i)
			{
				RendererAtomData atom;
				atom.element = elements[i];
				atom.cartesianPosition = positions[i];
				window.structure.atoms.push_back(atom);
			}
			return window;
		}

		[[nodiscard]] SelectionBasis NvBasis(const RendererWindowState &window)
		{
			SelectionBasis basis;
			basis.centre = glm::dvec3(0.0);
			for (std::size_t i = 0; i < window.structure.atoms.size(); ++i)
			{
				BasisSite site;
				site.element = window.structure.atoms[i].element;
				site.label = site.element + std::to_string(i);
				site.position = glm::dvec3(window.structure.atoms[i].cartesianPosition);
				basis.sites.push_back(site);
				basis.atomIndices.push_back(i);
			}
			return basis;
		}

		[[nodiscard]] SymmetryAdaptedVector Vector(const std::vector<double> &coefficients, const std::string &irrep = "A1")
		{
			SymmetryAdaptedVector vector;
			vector.irrepLabel = irrep;
			for (const double value : coefficients)
			{
				ExactCoefficient coefficient;
				coefficient.numeric = value;
				coefficient.exact = std::to_string(value);
				vector.coefficients.push_back(coefficient);
			}
			return vector;
		}

		void ExpectPointsAtCentre(const SceneOrbital::LcaoComponent &component, const RendererWindowState &window)
		{
			const glm::vec3 axis = Frame(component.rotationEuler) * *OrbitalPresetMemberAxis(component.preset, component.lobeIndex);
			const glm::vec3 inward = glm::normalize(-window.structure.atoms[component.anchorAtom].cartesianPosition);
			EXPECT_NEAR(glm::length(axis - inward), 0.0f, 1e-4f) << "atom " << component.anchorAtom;
		}

		// The same component as a stand-alone single-centre scene orbital.
		[[nodiscard]] SceneOrbital Single(const SceneOrbital::LcaoComponent &component)
		{
			SceneOrbital orbital;
			orbital.preset = component.preset;
			orbital.shell = component.shell;
			orbital.lobeIndex = component.lobeIndex;
			orbital.effectiveCharge = component.effectiveCharge;
			orbital.rotationEuler = component.rotationEuler;
			orbital.centerA = component.center;
			orbital.anchorAtoms = {component.anchorAtom};
			return orbital;
		}
	} // namespace

	// --- BuildSalcSceneOrbital ------------------------------------------------------------------

	TEST(SceneOrbitalLcaoTests, DanglingBondBasisPutsAnInwardSp3LobeOnEverySite)
	{
		const RendererWindowState window = NvWindow();

		const Result<SceneOrbital> orbital = BuildSalcSceneOrbital(
			window, Vector({0.9, 0.25, 0.25, 0.25}), NvBasis(window), SalcBasisFunction::Sp3DanglingBond, "a1");

		ASSERT_TRUE(orbital) << orbital.Error().technicalDetails;
		EXPECT_EQ(orbital->displayName, "a1");
		ASSERT_EQ(orbital->lcaoComponents.size(), 4u);
		for (std::size_t i = 0; i < 4; ++i)
		{
			const SceneOrbital::LcaoComponent &component = orbital->lcaoComponents[i];
			EXPECT_EQ(component.anchorAtom, i);
			EXPECT_EQ(component.center, window.structure.atoms[i].cartesianPosition);
			EXPECT_EQ(component.preset, OrbitalPreset::Sp3);
			EXPECT_EQ(component.lobeIndex, 0);
			EXPECT_EQ(component.shell, 2);
			EXPECT_FLOAT_EQ(component.effectiveCharge, ValenceEffectiveCharge(window.structure.atoms[i].element));
			ExpectPointsAtCentre(component, window);
		}
		EXPECT_FLOAT_EQ(orbital->lcaoComponents[0].coefficient, 0.9f);
		EXPECT_FLOAT_EQ(orbital->lcaoComponents[3].coefficient, 0.25f);
	}

	TEST(SceneOrbitalLcaoTests, ZeroCoefficientSitesAreLeftOut)
	{
		const RendererWindowState window = NvWindow();

		const Result<SceneOrbital> orbital = BuildSalcSceneOrbital(
			window, Vector({0.0, 0.816497, -0.408248, -0.408248}, "E"), NvBasis(window),
			SalcBasisFunction::Sp3DanglingBond, "e_x");

		ASSERT_TRUE(orbital);
		ASSERT_EQ(orbital->lcaoComponents.size(), 3u);
		EXPECT_EQ(orbital->lcaoComponents[0].anchorAtom, 1u);
		EXPECT_EQ(orbital->lcaoComponents[2].anchorAtom, 3u);
		EXPECT_LT(orbital->lcaoComponents[1].coefficient, 0.0f);
	}

	TEST(SceneOrbitalLcaoTests, PBasisPointsP_zAtTheCentre)
	{
		const RendererWindowState window = NvWindow();

		const Result<SceneOrbital> orbital = BuildSalcSceneOrbital(
			window, Vector({0.5, 0.5, 0.5, 0.5}), NvBasis(window), SalcBasisFunction::PTowardCentre, "p");

		ASSERT_TRUE(orbital);
		ASSERT_EQ(orbital->lcaoComponents.size(), 4u);
		for (const SceneOrbital::LcaoComponent &component : orbital->lcaoComponents)
		{
			EXPECT_EQ(component.preset, OrbitalPreset::P);
			EXPECT_EQ(component.lobeIndex, 0);
			ExpectPointsAtCentre(component, window);
		}
	}

	TEST(SceneOrbitalLcaoTests, SBasisHasNothingToAim)
	{
		const RendererWindowState window = NvWindow();

		const Result<SceneOrbital> orbital =
			BuildSalcSceneOrbital(window, Vector({0.5, 0.5, 0.5, 0.5}), NvBasis(window), SalcBasisFunction::S, "s");

		ASSERT_TRUE(orbital);
		for (const SceneOrbital::LcaoComponent &component : orbital->lcaoComponents)
		{
			EXPECT_EQ(component.preset, OrbitalPreset::S);
			EXPECT_EQ(component.rotationEuler, glm::vec3(0.0f));
		}
	}

	// The aim comes from the basis's unwrapped site positions, not from where the atom happens to be
	// drawn: a periodic image on the far side of the cell must still point into the vacancy.
	TEST(SceneOrbitalLcaoTests, AimUsesTheUnwrappedBasisPosition)
	{
		RendererWindowState window = NvWindow();
		const SelectionBasis basis = NvBasis(window);
		window.structure.atoms[1].cartesianPosition += glm::vec3(10.0f, 0.0f, 0.0f); // drawn one cell over

		const Result<SceneOrbital> orbital =
			BuildSalcSceneOrbital(window, Vector({0.0, 1.0, 0.0, 0.0}), basis, SalcBasisFunction::Sp3DanglingBond, "c");

		ASSERT_TRUE(orbital);
		ASSERT_EQ(orbital->lcaoComponents.size(), 1u);
		const SceneOrbital::LcaoComponent &component = orbital->lcaoComponents[0];
		const glm::vec3 axis = Frame(component.rotationEuler) * *OrbitalPresetMemberAxis(component.preset, 0);
		EXPECT_NEAR(glm::length(axis - glm::normalize(-glm::vec3(basis.sites[1].position))), 0.0f, 1e-4f);
	}

	TEST(SceneOrbitalLcaoTests, RefusesWhatItCannotDraw)
	{
		const RendererWindowState window = NvWindow();
		const SelectionBasis basis = NvBasis(window);

		SymmetryAdaptedVector complex = Vector({0.5, 0.5, 0.5, 0.5});
		complex.coefficients[2].numericImaginary = 0.4;
		const Result<SceneOrbital> complexResult =
			BuildSalcSceneOrbital(window, complex, basis, SalcBasisFunction::Sp3DanglingBond, "");
		ASSERT_FALSE(complexResult);
		EXPECT_EQ(complexResult.Error().code, "orbital.salc.complex_coefficient");

		const Result<SceneOrbital> mismatch =
			BuildSalcSceneOrbital(window, Vector({1.0, 0.0}), basis, SalcBasisFunction::Sp3DanglingBond, "");
		ASSERT_FALSE(mismatch);
		EXPECT_EQ(mismatch.Error().code, "orbital.salc.site_count_mismatch");

		const Result<SceneOrbital> allZero =
			BuildSalcSceneOrbital(window, Vector({0.0, 0.0, 0.0, 0.0}), basis, SalcBasisFunction::Sp3DanglingBond, "");
		ASSERT_FALSE(allZero);
		EXPECT_EQ(allZero.Error().code, "orbital.salc.all_zero");

		SelectionBasis stale = basis;
		stale.atomIndices[3] = 42;
		const Result<SceneOrbital> outOfRange =
			BuildSalcSceneOrbital(window, Vector({0.5, 0.5, 0.5, 0.5}), stale, SalcBasisFunction::Sp3DanglingBond, "");
		ASSERT_FALSE(outOfRange);
		EXPECT_EQ(outOfRange.Error().code, "orbital.salc.atom_out_of_range");
	}

	// --- an LCAO orbital through the existing geometry ------------------------------------------

	TEST(SceneOrbitalLcaoTests, WavefunctionIsTheCoefficientWeightedSumOfItsComponents)
	{
		const RendererWindowState window = NvWindow();
		const Result<SceneOrbital> lcao = BuildSalcSceneOrbital(
			window, Vector({0.7, -0.4, 0.3, 0.5}), NvBasis(window), SalcBasisFunction::Sp3DanglingBond, "mix");
		ASSERT_TRUE(lcao);

		const OrbitalWavefunction combined = BuildOrbitalWavefunction(*lcao, window.structure);
		for (const glm::vec3 &point :
			 {glm::vec3(0.0f), glm::vec3(0.4f, 0.1f, -0.2f), glm::vec3(1.0f, 0.2f, 0.0f), glm::vec3(-0.3f, 0.6f, 1.1f)})
		{
			float expected = 0.0f;
			for (const SceneOrbital::LcaoComponent &component : lcao->lcaoComponents)
				expected += component.coefficient *
					EvaluateOrbital(BuildOrbitalWavefunction(Single(component), window.structure), point);
			EXPECT_NEAR(EvaluateOrbital(combined, point), expected, 1e-4f * (1.0f + std::abs(expected)));
		}
	}

	TEST(SceneOrbitalLcaoTests, PhaseFlipNegatesAndPresetFieldsAreIgnored)
	{
		const RendererWindowState window = NvWindow();
		Result<SceneOrbital> lcao = BuildSalcSceneOrbital(
			window, Vector({0.7, 0.4, 0.3, 0.5}), NvBasis(window), SalcBasisFunction::Sp3DanglingBond, "");
		ASSERT_TRUE(lcao);
		const glm::vec3 point(0.3f, -0.2f, 0.4f);
		const float value = EvaluateOrbital(BuildOrbitalWavefunction(*lcao, window.structure), point);

		SceneOrbital changed = *lcao;
		changed.preset = OrbitalPreset::D;
		changed.lobeIndex = 3;
		changed.rotationEuler = glm::vec3(30.0f, 0.0f, 0.0f);
		changed.centerA = glm::vec3(9.0f);
		EXPECT_FLOAT_EQ(EvaluateOrbital(BuildOrbitalWavefunction(changed, window.structure), point), value);

		changed.phaseFlipped = true;
		EXPECT_FLOAT_EQ(EvaluateOrbital(BuildOrbitalWavefunction(changed, window.structure), point), -value);
	}

	TEST(SceneOrbitalLcaoTests, CentroidIsTheMeanOfTheComponentAtoms)
	{
		const RendererWindowState window = NvWindow();
		const Result<SceneOrbital> lcao = BuildSalcSceneOrbital(
			window, Vector({0.5, 0.5, 0.5, 0.5}), NvBasis(window), SalcBasisFunction::Sp3DanglingBond, "");
		ASSERT_TRUE(lcao);

		const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(*lcao, window.structure);

		glm::vec3 mean(0.0f);
		for (const RendererAtomData &atom : window.structure.atoms)
			mean += atom.cartesianPosition / 4.0f;
		EXPECT_NEAR(glm::length(centers.centroid - mean), 0.0f, 1e-5f);
	}

	TEST(SceneOrbitalLcaoTests, ComponentsFollowTheirAtomsAndFallBackWhenStale)
	{
		RendererWindowState window = NvWindow();
		Result<SceneOrbital> lcao = BuildSalcSceneOrbital(
			window, Vector({0.0, 1.0, 0.0, 0.0}), NvBasis(window), SalcBasisFunction::S, "");
		ASSERT_TRUE(lcao);

		window.structure.atoms[1].cartesianPosition = glm::vec3(2.0f, 0.0f, 0.0f);
		EXPECT_NEAR(glm::length(OrbitalCentroid(BuildOrbitalWavefunction(*lcao, window.structure)) - glm::vec3(2.0f, 0.0f, 0.0f)), 0.0f, 1e-5f);

		lcao->lcaoComponents[0].anchorAtom = 99;
		lcao->lcaoComponents[0].center = glm::vec3(5.0f, 0.0f, 0.0f);
		EXPECT_NEAR(glm::length(OrbitalCentroid(BuildOrbitalWavefunction(*lcao, window.structure)) - glm::vec3(5.0f, 0.0f, 0.0f)), 0.0f, 1e-5f);
	}

	TEST(SceneOrbitalLcaoTests, MeshKeyFollowsTheComponents)
	{
		const RendererWindowState window = NvWindow();
		const Result<SceneOrbital> lcao = BuildSalcSceneOrbital(
			window, Vector({0.5, 0.5, 0.5, 0.5}), NvBasis(window), SalcBasisFunction::Sp3DanglingBond, "");
		ASSERT_TRUE(lcao);
		SceneOrbital changed = *lcao;
		changed.lcaoComponents[2].coefficient = -0.5f;

		EXPECT_EQ(MakeSceneOrbitalMeshKey(*lcao, window.structure), MakeSceneOrbitalMeshKey(*lcao, window.structure));
		EXPECT_FALSE(MakeSceneOrbitalMeshKey(*lcao, window.structure) == MakeSceneOrbitalMeshKey(changed, window.structure));
	}

	TEST(SceneOrbitalLcaoTests, LcaoOrbitalMeshesIntoBothPhases)
	{
		const RendererWindowState window = NvWindow();
		Result<SceneOrbital> lcao = BuildSalcSceneOrbital(
			window, Vector({0.0, 0.816497, -0.408248, -0.408248}, "E"), NvBasis(window),
			SalcBasisFunction::Sp3DanglingBond, "e_x");
		ASSERT_TRUE(lcao);
		lcao->resolution = 24;

		const std::vector<IsosurfaceVertex> mesh = BuildSceneOrbitalMesh(*lcao, window.structure);

		ASSERT_FALSE(mesh.empty());
		EXPECT_EQ(mesh.size() % 3u, 0u);
		bool positive = false;
		bool negative = false;
		for (const IsosurfaceVertex &vertex : mesh)
		{
			positive |= vertex.sign > 0.0f;
			negative |= vertex.sign < 0.0f;
		}
		EXPECT_TRUE(positive);
		EXPECT_TRUE(negative);
	}
} // namespace DefectStudio::Tests
