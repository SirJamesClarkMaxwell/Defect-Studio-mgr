#include "Core/dspch.hpp"
#include "IO/VaspRunRecordIO.hpp"

#include <cmath>
#include <limits>
#include <sstream>

namespace DefectStudio::VaspRunRecordIO
{
	namespace
	{
		std::string Trim(std::string_view text)
		{
			const auto first = text.find_first_not_of(" \t\r\n");
			return first == std::string_view::npos ? std::string{} : std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
		}

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		std::string Value(std::string_view line, std::string_view key)
		{
			const auto at = line.find(key);
			if (at == std::string_view::npos || (at > 0 && (std::isalnum(static_cast<unsigned char>(line[at - 1])) || line[at - 1] == '_')))
				return {};
			const auto rest = line.substr(at + key.size());
			const auto equal = rest.find_first_not_of(" \t");
			return equal != std::string_view::npos && rest[equal] == '=' ? Trim(rest.substr(equal + 1)) : std::string{};
		}

		std::optional<double> Number(const std::string &text)
		{
			std::istringstream stream(text);
			double value = 0.0;
			if (!(stream >> value) || !std::isfinite(value))
				return std::nullopt;
			return value;
		}

		std::string Word(const std::string &text)
		{
			std::istringstream stream(text);
			std::string word;
			stream >> word;
			return word;
		}

		std::string Species(const std::string &title)
		{
			std::istringstream stream(title);
			std::string family, species;
			stream >> family >> species;
			return species.substr(0, species.find('_'));
		}

		bool RegularFile(const FilePath &path)
		{
			std::error_code error;
			return std::filesystem::is_regular_file(path, error);
		}
	}

	void ParseOutcar(std::string_view head, std::string_view tail, VaspRunRecord &record)
	{
		record.species.clear();
		record.ions.clear();
		record.valence.clear();
		record.electrons.reset();
		record.totalEnergy.reset();
		record.finished = record.relaxation = record.ionicConverged = false;
		record.method = {};
		record.error.clear();
		std::vector<std::string> potcars;
		std::vector<double> perPotcarValence, summaryValence;
		int nsw = 0, ibrion = -1;
		std::istringstream lines{std::string(head)};
		std::string line;
		while (std::getline(lines, line))
		{
			if (const auto title = Value(line, "TITEL"); !title.empty())
				record.method.potcars.push_back(title);
			if (const auto at = line.find("POTCAR:"); at != std::string::npos)
				potcars.push_back(Trim(std::string_view(line).substr(at + 7)));
			if (const auto counts = Value(line, "ions per type"); !counts.empty())
			{
				record.ions.clear();
				std::istringstream stream(counts);
				int count;
				while (stream >> count)
					record.ions.push_back(count);
			}
			if (const auto value = Value(line, "ZVAL"); !value.empty())
			{
				std::istringstream stream(value);
				std::vector<double> values;
				double number;
				while (stream >> number && std::isfinite(number))
					values.push_back(number);
				if (line.find("POMASS") != std::string::npos && !values.empty())
					perPotcarValence.push_back(values.front());
				else
					summaryValence = std::move(values);
			}
			if (const auto value = Number(Value(line, "NELECT"))) record.electrons = *value;
			if (const auto value = Number(Value(line, "ENCUT"))) record.method.encut = *value;
			if (const auto value = Number(Value(line, "HFSCREEN"))) record.method.hfscreen = *value;
			if (const auto value = Number(Value(line, "AEXX"))) record.method.aexx = *value;
			if (const auto value = Number(Value(line, "ISPIN"))) record.method.ispin = static_cast<int>(*value);
			if (const auto value = Number(Value(line, "NSW"))) nsw = static_cast<int>(*value);
			if (const auto value = Number(Value(line, "IBRION"))) ibrion = static_cast<int>(*value);
			if (const auto value = Word(Value(line, "PREC")); !value.empty()) record.method.prec = Lower(value);
			if (const auto value = Word(Value(line, "PRECFOCK")); !value.empty()) record.method.precFock = Lower(value);
			if (const auto value = Lower(Word(Value(line, "LHFCALC"))); !value.empty()) record.method.hybrid = value == "t" || value == ".true.";
		}
		if (record.method.potcars.empty() && potcars.size() >= record.ions.size())
			record.method.potcars.assign(potcars.begin(), potcars.begin() + record.ions.size());
		for (const auto &title : record.method.potcars)
			record.species.push_back(Species(title));
		record.valence = summaryValence.size() == record.ions.size() ? std::move(summaryValence) : std::move(perPotcarValence);
		if (!record.method.hybrid)
			record.method.precFock.clear();
		record.relaxation = nsw > 0 && (ibrion == 1 || ibrion == 2 || ibrion == 3);
		record.finished = tail.find("General timing and accounting") != std::string_view::npos;
		record.ionicConverged = tail.find("reached required accuracy") != std::string_view::npos;
		std::istringstream tailLines{std::string(tail)};
		while (std::getline(tailLines, line))
			// "free  energy   TOTEN" closes an ionic step; "free energy    TOTEN" is one SCF iteration
			// (the last line of a run cut off mid-step, which is not an energy of a finished step).
			if (line.find("free  energy   TOTEN") != std::string::npos)
				if (const auto energy = Number(Value(line, "TOTEN"))) record.totalEnergy = *energy;
		if (record.method.potcars.empty() || record.ions.empty())
			record.error = "OUTCAR has no POTCAR titles or ion counts";
	}

	VaspRunRecord Read(const Path &directory)
	{
		VaspRunRecord record;
		record.directory = directory;
		record.hasLocpot = RegularFile((directory / "LOCPOT").Native());
		record.hasContcar = RegularFile((directory / "CONTCAR").Native());
		const auto path = (directory / "OUTCAR").Native();
		std::error_code error;
		record.outcarSize = std::filesystem::file_size(path, error);
		if (error)
		{
			record.outcarSize = 0;
			record.error = "Cannot read OUTCAR: " + error.message();
			return record;
		}
		const auto time = std::filesystem::last_write_time(path, error);
		if (error)
		{
			record.error = "Cannot read OUTCAR write time: " + error.message();
			return record;
		}
		record.outcarWriteTime = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
		std::ifstream stream(path, std::ios::binary);
		const auto headSize = static_cast<std::size_t>(std::min<std::uintmax_t>(record.outcarSize, kHeadBytes));
		const auto tailSize = static_cast<std::size_t>(std::min<std::uintmax_t>(record.outcarSize, kTailBytes));
		std::string head(headSize, '\0'), tail(tailSize, '\0');
		stream.read(head.data(), static_cast<std::streamsize>(headSize));
		stream.seekg(static_cast<std::streamoff>(record.outcarSize - tailSize));
		stream.read(tail.data(), static_cast<std::streamsize>(tailSize));
		if (!stream)
			record.error = "Cannot read OUTCAR head and tail";
		else
			ParseOutcar(head, tail, record);
		return record;
	}

	bool IsUpToDate(const VaspRunRecord &record)
	{
		if (!record.error.empty())
			return false;
		const auto path = (record.directory / "OUTCAR").Native();
		std::error_code error;
		const auto size = std::filesystem::file_size(path, error);
		if (error || size != record.outcarSize)
			return false;
		const auto time = std::filesystem::last_write_time(path, error);
		return !error && std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count() == record.outcarWriteTime;
	}

	std::vector<Path> FindRuns(const Path &root, const std::function<bool()> &cancelled)
	{
		std::vector<Path> runs;
		if (cancelled && cancelled())
			return runs;
		std::error_code error;
		const auto status = std::filesystem::symlink_status(root.Native(), error);
		if (error || !std::filesystem::is_directory(status))
			return runs;
		if (RegularFile((root / "OUTCAR").Native()))
			runs.push_back(root);
		std::filesystem::recursive_directory_iterator it(root.Native(), std::filesystem::directory_options::skip_permission_denied, error), end;
		while (!error && it != end)
		{
			if (cancelled && cancelled())
				break;
			const auto entryStatus = it->symlink_status(error);
			if (!error && std::filesystem::is_directory(entryStatus) && RegularFile(it->path() / "OUTCAR"))
				runs.push_back(Path::FromResolved(it->path()));
			error.clear();
			it.increment(error);
		}
		std::sort(runs.begin(), runs.end(), [](const auto &a, const auto &b) { return a.Native() < b.Native(); });
		return runs;
	}

	std::optional<BandEdges> ParseEigenval(std::string_view text)
	{
		std::istringstream lines{std::string(text)};
		std::string line;
		if (!std::getline(lines, line))
			return std::nullopt;
		std::istringstream header(line);
		int first, second, third, spin;
		if (!(header >> first >> second >> third >> spin) || (spin != 1 && spin != 2))
			return std::nullopt;
		for (int i = 0; i < 5; ++i)
			if (!std::getline(lines, line)) return std::nullopt;
		std::istringstream counts(line);
		double electrons;
		int kpoints, bands;
		if (!(counts >> electrons >> kpoints >> bands) || !std::isfinite(electrons) || kpoints <= 0 || bands <= 0)
			return std::nullopt;
		BandEdges edges{-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
		for (int k = 0; k < kpoints; ++k)
		{
			if (!std::getline(lines, line) || !Trim(line).empty() || !std::getline(lines, line))
				return std::nullopt;
			std::istringstream point(line);
			double x, y, z, weight;
			if (!(point >> x >> y >> z >> weight) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(weight))
				return std::nullopt;
			for (int b = 1; b <= bands; ++b)
			{
				if (!std::getline(lines, line)) return std::nullopt;
				std::istringstream band(line);
				int index;
				std::array<double, 2> energy{}, occupation{};
				if (!(band >> index) || index != b) return std::nullopt;
				for (int s = 0; s < spin; ++s)
					if (!(band >> energy[s]) || !std::isfinite(energy[s])) return std::nullopt;
				for (int s = 0; s < spin; ++s)
					if (!(band >> occupation[s]) || !std::isfinite(occupation[s])) return std::nullopt;
				std::string extra;
				if (band >> extra) return std::nullopt;
				for (int s = 0; s < spin; ++s)
					if (occupation[s] > 0.5) edges.vbm = std::max(edges.vbm, energy[s]);
					else edges.cbm = std::min(edges.cbm, energy[s]);
			}
		}
		return std::isfinite(edges.vbm) && std::isfinite(edges.cbm) ? std::optional<BandEdges>(edges) : std::nullopt;
	}

	std::optional<BandEdges> ReadBandEdges(const Path &directory)
	{
		std::ifstream stream((directory / "EIGENVAL").Native(), std::ios::binary);
		std::ostringstream text;
		text << stream.rdbuf();
		if (!stream || text.bad())
			return std::nullopt;
		return ParseEigenval(text.str());
	}
}
