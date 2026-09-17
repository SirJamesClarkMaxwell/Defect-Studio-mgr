#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Domain/Electronic/ElectronicStructureModel.hpp"

namespace DefectStudio
{
	// Bohr radius in Angstrom - every length in this header is Angstrom, matching the rest of the
	// domain, so this is the only place the atomic-units convention leaks in.
	inline constexpr float kBohrRadiusAngstrom = 0.529177210903f;

	// One real hydrogenic atomic orbital. `m` indexes the REAL spherical harmonics (the +/-m
	// complex pair recombined into cos/sin form), which is what a chemistry figure actually shows:
	// p_x / p_y / p_z rather than p_-1 / p_0 / p_+1. Ordering within a shell is m = -l .. +l with
	// m = 0 always the z-aligned member:
	//   l = 0:  0 -> s
	//   l = 1: -1 -> p_y,   0 -> p_z,   +1 -> p_x
	//   l = 2: -2 -> d_xy, -1 -> d_yz,  0 -> d_z2, +1 -> d_xz, +2 -> d_x2-y2
	struct AtomicOrbital
	{
		int n = 1;
		int l = 0;
		int m = 0;
		// Slater effective nuclear charge. 1 is true hydrogen; raising it contracts the orbital,
		// which is how a carbon 2p gets drawn tighter than a hydrogen 1s without leaving this
		// analytic model.
		float effectiveCharge = 1.0f;
	};

	// One term of an LCAO expansion: an atomic orbital on one centre, in one orientation, with a
	// signed coefficient. Every shape task 26 asks for is a list of these and nothing else - a bare
	// p orbital is one term, an sp3 hybrid lobe is four terms on ONE centre, a sigma bond is two
	// terms on TWO centres, and its antibonding partner is the same two terms with one coefficient
	// negated. That is why there is a single evaluator here instead of one per preset.
	struct OrbitalTerm
	{
		AtomicOrbital orbital;
		glm::vec3 center = glm::vec3(0.0f);
		// Rotation from the term's own frame (where `m` is defined against the world axes) into
		// scene space. Identity for an axis-aligned orbital; the two-centre presets set it so that
		// the bond axis becomes the term's local z, which is what makes sigma/pi/delta work for a
		// bond pointing anywhere.
		glm::mat3 orientation = glm::mat3(1.0f);
		float coefficient = 1.0f;
	};

	// psi(r) = sum_i c_i * phi_i(R_i^T (r - centre_i)). Real-valued and signed, which is exactly
	// what the existing isosurface path wants: GenerateIsosurfaceMesh extracts the +isoValue and
	// -isoValue surfaces in one pass and tags each vertex with IsosurfaceVertex::sign, so the two
	// phases colour independently with no extra work here.
	struct OrbitalWavefunction
	{
		std::vector<OrbitalTerm> terms;
	};

	// R_nl(r) for the given effective charge, at radius `radius` Angstrom. Normalised so that the
	// integral of R^2 r^2 dr over [0, inf) is 1. Returns 0 for an invalid (n, l): n < 1, or l
	// outside [0, n-1]. Split out from EvaluateAtomicOrbital so the radial nodes (2s has one, 3s
	// has two) can be tested on their own.
	[[nodiscard]] float HydrogenicRadial(int n, int l, float effectiveCharge, float radius);

	// Real spherical harmonic Y_lm for the direction of `offset`, using the m convention documented
	// on AtomicOrbital. Normalised so that the integral of Y^2 over the unit sphere is 1. Returns 0
	// for l < 0 or |m| > l, and for a zero-length `offset` unless l == 0.
	//
	// ponytail: also returns 0 above l = 3, where the implementation stops. The explicit
	// closed forms up to f are cheaper and far clearer than a general recurrence, and nothing
	// past f has a picture anyone draws. Adding g means extending that one switch, nothing else.
	[[nodiscard]] float RealSphericalHarmonic(int l, int m, const glm::vec3 &offset);

	// R_nl * Y_lm at `offset` = point - centre, already in the orbital's own frame. Normalised so
	// that the integral of phi^2 over all space is 1 (units Angstrom^-3/2). Returns 0 for an
	// invalid (n, l, m).
	[[nodiscard]] float EvaluateAtomicOrbital(const AtomicOrbital &orbital, const glm::vec3 &offset);

	// The full LCAO sum at a world-space point. NOT renormalised: the preset builders below set
	// coefficients already normalised in the non-overlapping approximation, and the iso value is
	// chosen relative to the grid's own peak anyway (see SuggestOrbitalIsoValue), so an exact norm
	// buys nothing a viewer can see.
	[[nodiscard]] float EvaluateOrbital(const OrbitalWavefunction &wavefunction, const glm::vec3 &point);

	// Radius in Angstrom beyond which the wavefunction's amplitude is negligible, measured from
	// OrbitalCentroid. Sizes the sampling box so a 1s and a 3d both get a box that fits.
	[[nodiscard]] float SuggestOrbitalExtent(const OrbitalWavefunction &wavefunction);

	// Centroid of the wavefunction's term centres - where the sampling box gets centred.
	[[nodiscard]] glm::vec3 OrbitalCentroid(const OrbitalWavefunction &wavefunction);

	struct OrbitalSamplingSettings
	{
		// Samples per axis. 64^3 is the interactive default; the export path can raise it, and
		// UpsampleOrbitalGrid still works on the result since this produces an ordinary
		// OrbitalGridData.
		glm::ivec3 dimensions = glm::ivec3(64);
		// Half-width of the sampled cube in Angstrom. <= 0 means "ask SuggestOrbitalExtent".
		float extent = 0.0f;
	};

	// Samples psi onto an axis-aligned cube centred on OrbitalCentroid and writes it as an ordinary
	// OrbitalGridData - the same struct the WAVECAR path produces, so analytic orbitals feed the
	// existing GPU marching-tetrahedra renderer (RendererLayer::RegenerateOrbitalIsosurface) with
	// no new render path. `energy` and `occupation` stay 0: an analytic drawing orbital has no
	// calculated energy, and nothing downstream requires one.
	//
	// ponytail: the box is sampled corner to corner (sample i sits at i / (dimensions - 1) of the
	// way across), while the mesher maps a grid point to i / dimensions - correct for a WAVECAR
	// grid, which is periodic and whose last sample is NOT a repeat of the first. So a meshed
	// analytic orbital comes out smaller than the sampled one by (N - 1) / N: 1.6% at the default
	// 64 samples, shrinking as the grid grows, and invisible on a drawing aid. The fix when it
	// stops being invisible is a per-grid periodic flag the mesher reads, not a second mesher.
	[[nodiscard]] OrbitalGridData SampleOrbitalToGrid(
		const OrbitalWavefunction &wavefunction, const OrbitalSamplingSettings &settings);

	// An iso value that renders as a recognisable lobe for the given grid: a fixed fraction of the
	// grid's peak absolute amplitude, so a diffuse 3d and a tight 1s both come out looking like the
	// textbook picture instead of one filling the box and the other vanishing. Returns 0 for an
	// empty or all-zero grid.
	[[nodiscard]] float SuggestOrbitalIsoValue(const OrbitalGridData &grid, float fractionOfPeak = 0.2f);

	// Everything task 26 lists. Single-centre presets draw one orbital on one atom; two-centre
	// presets draw a molecular orbital built from two atomic orbitals, one on each atom, and the
	// starred member of each pair is the SAME two terms with the second coefficient negated - which
	// is what puts the node between the nuclei.
	enum class OrbitalPreset
	{
		S,
		P,
		D,
		// The seven real f orbitals, lobeIndex selecting m as documented on OrbitalPresetSettings.
		// Needs shell >= 4, and is clamped up to it - there is no 3f.
		F,
		// One lobe of a hybrid on a single centre; OrbitalPresetSettings::lobeIndex picks which of
		// the 2 / 3 / 4 equivalent lobes. Drawing a whole sp3 centre means four scene orbitals, one
		// per lobe, because each lobe is genuinely a separate wavefunction.
		//
		// Axis convention for all three, so that `orientation` means the same thing whichever
		// hybrid is chosen: lobe 0 always points along +z and the set is symmetric about z. So sp
		// is +z / -z; sp2 is +z plus two more at 120 degrees in the xz plane; sp3 is +z plus three
		// more at the tetrahedral angle, evenly spaced in azimuth. That is the usual tetrahedron,
		// just rotated so one vertex sits on the z axis.
		Sp,
		Sp2,
		Sp3,
		// Head-on overlap of the two centres' p orbitals aligned with the bond axis. At shell = 1
		// there is no p to build from, so it degrades to the s-s combination - which is the right
		// answer anyway, that being the sigma bond of H2.
		Sigma,
		SigmaStar,
		Pi,
		PiStar,
		Delta,
		DeltaStar,
		// Molecular orbitals built from two hybrid lobes pointing at each other across the bond
		// rather than from two pure atomic orbitals - the picture a textbook draws for the C-C
		// sigma bond in ethane. Starred members are the antibonding combination, as above.
		SpSigma,
		SpSigmaStar,
		Sp2Sigma,
		Sp2SigmaStar,
		Sp3Sigma,
		Sp3SigmaStar
	};

	struct OrbitalPresetSettings
	{
		glm::vec3 centerA = glm::vec3(0.0f);
		// Read only by the two-centre presets (Sigma onwards); ignored for S / P / D / Sp / Sp2 /
		// Sp3. If it coincides with centerA the two-centre presets fall back to a unit separation
		// along +x rather than producing a degenerate orbital.
		glm::vec3 centerB = glm::vec3(1.5f, 0.0f, 0.0f);
		float effectiveCharge = 1.0f;
		// Which member of a degenerate or multi-lobe set, clamped into range:
		//   P   -> 0 = p_z, 1 = p_x, 2 = p_y
		//   D   -> 0 = d_z2, 1 = d_xz, 2 = d_yz, 3 = d_x2-y2, 4 = d_xy
	//   F   -> 0 = f_z3, 1 = f_xz2, 2 = f_yz2, 3 = f_z(x2-y2), 4 = f_xyz, 5 = f_x(x2-3y2),
	//          6 = f_y(3x2-y2)  (m = 0, +1, -1, +2, -2, +3, -3 in that order)
		//   Sp  -> 0..1, Sp2 -> 0..2, Sp3 -> 0..3 (which hybrid lobe)
		//   Pi / PiStar / Delta / DeltaStar -> 0..1 (which of the two degenerate orientations)
		// Ignored by S, Sigma and SigmaStar, which have only one member.
		int lobeIndex = 0;
		// Principal quantum number to build the preset from: n = 2 gives the 2s/2p orbitals and the
		// hybrids a chemistry course draws, n = 3 their diffuse versions. Clamped so the preset's
		// angular momentum stays valid (D and Delta need n >= 3).
		int shell = 2;
		// Rotation applied to a single-centre preset, so a p or a hybrid lobe can point anywhere
		// without the caller rebuilding the term list. The two-centre presets derive their own
		// orientation from centerB - centerA and ignore this.
		glm::mat3 orientation = glm::mat3(1.0f);
	};

	[[nodiscard]] OrbitalWavefunction MakeOrbitalPreset(
		OrbitalPreset preset, const OrbitalPresetSettings &settings);

	// Stable identifier for a preset, for persistence and for the properties-panel combo - "sp3",
	// "pi*", "sigma". Round-trips with ParseOrbitalPreset.
	[[nodiscard]] const char *OrbitalPresetName(OrbitalPreset preset);

	// Inverse of OrbitalPresetName. Returns false and leaves `preset` untouched for an unknown
	// name, so a file written by a newer version degrades instead of being guessed at.
	[[nodiscard]] bool ParseOrbitalPreset(const std::string &name, OrbitalPreset &preset);

	// How the presets are filed in the Add menu and in the properties-panel combo. Eighteen-plus
	// entries in one flat list is not a menu anyone reads, and the grouping is a property of the
	// physics (an antibonding combination is antibonding wherever it is shown), not of ImGui -
	// which is why it lives here beside the presets rather than being hand-listed in the panel
	// that happens to draw them today.
	enum class OrbitalPresetGroup
	{
		Atomic,         // s, p, d, f
		Hybrid,         // sp, sp2, sp3
		Bonding,        // sigma, pi, delta
		Antibonding,    // sigma*, pi*, delta*
		HybridBonding   // sp-sigma .. sp3-sigma*, both members
	};

	// Display name, e.g. "Atomowe". Non-empty and distinct for every group.
	[[nodiscard]] const char *OrbitalPresetGroupName(OrbitalPresetGroup group);

	// Which drawer a preset belongs in. Total: every preset has exactly one group.
	[[nodiscard]] OrbitalPresetGroup OrbitalPresetGroupOf(OrbitalPreset preset);

	// The presets of one group, in the order they should be listed. Concatenating all five groups
	// yields every preset exactly once - that is the invariant the menu relies on to stay complete
	// when a preset is added, and it is what the tests check.
	[[nodiscard]] std::vector<OrbitalPreset> OrbitalPresetsInGroup(OrbitalPresetGroup group);

	// All five groups in menu order. Provided so a caller can render the whole menu without
	// naming the enumerators itself, and so adding a group cannot leave one silently undrawn.
	[[nodiscard]] std::vector<OrbitalPresetGroup> AllOrbitalPresetGroups();
} // namespace DefectStudio
