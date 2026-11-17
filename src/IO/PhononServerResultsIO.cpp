#include "Core/dspch.hpp"
#include "IO/PhononServerResultsIO.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <regex>
#include <tuple>

namespace DefectStudio
{
	std::vector<ServerHrResult> FindServerHrResults(const Path &root, const std::function<bool()> &cancelled)
	{
		std::vector<ServerHrResult> results;
		if (root.Empty() || (cancelled && cancelled())) return results;
		std::error_code error;
		if (!std::filesystem::is_directory(std::filesystem::symlink_status(root.Native(), error)) || error) return results;
		const auto inspect = [&](const FilePath &file) {
			if (file.filename() != "hr_factors.dat") return;
			const auto hr = file.parent_path();
			const auto size = hr.parent_path();
			if (size.parent_path().filename() != "spectral-function") return;
			static const std::regex sizePattern(R"(^([1-9][0-9]*)x([1-9][0-9]*)x([1-9][0-9]*)$)");
			static const std::regex variantPattern(R"(^(.+)_cutoff([0-9]+(?:\.[0-9]+)?)A$)");
			std::smatch match;
			const auto sizeText = Path::FromResolved(size.filename()).Utf8();
			if (!std::regex_match(sizeText, match, sizePattern)) return;
			ServerHrResult result;
			try
			{
				for (int i = 0; i < 3; ++i) result.supercell[i] = std::stoi(match[i + 1]);
				result.hrDirectory = Path::FromResolved(hr);
				result.systemDirectory = Path::FromResolved(size.parent_path().parent_path());
				result.variant = Path::FromResolved(hr.filename()).Utf8();
				result.method = result.variant;
				if (std::regex_match(result.variant, match, variantPattern))
				{
					result.method = match[1];
					result.cutoffAngstrom = std::stod(match[2]);
					if (!std::isfinite(*result.cutoffAngstrom) || *result.cutoffAngstrom <= 0.0) return;
				}
			}
			catch (const std::exception &) { return; }
			results.push_back(std::move(result));
		};
		std::filesystem::recursive_directory_iterator it(root.Native(), std::filesystem::directory_options::skip_permission_denied, error), end;
		while (!error && it != end)
		{
			if (cancelled && cancelled()) break;
			const auto status = it->symlink_status(error);
			if (!error && std::filesystem::is_directory(status))
			{
				const auto name = it->path().filename();
				if (name == ".git" || name == ".venv" || name == "__pycache__" || name == "embedded" ||
					name == "dispersion" || name == "lineshape" || name == "run_computations")
					it.disable_recursion_pending();
			}
			else if (!error && std::filesystem::is_regular_file(status)) inspect(it->path());
			error.clear();
			it.increment(error);
		}
		std::sort(results.begin(), results.end(), [](const auto &a, const auto &b) {
			const auto volume = [](const auto &size) { return static_cast<long double>(size[0]) * size[1] * size[2]; };
			return std::tuple(a.systemDirectory.Native(), a.variant, volume(a.supercell), a.supercell) <
				std::tuple(b.systemDirectory.Native(), b.variant, volume(b.supercell), b.supercell);
		});
		return results;
	}
}
