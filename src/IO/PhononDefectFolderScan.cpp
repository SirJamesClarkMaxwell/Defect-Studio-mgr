#include "Core/dspch.hpp"
#include "IO/PhononDefectFolderScan.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <tuple>

namespace DefectStudio
{
	namespace
	{
		std::string LowerName(const FilePath &path)
		{
			auto name = Path::FromResolved(path.filename()).Utf8();
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return name;
		}

		bool Skip(const FilePath &directory)
		{
			const auto name = LowerName(directory);
			static const std::regex displacement("disp-[0-9]+");
			return name == ".git" || name == ".venv" || name == "__pycache__" || name == "defectstudio" ||
				name == "embedded" || name == "spectral-function" || name == "lineshape" || name == "run_computations" ||
				std::regex_match(name, displacement);
		}

		std::optional<PhononFolderRole> RunRole(const FilePath &path)
		{
			const auto name = LowerName(path);
			if (name.find("triplet") != std::string::npos || name.find("tryplet") != std::string::npos) return PhononFolderRole::Triplet;
			if (name.find("excited_geometry") != std::string::npos || name.find("gs_at_es") != std::string::npos ||
				name.find("gs@es") != std::string::npos) return PhononFolderRole::GroundAtExcited;
			static const std::regex ground("(^|[^a-z0-9])(relax|ground|gs)([^a-z0-9]|$)");
			static const std::regex excited("(^|[^a-z0-9])(excited|exc|es)([^a-z0-9]|$)");
			if (std::regex_search(name, ground)) return PhononFolderRole::GroundState;
			if (std::regex_search(name, excited)) return PhononFolderRole::ExcitedState;
			return std::nullopt;
		}

		bool Below(const FilePath &path, const FilePath &parent)
		{
			auto a = path.begin(), b = parent.begin();
			for (; a != path.end() && b != parent.end() && *a == *b; ++a, ++b) {}
			return b == parent.end();
		}
	}

	std::vector<PhononFolderProposal> ScanPhononDefectFolder(const Path &root, const std::function<bool()> &cancelled)
	{
		std::vector<PhononFolderProposal> proposals;
		if (root.Empty() || (cancelled && cancelled())) return proposals;
		std::error_code error;
		auto base = root.Native().lexically_normal();
		if (base.filename().empty() && base != base.root_path()) base = base.parent_path();
		if (!std::filesystem::is_directory(std::filesystem::symlink_status(base, error)) || error) return proposals;
		std::vector<PhononFolderCandidate> candidates;
		const auto inspect = [&](const FilePath &directory) {
			const auto file = [&](const auto &name) {
				std::error_code ignored;
				return std::filesystem::is_regular_file(directory / name, ignored);
			};
			if (file("dephonopy_disp.yaml") || file("phonopy_disp.yaml") || file("FORCE_SETS") || file("FORCE_CONSTANTS"))
				candidates.push_back({PhononFolderRole::ForceConstants, Path::FromResolved(directory)});
			if (!file("OUTCAR") || !file("CONTCAR")) return;
			auto role = RunRole(directory);
			if (!role && directory != base && directory.parent_path() != base) role = RunRole(directory.parent_path());
			if (role) candidates.push_back({*role, Path::FromResolved(directory)});
		};
		inspect(base);
		std::filesystem::recursive_directory_iterator it(base, std::filesystem::directory_options::skip_permission_denied, error), end;
		while (!error && it != end)
		{
			if (cancelled && cancelled()) break;
			const auto status = it->symlink_status(error);
			if (!error && std::filesystem::is_directory(status))
			{
				if (Skip(it->path())) it.disable_recursion_pending();
				else inspect(it->path());
			}
			error.clear();
			it.increment(error);
		}
		std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
			return std::tie(a.directory.Native(), a.role) < std::tie(b.directory.Native(), b.role);
		});
		std::map<FilePath, unsigned> pairs;
		for (const auto &candidate : candidates)
		{
			const unsigned flag = candidate.role == PhononFolderRole::GroundState ? 1 : candidate.role == PhononFolderRole::ExcitedState ? 2 : 0;
			if (!flag) continue;
			for (auto parent = candidate.directory.Native(); Below(parent, base); parent = parent.parent_path())
			{
				pairs[parent] |= flag;
				if (parent == base || parent == parent.parent_path()) break;
			}
		}
		std::set<FilePath> ancestors;
		for (const auto &[path, flags] : pairs)
			if (flags == 3 && path != base) ancestors.insert(path.parent_path());
		for (const auto &[path, flags] : pairs)
		{
			if (flags != 3 || ancestors.contains(path)) continue;
			PhononFolderProposal proposal;
			proposal.systemDirectory = Path::FromResolved(path);
			proposal.name = path == base ? Path::FromResolved(base.filename()).Utf8() : Path::FromResolved(path.lexically_relative(base)).Utf8();
			std::replace(proposal.name.begin(), proposal.name.end(), '\\', '/');
			for (const auto &candidate : candidates)
				if (Below(candidate.directory.Native(), path)) proposal.candidates.push_back(candidate);
			const auto pick = [&](PhononFolderRole role) {
				std::vector<FilePath> paths;
				for (const auto &candidate : proposal.candidates)
					if (candidate.role == role) paths.push_back(candidate.directory.Native());
				const auto rank = [&](const FilePath &candidate) {
					const auto relative = candidate.lexically_relative(path);
					return std::tuple(std::distance(relative.begin(), relative.end()),
						role == PhononFolderRole::ExcitedState && LowerName(candidate).find("singlet") == std::string::npos, candidate);
				};
				if (paths.empty()) return Path{};
				return Path::FromResolved(*std::min_element(paths.begin(), paths.end(), [&](const auto &a, const auto &b) { return rank(a) < rank(b); }));
			};
			proposal.forceConstantDirectory = pick(PhononFolderRole::ForceConstants);
			proposal.groundStateDirectory = pick(PhononFolderRole::GroundState);
			proposal.excitedStateDirectory = pick(PhononFolderRole::ExcitedState);
			proposal.tripletDirectory = pick(PhononFolderRole::Triplet);
			proposal.groundAtExcitedDirectory = pick(PhononFolderRole::GroundAtExcited);
			proposal.serverResults = FindServerHrResults(proposal.systemDirectory, cancelled);
			proposal.datasets = FindDatasets(proposal.systemDirectory, cancelled);
			proposals.push_back(std::move(proposal));
		}
		return proposals;
	}
}
