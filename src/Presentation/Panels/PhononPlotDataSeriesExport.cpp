#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <set>
#include "Core/Utils/Uuid.hpp"

namespace DefectStudio
{
	namespace
	{
		double Interpolate(std::span<const double> x, std::span<const double> y, double value)
		{
			const auto missing = std::numeric_limits<double>::quiet_NaN();
			if (x.empty() || x.size() != y.size() || !std::isfinite(value)) return missing;
			// Match numpy.interp in lineshape.py, including the constant endpoints.
			if (value <= x.front()) return y.front();
			if (value >= x.back()) return y.back();
			const auto upper = static_cast<std::size_t>(std::upper_bound(x.begin(), x.end(), value) - x.begin());
			const auto lower = upper - 1;
			return std::lerp(y[lower], y[upper], (value - x[lower]) / (x[upper] - x[lower]));
		}
	}

	std::vector<DataTableColumn> PhononSeriesColumns(const PhononConvergenceSeries &series, std::string_view preferredZpl)
	{
		if (series.members.empty()) return {};
		const auto &first = series.members.front().result.get();
		const auto firstZpl = SelectPhononPlCurve(first, preferredZpl);
		std::vector<double> relative;
		if (firstZpl.zplEv)
			for (const auto energy : first.plPhotonEnergyEv) relative.push_back(1000.0 * (energy - *firstZpl.zplEv));
		const auto info = series.label + std::format(" ZPL={} eV; E-E_ZPL grid: first member ({}) PL grid, shifted by each member's ZPL; ZPL source={}",
			firstZpl.zplEv ? std::format("{:g}", *firstZpl.zplEv) : "missing", series.members.front().label, firstZpl.choice);
		std::vector<DataTableColumn> columns = {{"E-E_ZPL", "meV", info, relative, {}}, {"E", "eV", "first member photon energy", firstZpl.zplEv ? first.plPhotonEnergyEv : std::vector<double>{}, {}}};
		for (const auto &member : series.members)
		{
			const auto &result = member.result.get();
			const auto selected = SelectPhononPlCurve(result, preferredZpl);
			DataTableColumn column{"L_" + member.label, "1/eV", std::format("S={:.3f}", result.sTotal), {}, {}};
			const auto &curve = PhononPanelSession::PlCurve(result, selected.choice);
			for (const auto energy : relative)
				column.values.push_back(selected.zplEv ? Interpolate(result.plPhotonEnergyEv, curve, *selected.zplEv + energy / 1000.0) : std::numeric_limits<double>::quiet_NaN());
			columns.push_back(std::move(column));
		}
		columns.push_back({"hw", "meV", "first member spectral grid; other members interpolated", first.spectralEnergyMeV, {}});
		for (const auto &member : series.members)
		{
			const auto &result = member.result.get();
			DataTableColumn column{"S(hw)_" + member.label, "1/meV", std::format("S={:.3f}", result.sTotal), {}, {}};
			for (const auto energy : first.spectralEnergyMeV)
				column.values.push_back(Interpolate(result.spectralEnergyMeV, result.spectralDensityPerMeV, energy));
			columns.push_back(std::move(column));
		}
		// lineshape.py sorts the scalar keys alphabetically, before the S_<irrep> keys.
		const auto scalarStart = columns.size();
		for (const auto &column : std::vector<DataTableColumn>{{"n", "", "a when a=b; otherwise a*b*c; unknown FC cell=empty", {}, {}},
			{"N_atoms", "", "loaded structure atom count", {}, {}}, {"DW", "", "", {}, {}}, {"E_FC", "meV", "", {}, {}},
			{"S", "", "", {}, {}}, {"hw_eff", "meV", "sum(S_k*hw_k)/S, hw_k>0", {}, {}}}) columns.push_back(column);
		std::set<std::string> irreps;
		for (const auto &member : series.members)
			for (const auto &[irrep, value] : member.irrepS) irreps.insert(irrep);
		for (const auto &irrep : irreps) columns.push_back({"S_" + irrep, "", "positive-frequency modes", {}, {}});
		const double missing = std::numeric_limits<double>::quiet_NaN();
		for (const auto &member : series.members)
		{
			const auto &result = member.result.get();
			const std::array values = {result.supercell ? member.n : missing,
				result.symbols.empty() ? missing : static_cast<double>(result.AtomCount()), result.debyeWaller,
				member.franckCondonMeV.value_or(missing), result.sTotal, member.effectiveMeV.value_or(missing)};
			for (std::size_t i = 0; i < values.size(); ++i) columns[scalarStart + i].values.push_back(values[i]);
			std::size_t i = scalarStart + values.size();
			for (const auto &irrep : irreps)
				columns[i++].values.push_back(!member.franckCondonMeV ? missing : member.irrepS.contains(irrep) ? member.irrepS.at(irrep) : 0.0);
		}
		return columns;
	}

	std::string PhononSeriesFileName(const PhononConvergenceSeries &series)
	{
		if (series.members.empty()) return "PL.dat";
		const auto &result = series.members.front().result.get();
		std::string name = "PL_" + (result.hrMethod.empty() ? "legacy" : result.hrMethod);
		if (result.hrCutoffAngstrom) name += std::format("_cutoff{:g}A", *result.hrCutoffAngstrom);
		name += std::format("_sig{:g}", result.sigmaMeV);
		if (result.sigmaMaxMeV) name += std::format("-{:g}", *result.sigmaMaxMeV);
		name += std::format("_gam{:g}", result.gammaMeV);
		for (auto &ch : name)
			if (static_cast<unsigned char>(ch) < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) ch = '_';
		return name + ".dat";
	}

	bool SavePhononSeriesOrigin(const PhononConvergenceSeries &series, std::string_view preferredZpl,
		const Path &project, Path &outFile, std::string &outError)
	{
		outFile = {}; outError.clear();
		if (project.Empty() || series.members.empty()) { outError = "Otwórz projekt i wybierz niepustą serię."; return false; }
		Path temporary;
		try
		{
			const auto columns = PhononSeriesColumns(series, preferredZpl);
			if (columns.front().values.empty()) { outError = "Pierwszy zestaw nie ma siatki PL z dostępnym ZPL."; return false; }
			const auto directory = project.Join("exports");
			std::error_code error;
			FileSystem::CreateDirectories(directory.Native(), error);
			if (error) { outError = error.message(); return false; }
			temporary = directory.Join(".series-" + ToString(GenerateUuid()) + ".tmp");
			DataTableFormat format; format.originHeader = true;
			if (!SaveDataTable(temporary, columns, format, outError))
			{
				(void)FileSystem::Remove(temporary.Native(), error);
				return false;
			}
			const auto name = PhononSeriesFileName(series);
			const auto stem = name.substr(0, name.size() - 4);
			for (std::size_t suffix = 1;; ++suffix)
			{
				const auto file = directory / Path::FromUtf8(stem + (suffix == 1 ? "" : "_" + std::to_string(suffix)) + ".dat");
				if (FileSystem::RenameNoReplace(temporary.Native(), file.Native(), error)) { outFile = file; return true; }
				if (error != std::errc::file_exists)
				{
					outError = error.message();
					(void)FileSystem::Remove(temporary.Native(), error);
					return false;
				}
			}
		}
		catch (const std::exception &exception)
		{
			outError = exception.what();
			std::error_code ignored;
			if (!temporary.Empty()) (void)FileSystem::Remove(temporary.Native(), ignored);
			return false;
		}
	}
}
