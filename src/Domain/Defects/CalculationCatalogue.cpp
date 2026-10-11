#include "Core/dspch.hpp"
#include "Domain/Defects/CalculationCatalogue.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <regex>
#include <sstream>

namespace DefectStudio
{
	namespace
	{
		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		std::string PotcarSpecies(const std::string &title)
		{
			std::istringstream stream(title);
			std::string family, species;
			stream >> family >> species;
			return species.substr(0, species.find('_'));
		}

		void SortRuns(std::vector<std::size_t> &indices, std::span<const VaspRunRecord> records)
		{
			std::sort(indices.begin(), indices.end(), [&](auto a, auto b) { return records[a].directory.Native() < records[b].directory.Native(); });
		}
	}

	std::optional<int> CalculationCharge(const VaspRunRecord &record)
	{
		if (!record.electrons || record.ions.empty() || record.ions.size() != record.valence.size())
			return std::nullopt;
		double charge = -*record.electrons;
		for (std::size_t i = 0; i < record.ions.size(); ++i)
		{
			if (record.ions[i] < 0 || !std::isfinite(record.valence[i]))
				return std::nullopt;
			charge += record.ions[i] * record.valence[i];
		}
		const double rounded = std::round(charge);
		if (!std::isfinite(charge) || std::abs(charge - rounded) > 1e-3 ||
			rounded < std::numeric_limits<int>::min() || rounded > std::numeric_limits<int>::max())
			return std::nullopt;
		return static_cast<int>(rounded);
	}

	SpeciesCounts CalculationComposition(const VaspRunRecord &record)
	{
		SpeciesCounts counts;
		if (record.species.size() != record.ions.size())
			return counts;
		for (std::size_t i = 0; i < record.species.size(); ++i)
			counts[record.species[i]] += record.ions[i];
		std::erase_if(counts, [](const auto &entry) { return entry.second == 0; });
		return counts;
	}

	bool CalculationUsable(const VaspRunRecord &record)
	{
		return record.error.empty() && record.finished && record.totalEnergy && std::isfinite(*record.totalEnergy) &&
			(!record.relaxation || record.ionicConverged);
	}

	std::vector<std::string> MethodMismatches(const CalculationMethod &a, const CalculationMethod &b)
	{
		std::vector<std::string> result;
		const auto compare = [&](std::string_view name, const auto &left, const auto &right, bool same) {
			if (!same)
			{
				std::ostringstream text;
				text << name << ' ' << left << " vs " << right;
				result.push_back(text.str());
			}
		};
		compare("LHFCALC", a.hybrid, b.hybrid, a.hybrid == b.hybrid);
		compare("HFSCREEN", a.hfscreen, b.hfscreen, std::abs(a.hfscreen - b.hfscreen) <= 1e-6);
		compare("AEXX", a.aexx, b.aexx, std::abs(a.aexx - b.aexx) <= 1e-6);
		compare("PRECFOCK", a.precFock, b.precFock, Lower(a.precFock) == Lower(b.precFock));
		compare("ENCUT", a.encut, b.encut, std::abs(a.encut - b.encut) <= 1e-6);
		compare("PREC", a.prec, b.prec, Lower(a.prec) == Lower(b.prec));
		for (const auto &left : a.potcars)
			for (const auto &right : b.potcars)
				if (PotcarSpecies(left) == PotcarSpecies(right) && left != right)
					compare("POTCAR", left, right, false);
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	std::optional<int> ChargeFromFolderName(std::string_view name)
	{
		static const std::regex pattern("^(q|stan_)([+-]?)([0-9]+)([+-]?)$", std::regex::icase);
		const std::string folder(name);
		std::smatch match;
		if (!std::regex_match(folder, match, pattern))
			return std::nullopt;
		const std::string before = match[2].str(), after = match[4].str();
		if ((!before.empty() && !after.empty()) || (match[1].length() == 1 && !after.empty()))
			return std::nullopt;
		const std::string sign = before.empty() ? after : before;
		const std::string number = (sign == "-" ? "-" : "") + match[3].str();
		int charge = 0;
		const auto parsed = std::from_chars(number.data(), number.data() + number.size(), charge);
		if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size())
			return std::nullopt;
		return charge;
	}

	std::vector<DefectRunGroup> GroupDefectRuns(std::span<const VaspRunRecord> records,
		std::size_t bulk, const Path &root, int maxChangedAtoms)
	{
		const std::array roots{root};
		return GroupDefectRuns(records, bulk, roots, maxChangedAtoms);
	}

	std::optional<Path> CalculationRoot(const Path &directory, std::span<const Path> roots)
	{
		std::optional<Path> found;
		std::ptrdiff_t depth = -1;
		if (directory.Empty()) return found;
		for (const Path &root : roots)
		{
			if (root.Empty()) continue;
			auto native = root.Native().lexically_normal();
			if (native.filename().empty()) native = native.parent_path();
			const auto relative = directory.Native().lexically_normal().lexically_relative(native);
			if (relative.empty() || relative.is_absolute() ||
				std::any_of(relative.begin(), relative.end(), [](const auto &part) { return part == ".."; }))
				continue;
			const auto candidateDepth = std::distance(native.begin(), native.end());
			if (candidateDepth > depth)
			{
				depth = candidateDepth;
				found = Path::FromResolved(std::move(native));
			}
		}
		return found;
	}

	std::vector<DefectRunGroup> GroupDefectRuns(std::span<const VaspRunRecord> records,
		std::size_t bulk, std::span<const Path> roots, int maxChangedAtoms)
	{
		std::vector<DefectRunGroup> groups;
		if (bulk >= records.size() || roots.empty() || maxChangedAtoms < 0)
			return groups;
		const auto host = CalculationComposition(records[bulk]);
		if (host.empty())
			return groups;
		for (std::size_t i = 0; i < records.size(); ++i)
		{
			if (i == bulk)
				continue;
			const auto root = CalculationRoot(records[i].directory, roots);
			if (!root)
				continue;
			const auto relative = records[i].directory.Native().lexically_normal().lexically_relative(root->Native());
			if (relative == ".")
				continue;
			const auto charge = CalculationCharge(records[i]);
			if (!charge)
				continue;
			auto difference = CalculationComposition(records[i]);
			if (difference.empty())
				continue;
			for (const auto &[species, count] : host)
				difference[species] -= count;
			std::erase_if(difference, [](const auto &entry) { return entry.second == 0; });
			std::int64_t changed = 0;
			for (const auto &[species, count] : difference)
				changed += std::abs(static_cast<std::int64_t>(count));
			if (changed == 0 || changed > maxChangedAtoms)
				continue;
			const Path first = Path::FromResolved(*relative.begin());
			const std::string defect = ChargeFromFolderName(first.Utf8()) ? root->filename().Utf8() : first.Utf8();
			auto found = std::find_if(groups.begin(), groups.end(), [&](const auto &group) { return group.defect == defect && group.charge == *charge; });
			if (found == groups.end())
				groups.push_back({defect, *charge, std::move(difference), {i}});
			else
				found->runs.push_back(i);
		}
		std::sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) {
			return a.defect == b.defect ? a.charge > b.charge : a.defect < b.defect;
		});
		for (auto &group : groups)
			SortRuns(group.runs, records);
		return groups;
	}

	std::vector<std::size_t> BulkCandidates(std::span<const VaspRunRecord> records)
	{
		std::vector<std::size_t> largest;
		std::int64_t maximum = 0;
		for (std::size_t i = 0; i < records.size(); ++i)
		{
			if (!CalculationUsable(records[i]))
				continue;
			const auto count = std::accumulate(records[i].ions.begin(), records[i].ions.end(), std::int64_t{0});
			if (count > maximum)
			{
				maximum = count;
				largest.clear();
			}
			if (count == maximum && count > 0)
				largest.push_back(i);
		}
		if (largest.empty())
			return {};
		auto common = CalculationComposition(records[largest.front()]);
		for (const auto i : largest)
		{
			const auto composition = CalculationComposition(records[i]);
			std::erase_if(common, [&](const auto &entry) { return !composition.contains(entry.first); });
		}
		std::erase_if(largest, [&](auto i) {
			const auto composition = CalculationComposition(records[i]);
			return CalculationCharge(records[i]) != std::optional<int>(0) || composition.empty() ||
				std::any_of(composition.begin(), composition.end(), [&](const auto &entry) { return !common.contains(entry.first); });
		});
		SortRuns(largest, records);
		return largest;
	}

	RunProposal ProposeEnergyRun(std::span<const VaspRunRecord> records, const DefectRunGroup &group, std::size_t bulk)
	{
		std::vector<std::size_t> candidates;
		for (auto i : group.runs)
			if (i < records.size() && CalculationUsable(records[i]))
				candidates.push_back(i);
		if (candidates.empty())
			return {std::nullopt, "brak zakończonego obliczenia"};
		const auto prefer = [&](const auto &predicate) {
			if (std::any_of(candidates.begin(), candidates.end(), predicate))
				std::erase_if(candidates, [&](auto i) { return !predicate(i); });
		};
		if (bulk < records.size())
			prefer([&](auto i) { return MethodMismatches(records[i].method, records[bulk].method).empty(); });
		prefer([&](auto i) { const auto prec = Lower(records[i].method.precFock); return prec == "normal" || prec == "accurate"; });
		prefer([&](auto i) { return records[i].hasLocpot; });
		if (candidates.size() != 1)
			return {std::nullopt, "wybierz: " + std::to_string(candidates.size()) + " równorzędne"};
		const auto i = candidates.front();
		std::string reason = records[i].method.hybrid ? "zbieżne HSE" : "zbieżne PBE";
		if (!records[i].method.precFock.empty())
			reason += ", PRECFOCK " + Lower(records[i].method.precFock);
		if (bulk < records.size() && MethodMismatches(records[i].method, records[bulk].method).empty())
			reason += ", zgodne z bulkiem";
		if (records[i].hasLocpot)
			reason += ", z LOCPOT";
		return {i, std::move(reason)};
	}
}
