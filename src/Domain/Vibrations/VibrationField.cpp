#include "Core/dspch.hpp"

#include "Domain/Vibrations/VibrationField.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace DefectStudio
{
	namespace
	{
		bool Finite(const glm::dvec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool ValidVectors(const VibrationField &field)
		{
			return (field.imag.empty() || field.imag.size() == field.real.size()) &&
				std::all_of(field.real.begin(), field.real.end(), Finite) &&
				std::all_of(field.imag.begin(), field.imag.end(), Finite);
		}

		glm::dvec3 Direction(const VibrationField &field, std::size_t atom, double sine, double cosine)
		{
			return field.real[atom] * sine + (field.imag.empty() ? glm::dvec3(0.0) : field.imag[atom] * cosine);
		}

		bool SameMultiplet(const PhononMode &first, const PhononMode &second, double toleranceMeV)
		{
			if (first.isTranslation || second.isTranslation)
				return false;
			// Two different E pairs can sit next to each other, so the irrep alone is not enough.
			if ((!first.irrep.empty() || !second.irrep.empty()) && first.irrep != second.irrep)
				return false;
			return std::isfinite(first.frequencyMeV) && std::isfinite(second.frequencyMeV) &&
				std::abs(first.frequencyMeV - second.frequencyMeV) <= toleranceMeV;
		}
	} // namespace

	double FieldCycleMaximum(const VibrationField &field)
	{
		if (!ValidVectors(field))
			return 0.0;
		double maximum = 0.0;
		for (std::size_t atom = 0; atom < field.real.size(); ++atom)
		{
			const auto &real = field.real[atom];
			const auto imag = field.imag.empty() ? glm::dvec3(0.0) : field.imag[atom];
			const double scale = std::max({std::abs(real.x), std::abs(real.y), std::abs(real.z),
				std::abs(imag.x), std::abs(imag.y), std::abs(imag.z)});
			if (scale == 0.0)
				continue;
			const auto r = real / scale, i = imag / scale;
			const double a = glm::dot(r, r), b = glm::dot(r, i), c = glm::dot(i, i);
			// Largest eigenvalue of the 2x2 Gram matrix of the two quadratures.
			const double axis = scale * std::sqrt(0.5 * (a + c + std::hypot(a - c, 2.0 * b)));
			if (!std::isfinite(axis))
				return 0.0;
			maximum = std::max(maximum, axis);
		}
		return maximum;
	}

	std::vector<glm::dvec3> FieldDisplacements(const VibrationField &field, double phaseRadians,
		double amplitudeAngstrom)
	{
		return FieldDisplacements(field, phaseRadians, amplitudeAngstrom, 1.0);
	}

	std::vector<glm::dvec3> FieldDisplacements(const VibrationField &field, double phaseRadians,
		double amplitudeAngstrom, double extent)
	{
		std::vector<glm::dvec3> displacements(field.real.size(), glm::dvec3(0.0));
		if (field.motion == VibrationMotion::Sweep)
		{
			if (!ValidVectors(field) || !std::isfinite(phaseRadians) || !std::isfinite(extent) || extent < 0.0)
				return displacements;
			const double fraction = extent * (1.0 - std::cos(phaseRadians)) / 2.0;
			for (std::size_t atom = 0; atom < displacements.size(); ++atom)
				displacements[atom] = field.real[atom] * fraction;
			return displacements;
		}
		const double maximum = FieldCycleMaximum(field);
		if (maximum <= 0.0 || !std::isfinite(phaseRadians) || !std::isfinite(amplitudeAngstrom))
			return displacements;
		const double sine = std::sin(phaseRadians), cosine = std::cos(phaseRadians);
		for (std::size_t atom = 0; atom < displacements.size(); ++atom)
			displacements[atom] = Direction(field, atom, sine, cosine) * (amplitudeAngstrom / maximum);
		return displacements;
	}

	std::vector<ModeArrow> FieldArrows(const VibrationField &field, std::optional<double> phaseRadians,
		double participationThreshold, double maxLengthAngstrom)
	{
		const auto atoms = field.real.size();
		if (atoms == 0 || !ValidVectors(field) || (!field.participation.empty() &&
			field.participation.size() != atoms) || (phaseRadians && !std::isfinite(*phaseRadians)) ||
			!std::isfinite(participationThreshold) || !std::isfinite(maxLengthAngstrom) || maxLengthAngstrom < 0.0 ||
			std::any_of(field.participation.begin(), field.participation.end(), [](double value) {
				return !std::isfinite(value) || value < 0.0;
			}))
			return {};
		const double sine = phaseRadians ? (field.motion == VibrationMotion::Sweep ?
			(1.0 - std::cos(*phaseRadians)) / 2.0 : std::sin(*phaseRadians)) : 1.0;
		const double cosine = phaseRadians && field.motion == VibrationMotion::Oscillate ? std::cos(*phaseRadians) : 0.0;
		std::vector<glm::dvec3> directions;
		directions.reserve(atoms);
		double longest = 0.0;
		for (std::size_t atom = 0; atom < atoms; ++atom)
		{
			const auto direction = Direction(field, atom, sine, cosine);
			const double length = glm::length(direction);
			if (!std::isfinite(length))
				return {};
			directions.push_back(direction);
			longest = std::max(longest, length);
		}
		std::vector<ModeArrow> arrows;
		for (std::size_t atom = 0; atom < atoms; ++atom)
		{
			const double participation = field.participation.empty() ? 1.0 / atoms : field.participation[atom];
			if (participation >= participationThreshold)
				arrows.push_back({atom, longest > 0.0 ? directions[atom] * (maxLengthAngstrom / longest) :
					glm::dvec3(0.0), participation});
		}
		return arrows;
	}

	VibrationField DefectModeField(const PhononAnalysisResult &result, std::size_t mode,
		std::optional<std::size_t> partner, double mixAngleRadians)
	{
		const auto atoms = result.AtomCount();
		const auto validMode = [&](std::size_t index) {
			return index < result.ModeCount() && !result.modes[index].isTranslation &&
				index < result.eigenvectors.size() / 3 / atoms;
		};
		if (atoms == 0 || result.positionsAngstrom.size() != atoms || result.massesAmu.size() != atoms ||
			!std::isfinite(mixAngleRadians) || !validMode(mode) || (partner && !validMode(*partner)))
			return {};
		VibrationField field;
		field.symbols = result.symbols;
		field.equilibriumAngstrom = result.positionsAngstrom;
		field.real.reserve(atoms);
		field.participation.reserve(atoms);
		const double cosine = partner ? std::cos(mixAngleRadians) : 1.0;
		const double sine = partner ? std::sin(mixAngleRadians) : 0.0;
		for (std::size_t atom = 0; atom < atoms; ++atom)
		{
			const double mass = result.massesAmu[atom];
			if (!std::isfinite(mass) || mass <= 0.0 || !Finite(field.equilibriumAngstrom[atom]))
				return {};
			const auto eigenvector = [&](std::size_t index) {
				const auto offset = (index * atoms + atom) * 3;
				return glm::dvec3(result.eigenvectors[offset], result.eigenvectors[offset + 1], result.eigenvectors[offset + 2]);
			};
			const auto e = cosine * eigenvector(mode) + (partner ? sine * eigenvector(*partner) : glm::dvec3(0.0));
			const auto displacement = e / std::sqrt(mass);
			if (!Finite(displacement))
				return {};
			field.real.push_back(displacement);
			// Preserve the stored float32 participation at theta=0, including threshold boundaries.
			const double participation = (!partner || mixAngleRadians == 0.0) &&
				mode < result.participation.size() / atoms ? result.participation[mode * atoms + atom] : glm::dot(e, e);
			if (!std::isfinite(participation) || participation < 0.0)
				return {};
			field.participation.push_back(participation);
		}
		const auto &selected = result.modes[mode];
		const auto irrep = selected.irrep.empty() ? "" : " · " + selected.irrep;
		field.label = partner ? std::format("mod {}/{}{} · θ {:.1f}°", selected.index,
			result.modes[*partner].index, irrep, mixAngleRadians * 180.0 / std::numbers::pi) :
			std::format("mod {}{} · {:.1f} meV", selected.index, irrep, selected.frequencyMeV);
		if (partner)
		{
			std::size_t first = mode, last = mode;
			while (first > 0 && SameMultiplet(selected, result.modes[first - 1], 0.5)) --first;
			while (last + 1 < result.ModeCount() && SameMultiplet(selected, result.modes[last + 1], 0.5)) ++last;
			// ponytail: two-member mixing only; extend to a multiplet basis when more angles are needed.
			if (last - first + 1 > 2) field.label += " · mieszanie tylko dwóch modów";
		}
		return field;
	}

	VibrationField CcdDirectionField(const CcdDisplacement &displacement)
	{
		const auto atoms = displacement.symbols.size();
		if (atoms == 0 || displacement.masses.size() != atoms || displacement.gsPositions.size() != atoms ||
			displacement.displacement.size() != atoms || !std::isfinite(displacement.deltaQ) || displacement.deltaQ <= 0.0)
			return {};
		VibrationField field;
		field.symbols = displacement.symbols;
		field.equilibriumAngstrom = displacement.gsPositions;
		field.real = displacement.displacement;
		field.motion = VibrationMotion::Sweep;
		double total = 0.0;
		for (std::size_t atom = 0; atom < atoms; ++atom)
		{
			const double mass = displacement.masses[atom];
			if (field.symbols[atom].empty() || !std::isfinite(mass) || mass <= 0.0 ||
				!Finite(field.real[atom]) || !Finite(field.equilibriumAngstrom[atom]))
				return {};
			const double weight = mass * glm::dot(field.real[atom], field.real[atom]);
			total += weight;
			field.participation.push_back(weight / (displacement.deltaQ * displacement.deltaQ));
		}
		if (!std::isfinite(total) || std::abs(std::sqrt(total) - displacement.deltaQ) > 1e-6 * displacement.deltaQ)
			return {};
		field.label = std::format("kierunek ΔQ · ΔQ {:.2f} amu^½ Å", displacement.deltaQ);
		return field;
	}

	VibrationField BulkModeField(const PhononModeField &mode, double mixAngleRadians)
	{
		const auto atoms = mode.symbols.size();
		if (atoms == 0 || mode.positions.size() != atoms || mode.real.size() != atoms || mode.imag.size() != atoms ||
			(mode.partner && (!std::isfinite(mixAngleRadians) ||
			mode.partner->real.size() != atoms || mode.partner->imag.size() != atoms)))
			return {};
		VibrationField field;
		field.symbols = mode.symbols;
		field.equilibriumAngstrom = mode.positions;
		const double cosine = mode.partner ? std::cos(mixAngleRadians) : 1.0;
		const double sine = mode.partner ? std::sin(mixAngleRadians) : 0.0;
		double total = 0.0;
		for (std::size_t atom = 0; atom < atoms; ++atom)
		{
			const auto real = cosine * mode.real[atom] + (mode.partner ? sine * mode.partner->real[atom] : glm::dvec3(0.0));
			const auto imag = cosine * mode.imag[atom] + (mode.partner ? sine * mode.partner->imag[atom] : glm::dvec3(0.0));
			if (!Finite(real) || !Finite(imag) || !Finite(mode.positions[atom])) return {};
			field.real.push_back(real);
			field.imag.push_back(imag);
			const double weight = glm::dot(real, real) + glm::dot(imag, imag);
			field.participation.push_back(weight);
			total += weight;
		}
		if (!std::isfinite(total) || total <= 0.0) return {};
		for (auto &weight : field.participation) weight /= total;
		field.label = std::format("bulk · {} · ℏω {:.1f} meV", mode.label, mode.frequencyMeV);
		return field;
	}

	std::optional<std::size_t> DegeneratePartner(const PhononAnalysisResult &result, std::size_t mode,
		double toleranceMeV)
	{
		if (mode >= result.ModeCount() || result.modes[mode].isTranslation ||
			!std::isfinite(toleranceMeV) || toleranceMeV < 0.0)
			return std::nullopt;
		const auto gap = [&](std::size_t other) { return std::abs(result.modes[mode].frequencyMeV - result.modes[other].frequencyMeV); };
		const bool next = mode + 1 < result.ModeCount() && SameMultiplet(result.modes[mode], result.modes[mode + 1], toleranceMeV);
		const bool previous = mode > 0 && SameMultiplet(result.modes[mode], result.modes[mode - 1], toleranceMeV);
		if (next && (!previous || gap(mode + 1) <= gap(mode - 1)))
			return mode + 1;
		if (previous)
			return mode - 1;
		return std::nullopt;
	}
} // namespace DefectStudio
