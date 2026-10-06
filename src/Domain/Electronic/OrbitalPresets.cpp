#include "Core/dspch.hpp"
#include "Domain/Electronic/HydrogenicOrbital.hpp"

#include <algorithm>
#include <span>
#include <array>
#include <cmath>

namespace DefectStudio
{
	namespace
	{
		constexpr int kMaximumShell = 4;
		constexpr std::array<int, 3> kPMValues = {0, 1, -1};
		constexpr std::array<int, 5> kDMValues = {0, 1, -1, 2, -2};
		constexpr std::array<int, 7> kFMValues = {0, 1, -1, 2, -2, 3, -3};

		[[nodiscard]] AtomicOrbital MakeAtomicOrbital(
			int n, int l, int m, float effectiveCharge)
		{
			return AtomicOrbital{n, l, m, effectiveCharge};
		}

		void AddTerm(
			OrbitalWavefunction &wavefunction, const AtomicOrbital &orbital, const glm::vec3 &center,
			const glm::mat3 &orientation, float coefficient)
		{
			wavefunction.terms.push_back(OrbitalTerm{orbital, center, orientation, coefficient});
		}

		[[nodiscard]] glm::vec3 HybridDirection(int pCount, int lobeIndex)
		{
			if (pCount == 1)
				return lobeIndex == 0 ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 0.0f, -1.0f);

			if (pCount == 2)
			{
				constexpr float rootThreeOverTwo = 0.866025403784f;
				const std::array<glm::vec3, 3> directions = {
					glm::vec3(0.0f, 0.0f, 1.0f),
					glm::vec3(rootThreeOverTwo, 0.0f, -0.5f),
					glm::vec3(-rootThreeOverTwo, 0.0f, -0.5f)};
				return directions[std::clamp(lobeIndex, 0, 2)];
			}

			constexpr float radial = 0.942809041582f; // 2 * sqrt(2) / 3
			constexpr float z = -1.0f / 3.0f;
			const std::array<glm::vec3, 4> directions = {
				glm::vec3(0.0f, 0.0f, 1.0f),
				glm::vec3(radial, 0.0f, z),
				glm::vec3(-0.5f * radial, 0.866025403784f * radial, z),
				glm::vec3(-0.5f * radial, -0.866025403784f * radial, z)};
			return directions[std::clamp(lobeIndex, 0, 3)];
		}

		void AddHybrid(
			OrbitalWavefunction &wavefunction, int pCount, int shell, float effectiveCharge,
			const glm::vec3 &center, const glm::mat3 &orientation, const glm::vec3 &direction,
			float overallCoefficient)
		{
			const float sCoefficient = -overallCoefficient / std::sqrt(static_cast<float>(pCount + 1));
			const float pCoefficient = overallCoefficient *
				std::sqrt(static_cast<float>(pCount) / static_cast<float>(pCount + 1));
			AddTerm(
				wavefunction, MakeAtomicOrbital(shell, 0, 0, effectiveCharge), center, orientation, sCoefficient);

			if (pCount >= 2)
			{
				AddTerm(
					wavefunction, MakeAtomicOrbital(shell, 1, 1, effectiveCharge), center, orientation,
					pCoefficient * direction.x);
			}
			if (pCount >= 3)
			{
				AddTerm(
					wavefunction, MakeAtomicOrbital(shell, 1, -1, effectiveCharge), center, orientation,
					pCoefficient * direction.y);
			}
			AddTerm(
				wavefunction, MakeAtomicOrbital(shell, 1, 0, effectiveCharge), center, orientation,
				pCoefficient * direction.z);
		}

		[[nodiscard]] glm::mat3 OrientationFromZ(const glm::vec3 &direction)
		{
			const glm::vec3 z = glm::normalize(direction);
			const glm::vec3 helper =
				std::abs(z.z) > 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
			const glm::vec3 x = glm::normalize(glm::cross(helper, z));
			const glm::vec3 y = glm::cross(z, x);
			return glm::mat3(x, y, z);
		}

		struct BondFrame
		{
			glm::vec3 centerB;
			glm::vec3 direction;
			glm::mat3 orientation;
		};

		[[nodiscard]] BondFrame MakeBondFrame(const OrbitalPresetSettings &settings)
		{
			glm::vec3 centerB = settings.centerB;
			glm::vec3 offset = centerB - settings.centerA;
			if (glm::dot(offset, offset) <= 1e-12f)
			{
				centerB = settings.centerA + glm::vec3(1.0f, 0.0f, 0.0f);
				offset = centerB - settings.centerA;
			}
			const glm::vec3 direction = glm::normalize(offset);
			return BondFrame{centerB, direction, OrientationFromZ(direction)};
		}

		[[nodiscard]] bool IsAntibonding(OrbitalPreset preset)
		{
			switch (preset)
			{
				case OrbitalPreset::SigmaStar:
				case OrbitalPreset::PiStar:
				case OrbitalPreset::DeltaStar:
				case OrbitalPreset::SpSigmaStar:
				case OrbitalPreset::Sp2SigmaStar:
				case OrbitalPreset::Sp3SigmaStar: return true;
				default: return false;
			}
		}

		[[nodiscard]] int HybridPCount(OrbitalPreset preset)
		{
			switch (preset)
			{
				case OrbitalPreset::Sp:
				case OrbitalPreset::SpSigma:
				case OrbitalPreset::SpSigmaStar: return 1;
				case OrbitalPreset::Sp2:
				case OrbitalPreset::Sp2Sigma:
				case OrbitalPreset::Sp2SigmaStar: return 2;
				case OrbitalPreset::Sp3:
				case OrbitalPreset::Sp3Sigma:
				case OrbitalPreset::Sp3SigmaStar: return 3;
				default: return 0;
			}
		}
	} // namespace

	OrbitalWavefunction MakeOrbitalPreset(
		OrbitalPreset preset, const OrbitalPresetSettings &settings)
	{
		OrbitalWavefunction wavefunction;
		const int hybridPCount = HybridPCount(preset);
		if (preset == OrbitalPreset::S)
		{
			const int shell = std::clamp(settings.shell, 1, kMaximumShell);
			AddTerm(
				wavefunction, MakeAtomicOrbital(shell, 0, 0, settings.effectiveCharge), settings.centerA,
				settings.orientation, 1.0f);
			return wavefunction;
		}
		if (preset == OrbitalPreset::P)
		{
			const int shell = std::clamp(settings.shell, 2, kMaximumShell);
			const int lobe = std::clamp(settings.lobeIndex, 0, 2);
			AddTerm(
				wavefunction, MakeAtomicOrbital(shell, 1, kPMValues[lobe], settings.effectiveCharge),
				settings.centerA, settings.orientation, 1.0f);
			return wavefunction;
		}
		if (preset == OrbitalPreset::D)
		{
			const int shell = std::clamp(settings.shell, 3, kMaximumShell);
			const int lobe = std::clamp(settings.lobeIndex, 0, 4);
			AddTerm(
				wavefunction, MakeAtomicOrbital(shell, 2, kDMValues[lobe], settings.effectiveCharge),
				settings.centerA, settings.orientation, 1.0f);
			return wavefunction;
		}
		if (preset == OrbitalPreset::F)
		{
			const int shell = std::clamp(settings.shell, 4, kMaximumShell);
			const int lobe = std::clamp(settings.lobeIndex, 0, 6);
			AddTerm(
				wavefunction, MakeAtomicOrbital(shell, 3, kFMValues[lobe], settings.effectiveCharge),
				settings.centerA, settings.orientation, 1.0f);
			return wavefunction;
		}
		if (preset == OrbitalPreset::Sp || preset == OrbitalPreset::Sp2 || preset == OrbitalPreset::Sp3)
		{
			const int shell = std::clamp(settings.shell, 2, kMaximumShell);
			const int lobe = std::clamp(settings.lobeIndex, 0, hybridPCount);
			AddHybrid(
				wavefunction, hybridPCount, shell, settings.effectiveCharge, settings.centerA,
				settings.orientation, HybridDirection(hybridPCount, lobe), 1.0f);
			return wavefunction;
		}

		const BondFrame bond = MakeBondFrame(settings);
		constexpr float centreCoefficient = 0.707106781187f;
		if (preset == OrbitalPreset::Sigma || preset == OrbitalPreset::SigmaStar)
		{
			const int shell = std::clamp(settings.shell, 1, kMaximumShell);
			const int angularMomentum = shell == 1 ? 0 : 1;
			const AtomicOrbital orbital =
				MakeAtomicOrbital(shell, angularMomentum, 0, settings.effectiveCharge);
			const float bondingB = angularMomentum == 0 ? centreCoefficient : -centreCoefficient;
			AddTerm(wavefunction, orbital, settings.centerA, bond.orientation, centreCoefficient);
			AddTerm(
				wavefunction, orbital, bond.centerB, bond.orientation,
				IsAntibonding(preset) ? -bondingB : bondingB);
			return wavefunction;
		}
		if (preset == OrbitalPreset::Pi || preset == OrbitalPreset::PiStar)
		{
			const int shell = std::clamp(settings.shell, 2, kMaximumShell);
			const int m = std::clamp(settings.lobeIndex, 0, 1) == 0 ? 1 : -1;
			const AtomicOrbital orbital = MakeAtomicOrbital(shell, 1, m, settings.effectiveCharge);
			AddTerm(wavefunction, orbital, settings.centerA, bond.orientation, centreCoefficient);
			AddTerm(
				wavefunction, orbital, bond.centerB, bond.orientation,
				IsAntibonding(preset) ? -centreCoefficient : centreCoefficient);
			return wavefunction;
		}
		if (preset == OrbitalPreset::Delta || preset == OrbitalPreset::DeltaStar)
		{
			const int shell = std::clamp(settings.shell, 3, kMaximumShell);
			const int m = std::clamp(settings.lobeIndex, 0, 1) == 0 ? 2 : -2;
			const AtomicOrbital orbital = MakeAtomicOrbital(shell, 2, m, settings.effectiveCharge);
			AddTerm(wavefunction, orbital, settings.centerA, bond.orientation, centreCoefficient);
			AddTerm(
				wavefunction, orbital, bond.centerB, bond.orientation,
				IsAntibonding(preset) ? -centreCoefficient : centreCoefficient);
			return wavefunction;
		}
		if (hybridPCount > 0)
		{
			const int shell = std::clamp(settings.shell, 2, kMaximumShell);
			AddHybrid(
				wavefunction, hybridPCount, shell, settings.effectiveCharge, settings.centerA,
				bond.orientation, glm::vec3(0.0f, 0.0f, 1.0f), centreCoefficient);
			AddHybrid(
				wavefunction, hybridPCount, shell, settings.effectiveCharge, bond.centerB,
				OrientationFromZ(-bond.direction), glm::vec3(0.0f, 0.0f, 1.0f),
				IsAntibonding(preset) ? -centreCoefficient : centreCoefficient);
		}
		return wavefunction;
	}

	std::optional<glm::vec3> OrbitalPresetMemberAxis(OrbitalPreset preset, int lobeIndex)
	{
		if (preset == OrbitalPreset::Sp || preset == OrbitalPreset::Sp2 || preset == OrbitalPreset::Sp3)
		{
			const int pCount = HybridPCount(preset);
			return HybridDirection(pCount, std::clamp(lobeIndex, 0, pCount));
		}
		if (preset == OrbitalPreset::P)
		{
			const int m = kPMValues[std::clamp(lobeIndex, 0, 2)];
			return m == 0 ? glm::vec3(0, 0, 1) : m == 1 ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
		}
		if (preset == OrbitalPreset::D)
		{
			switch (kDMValues[std::clamp(lobeIndex, 0, 4)])
			{
				case 0: return glm::vec3(0, 0, 1);
				case 1: return glm::normalize(glm::vec3(1, 0, 1));
				case -1: return glm::normalize(glm::vec3(0, 1, 1));
				case 2: return glm::vec3(1, 0, 0);
				case -2: return glm::normalize(glm::vec3(1, 1, 0));
			}
		}
		if (preset == OrbitalPreset::F && kFMValues[std::clamp(lobeIndex, 0, 6)] == 0)
			return glm::vec3(0, 0, 1);
		return std::nullopt;
	}

	const char *OrbitalPresetName(OrbitalPreset preset)
	{
		constexpr std::array<const char *, 19> names = {
			"s", "p", "d", "f", "sp", "sp2", "sp3", "sigma", "sigma*", "pi", "pi*", "delta",
			"delta*", "sp-sigma", "sp-sigma*", "sp2-sigma", "sp2-sigma*", "sp3-sigma", "sp3-sigma*"};
		const int index = static_cast<int>(preset);
		return index >= 0 && index < static_cast<int>(names.size()) ? names[index] : "";
	}

	bool ParseOrbitalPreset(const std::string &name, OrbitalPreset &preset)
	{
		for (int index = 0; index < 19; ++index)
		{
			const auto candidate = static_cast<OrbitalPreset>(index);
			if (name == OrbitalPresetName(candidate))
			{
				preset = candidate;
				return true;
			}
		}
		return false;
	}

	namespace
	{
		// The members lobeIndex selects, in its own order. Kept beside the preset table rather than
		// in the panel that draws them: which orbitals a preset is made of is physics, and the same
		// list is needed by the properties combo and by "add every lobe at once".
		[[nodiscard]] std::span<const char *const> PresetMembers(OrbitalPreset preset)
		{
			static const char *const kS[] = {"s"};
			static const char *const kP[] = {"p_z", "p_x", "p_y"};
			static const char *const kD[] = {"d_z2", "d_xz", "d_yz", "d_x2-y2", "d_xy"};
			static const char *const kF[] = {
				"f_z3", "f_xz2", "f_yz2", "f_z(x2-y2)", "f_xyz", "f_x(x2-3y2)", "f_y(3x2-y2)"};
			// Lobe 0 is always +z and the set is symmetric about z - see the axis convention on
			// OrbitalPreset - so the angles below are the real directions, not decoration.
			static const char *const kSp[] = {"sp #1 (+z)", "sp #2 (-z)"};
			static const char *const kSp2[] = {"sp2 #1 (+z)", "sp2 #2 (120 st.)", "sp2 #3 (240 st.)"};
			static const char *const kSp3[] = {
				"sp3 #1 (+z)", "sp3 #2 (tetraedryczny)", "sp3 #3 (tetraedryczny)", "sp3 #4 (tetraedryczny)"};
			static const char *const kSigma[] = {"sigma (wzdluz wiazania)"};
			static const char *const kSigmaStar[] = {"sigma* (wzdluz wiazania)"};
			// The two degenerate perpendiculars to the bond axis. Which one reads as "in the
			// molecular plane" depends on where the rest of the molecule is, which this layer does
			// not know - hence the axis, not a claim about the plane.
			static const char *const kPi[] = {"pi (prostopadly x)", "pi (prostopadly y)"};
			static const char *const kPiStar[] = {"pi* (prostopadly x)", "pi* (prostopadly y)"};
			static const char *const kDelta[] = {"delta (x2-y2)", "delta (xy)"};
			static const char *const kDeltaStar[] = {"delta* (x2-y2)", "delta* (xy)"};
			static const char *const kHybridSigma[] = {"sigma z hybryd"};

			switch (preset)
			{
				case OrbitalPreset::S: return kS;
				case OrbitalPreset::P: return kP;
				case OrbitalPreset::D: return kD;
				case OrbitalPreset::F: return kF;
				case OrbitalPreset::Sp: return kSp;
				case OrbitalPreset::Sp2: return kSp2;
				case OrbitalPreset::Sp3: return kSp3;
				case OrbitalPreset::Sigma: return kSigma;
				case OrbitalPreset::SigmaStar: return kSigmaStar;
				case OrbitalPreset::Pi: return kPi;
				case OrbitalPreset::PiStar: return kPiStar;
				case OrbitalPreset::Delta: return kDelta;
				case OrbitalPreset::DeltaStar: return kDeltaStar;
				default: return kHybridSigma;
			}
		}
	} // namespace

	namespace
	{
		// Display counterpart of PresetMembers, in the same order. Kept as its own table rather than
		// derived from the ASCII one: the two differ by more than a character substitution (pi's
		// members are labelled by the axis they point along, not by the word "prostopadly"), and a
		// clever transform would have to be read to be believed.
		[[nodiscard]] std::span<const char *const> PresetMemberDisplays(OrbitalPreset preset)
		{
			static const char *const kS[] = {"s"};
			static const char *const kP[] = {"p_z", "p_x", "p_y"};
			static const char *const kD[] = {"d_z²", "d_xz", "d_yz", "d_x²-y²", "d_xy"};
			static const char *const kF[] = {
				"f_z³", "f_xz²", "f_yz²", "f_z(x²-y²)", "f_xyz", "f_x(x²-3y²)", "f_y(3x²-y²)"};
			static const char *const kSp[] = {"sp #1 (+z)", "sp #2 (-z)"};
			static const char *const kSp2[] = {"sp² #1 (+z)", "sp² #2 (120°)", "sp² #3 (240°)"};
			static const char *const kSp3[] = {
				"sp³ #1 (+z)", "sp³ #2 (tetraedryczny)", "sp³ #3 (tetraedryczny)", "sp³ #4 (tetraedryczny)"};
			static const char *const kSigma[] = {"σ (wzdluz wiazania)"};
			static const char *const kSigmaStar[] = {"σ* (wzdluz wiazania)"};
			static const char *const kPi[] = {"π_x", "π_y"};
			static const char *const kPiStar[] = {"π*_x", "π*_y"};
			static const char *const kDelta[] = {"δ_x²-y²", "δ_xy"};
			static const char *const kDeltaStar[] = {"δ*_x²-y²", "δ*_xy"};
			static const char *const kHybridSigma[] = {"σ z hybryd"};

			switch (preset)
			{
				case OrbitalPreset::S: return kS;
				case OrbitalPreset::P: return kP;
				case OrbitalPreset::D: return kD;
				case OrbitalPreset::F: return kF;
				case OrbitalPreset::Sp: return kSp;
				case OrbitalPreset::Sp2: return kSp2;
				case OrbitalPreset::Sp3: return kSp3;
				case OrbitalPreset::Sigma: return kSigma;
				case OrbitalPreset::SigmaStar: return kSigmaStar;
				case OrbitalPreset::Pi: return kPi;
				case OrbitalPreset::PiStar: return kPiStar;
				case OrbitalPreset::Delta: return kDelta;
				case OrbitalPreset::DeltaStar: return kDeltaStar;
				default: return kHybridSigma;
			}
		}
	} // namespace

	const char *OrbitalPresetDisplayName(OrbitalPreset preset)
	{
		constexpr std::array<const char *, 19> names = {
			"s", "p", "d", "f", "sp", "sp²", "sp³", "σ", "σ*", "π", "π*", "δ",
			"δ*", "sp-σ", "sp-σ*", "sp²-σ", "sp²-σ*", "sp³-σ", "sp³-σ*"};
		const int index = static_cast<int>(preset);
		return index >= 0 && index < static_cast<int>(names.size()) ? names[index] : "";
	}

	const char *OrbitalPresetMemberDisplayName(OrbitalPreset preset, int lobeIndex)
	{
		const std::span<const char *const> members = PresetMemberDisplays(preset);
		return members[static_cast<std::size_t>(
			std::clamp(lobeIndex, 0, static_cast<int>(members.size()) - 1))];
	}

	int OrbitalPresetMemberCount(OrbitalPreset preset)
	{
		return static_cast<int>(PresetMembers(preset).size());
	}

	const char *OrbitalPresetMemberName(OrbitalPreset preset, int lobeIndex)
	{
		const std::span<const char *const> members = PresetMembers(preset);
		return members[static_cast<std::size_t>(
			std::clamp(lobeIndex, 0, static_cast<int>(members.size()) - 1))];
	}

	const char *OrbitalPresetGroupName(OrbitalPresetGroup group)
	{
		switch (group)
		{
			case OrbitalPresetGroup::Atomic: return "Atomowe";
			case OrbitalPresetGroup::Hybrid: return "Hybrydy";
			case OrbitalPresetGroup::Bonding: return "Wiazace";
			case OrbitalPresetGroup::Antibonding: return "Antywiazace";
			case OrbitalPresetGroup::HybridBonding: return "Wiazania z hybryd";
			default: return "";
		}
	}

	OrbitalPresetGroup OrbitalPresetGroupOf(OrbitalPreset preset)
	{
		switch (preset)
		{
			case OrbitalPreset::S:
			case OrbitalPreset::P:
			case OrbitalPreset::D:
			case OrbitalPreset::F: return OrbitalPresetGroup::Atomic;
			case OrbitalPreset::Sp:
			case OrbitalPreset::Sp2:
			case OrbitalPreset::Sp3: return OrbitalPresetGroup::Hybrid;
			case OrbitalPreset::Sigma:
			case OrbitalPreset::Pi:
			case OrbitalPreset::Delta: return OrbitalPresetGroup::Bonding;
			case OrbitalPreset::SigmaStar:
			case OrbitalPreset::PiStar:
			case OrbitalPreset::DeltaStar: return OrbitalPresetGroup::Antibonding;
			case OrbitalPreset::SpSigma:
			case OrbitalPreset::SpSigmaStar:
			case OrbitalPreset::Sp2Sigma:
			case OrbitalPreset::Sp2SigmaStar:
			case OrbitalPreset::Sp3Sigma:
			case OrbitalPreset::Sp3SigmaStar: return OrbitalPresetGroup::HybridBonding;
			default: return OrbitalPresetGroup::Atomic;
		}
	}

	std::vector<OrbitalPreset> OrbitalPresetsInGroup(OrbitalPresetGroup group)
	{
		switch (group)
		{
			case OrbitalPresetGroup::Atomic:
				return {OrbitalPreset::S, OrbitalPreset::P, OrbitalPreset::D, OrbitalPreset::F};
			case OrbitalPresetGroup::Hybrid:
				return {OrbitalPreset::Sp, OrbitalPreset::Sp2, OrbitalPreset::Sp3};
			case OrbitalPresetGroup::Bonding:
				return {OrbitalPreset::Sigma, OrbitalPreset::Pi, OrbitalPreset::Delta};
			case OrbitalPresetGroup::Antibonding:
				return {OrbitalPreset::SigmaStar, OrbitalPreset::PiStar, OrbitalPreset::DeltaStar};
			case OrbitalPresetGroup::HybridBonding:
				return {
					OrbitalPreset::SpSigma, OrbitalPreset::SpSigmaStar,
					OrbitalPreset::Sp2Sigma, OrbitalPreset::Sp2SigmaStar,
					OrbitalPreset::Sp3Sigma, OrbitalPreset::Sp3SigmaStar};
			default: return {};
		}
	}

	std::vector<OrbitalPresetGroup> AllOrbitalPresetGroups()
	{
		return {
			OrbitalPresetGroup::Atomic,
			OrbitalPresetGroup::Hybrid,
			OrbitalPresetGroup::Bonding,
			OrbitalPresetGroup::Antibonding,
			OrbitalPresetGroup::HybridBonding};
	}
} // namespace DefectStudio
