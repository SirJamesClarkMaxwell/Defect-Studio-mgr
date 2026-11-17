#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	void PhononPanelSession::AddDefectFolders(std::vector<Path> roots)
	{
		std::erase_if(roots, [](const auto &root) { return root.Empty(); });
		if (roots.empty()) return;
		for (const auto &root : roots)
			if (std::none_of(m_Settings.defectRoots.begin(), m_Settings.defectRoots.end(), [&](const auto &other) {
				return SameFilePath(root, other, m_ProjectDirectory);
			})) m_Settings.defectRoots.push_back(root);
		Changed();
		startDefectFolderScan(std::move(roots), true);
	}

	void PhononPanelSession::RescanDefectFolders()
	{
		if (!m_Settings.defectRoots.empty()) startDefectFolderScan(m_Settings.defectRoots, false);
	}

	void PhononPanelSession::RemoveDefectRoot(const Path &root)
	{
		const Path removed = root;
		std::erase_if(m_Settings.defectRoots, [&](const auto &path) { return SameFilePath(path, removed, m_ProjectDirectory); });
		Changed();
	}

	void PhononPanelSession::commitDefectFolderScan(std::vector<PhononFolderProposal> proposals,
		std::map<std::string, std::vector<PhononNewServerResult>> newResults)
	{
		// Interactive scans can cover only one of several remembered roots.
		for (const auto &proposal : proposals)
			for (const auto &item : Cases())
				if (SameFilePath(item.sourceDirectory, proposal.systemDirectory, m_ProjectDirectory))
					m_NewServerResults[item.id] = newResults[item.id];
		m_ScanMessage = "Skan zakończony.";
		if (!m_ProposeDefectFolders) return;
		m_DefectFolderSelections.clear();
		for (auto &proposal : proposals)
		{
			PhononFolderSelection selection;
			selection.proposal = std::move(proposal);
			for (const auto &item : Cases())
				if (SameFilePath(item.settings.groundStateDirectory, selection.proposal.groundStateDirectory, m_ProjectDirectory) &&
					SameFilePath(item.settings.excitedStateDirectory, selection.proposal.excitedStateDirectory, m_ProjectDirectory))
				{
					selection.selected = false;
					selection.existingCase = item.name;
					break;
				}
			selection.serverChecked.assign(selection.proposal.serverResults.size(), true);
			selection.datasetChecked.assign(selection.proposal.datasets.size(), true);
			m_DefectFolderSelections.push_back(std::move(selection));
		}
		m_DefectFolderPopupRequested = !m_DefectFolderSelections.empty();
		m_ScanMessage.clear();
		if (m_DefectFolderSelections.empty())
			for (const auto &root : m_DefectFolderScanRoots)
				m_ScanMessage += "Nie znaleziono par GS + ES pod " + root.Utf8() + ".\n";
	}

	std::vector<std::string> PhononPanelSession::CreateCasesFromFolders(const std::vector<PhononFolderSelection> &selections)
	{
		std::vector<std::string> created;
		if (IsBusy()) return created;
		Path bulk;
		bool severalBulk = false;
		const bool hadCases = !Cases().empty();
		for (const auto &item : Cases())
			if (!item.settings.bulkForceConstantDirectory.Empty())
			{
				if (bulk.Empty()) bulk = item.settings.bulkForceConstantDirectory;
				else if (!SameFilePath(bulk, item.settings.bulkForceConstantDirectory, m_ProjectDirectory)) severalBulk = true;
			}
		for (const auto &selection : selections)
		{
			const auto &proposal = selection.proposal;
			if (!selection.selected || proposal.groundStateDirectory.Empty() || proposal.excitedStateDirectory.Empty()) continue;
			const auto id = AddCase(proposal.name, false);
			if (id.empty()) break;
			auto &item = m_Settings.cases.back();
			item.sourceDirectory = proposal.systemDirectory;
			auto &settings = item.settings;
			settings.forceConstantDirectory = proposal.forceConstantDirectory;
			settings.groundStateDirectory = proposal.groundStateDirectory;
			settings.excitedStateDirectory = proposal.excitedStateDirectory;
			settings.tripletDirectory = proposal.tripletDirectory;
			settings.groundAtExcitedDirectory = proposal.groundAtExcitedDirectory;
			if (!severalBulk) settings.bulkForceConstantDirectory = bulk;
			for (std::size_t i = 0; i < proposal.datasets.size(); ++i)
			{
				if (i >= selection.datasetChecked.size() || !selection.datasetChecked[i]) continue;
				const auto &dataset = proposal.datasets[i];
				AddDataset(dataset);
				if (dataset.kind == AnalysisDatasetKind::Phonons && settings.resultDirectory.Empty()) settings.resultDirectory = dataset.directory;
				else if (dataset.kind == AnalysisDatasetKind::Ccd && settings.selectedCcdDirectory.Empty()) settings.selectedCcdDirectory = dataset.directory;
				else if (dataset.kind == AnalysisDatasetKind::Dispersion) settings.selectedDispersionDirectory = dataset.directory;
				else if (dataset.kind == AnalysisDatasetKind::BulkMode) settings.selectedBulkModeDirectory = dataset.directory;
				AttachDataset(id, dataset.directory);
			}
			created.push_back(id);
		}
		// AddCase can preserve the active case; no renderer or loaded-result churn during bulk creation.
		if (!hadCases && !created.empty()) (void)ActivateCase(created.front());
		std::size_t index = 0;
		for (const auto &selection : selections)
		{
			if (!selection.selected || selection.proposal.groundStateDirectory.Empty() || selection.proposal.excitedStateDirectory.Empty()) continue;
			if (index >= created.size()) break;
			std::vector<ServerHrResult> hits;
			for (std::size_t i = 0; i < selection.proposal.serverResults.size(); ++i)
				if (i < selection.serverChecked.size() && selection.serverChecked[i]) hits.push_back(selection.proposal.serverResults[i]);
			(void)queueServerResults(created[index++], hits);
		}
		submitNextBatch();
		Changed();
		return created;
	}

	std::map<std::string, std::vector<PhononNewServerResult>> PhononPanelSession::findNewServerResults(
		const std::vector<PhononFolderProposal> &proposals, const std::vector<PhononCase> &cases,
		const Path &project, const std::function<bool()> &cancelled)
	{
		std::map<std::string, std::vector<PhononNewServerResult>> results;
		for (const auto &item : cases)
		{
			if (cancelled && cancelled()) break;
			if (item.sourceDirectory.Empty()) continue;
			std::vector<std::pair<Path, double>> imported;
			for (const auto &directory : item.datasets)
			{
				if (cancelled && cancelled()) return results;
				std::string text, error;
				if (!TextFileIO::Load(directory / "summary.json", text, error)) continue;
				try
				{
					const auto summary = nlohmann::json::parse(text);
					if (!summary.contains("server")) continue;
					const auto &server = summary.at("server");
					auto source = Path::FromUtf8(server.at("hr_dir").get<std::string>());
					if (!source.Native().is_absolute()) source = directory / source;
					const double time = server.value("hr_factors_mtime", 0.0);
					if (std::isfinite(time))
					{
						const auto existing = std::find_if(imported.begin(), imported.end(), [&](const auto &entry) { return SameFilePath(entry.first, source, project); });
						if (existing == imported.end()) imported.emplace_back(source, time);
						else existing->second = std::max(existing->second, time);
					}
				}
				catch (const std::exception &) {}
			}
			for (const auto &proposal : proposals)
				if (SameFilePath(proposal.systemDirectory, item.sourceDirectory, project))
					for (const auto &hit : proposal.serverResults)
					{
						if (cancelled && cancelled()) return results;
						const auto found = std::find_if(imported.begin(), imported.end(), [&](const auto &entry) {
							return SameFilePath(entry.first, hit.hrDirectory, project);
						});
						if (found == imported.end()) results[item.id].push_back({hit, false});
						else
						{
							std::error_code error;
							const auto modified = std::filesystem::last_write_time((hit.hrDirectory / "hr_factors.dat").Native(), error);
							if (error) continue;
							const double seconds = std::chrono::duration<double>(std::chrono::clock_cast<std::chrono::system_clock>(modified).time_since_epoch()).count();
							if (seconds > found->second + 1e-6) results[item.id].push_back({hit, true});
						}
					}
		}
		return results;
	}

	const std::vector<PhononNewServerResult> &PhononPanelSession::NewServerResults(const std::string &caseId) const
	{
		static const std::vector<PhononNewServerResult> empty;
		const auto found = m_NewServerResults.find(caseId);
		return found == m_NewServerResults.end() ? empty : found->second;
	}

	void PhononPanelSession::ImportNewServerResults(const std::string &caseId)
	{
		if (IsBusy()) return;
		std::vector<ServerHrResult> hits;
		for (const auto &entry : NewServerResults(caseId)) hits.push_back(entry.result);
		if (queueServerResults(caseId, hits)) submitNextBatch();
	}

	void PhononPanelSession::SetCaseHidden(const std::string &id, bool hidden)
	{
		for (auto &item : m_Settings.cases)
			if (item.id == id && item.hidden != hidden)
			{
				item.hidden = hidden;
				if (id == ActiveCaseId())
				{
					if (hidden) { StopAnimation(); m_Dispersion.reset(); }
					else loadCaseBulk();
				}
				++m_PlotRevision;
				Changed();
			}
	}

	bool PhononPanelSession::ActiveCaseVisible() const
	{
		for (const auto &item : m_Settings.cases)
			if (item.id == ActiveCaseId()) return !item.hidden;
		return true;
	}

	bool PhononPanelSession::DatasetVisible(const Path &directory) const
	{
		const auto owner = DatasetCaseId(directory);
		bool hidden = false;
		for (const auto &item : Cases())
			if ((!owner.empty() && item.id == owner) || (owner.empty() &&
				(SameFilePath(item.settings.resultDirectory, directory, m_ProjectDirectory) ||
				 SameFilePath(item.settings.selectedCcdDirectory, directory, m_ProjectDirectory) ||
				 SameFilePath(item.settings.selectedDispersionDirectory, directory, m_ProjectDirectory) ||
				 SameFilePath(item.settings.selectedBulkModeDirectory, directory, m_ProjectDirectory))))
			{
				if (!item.hidden) return true;
				hidden = true;
			}
		return !hidden;
	}

	void PhononPanelSession::CompareAllVisibleCases()
	{
		for (const auto &item : Cases())
			if (!item.hidden && !item.settings.resultDirectory.Empty()) SetCompared(item.settings.resultDirectory, true);
	}

	void PhononPanelSession::ClearCompared()
	{
		if (m_Settings.comparedDirectories.empty()) return;
		m_Settings.comparedDirectories.clear();
		++m_PlotRevision;
		Changed();
	}
}
