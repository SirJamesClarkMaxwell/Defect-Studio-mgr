#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	const std::vector<PhononCase> &PhononPanelSession::Cases() const
	{
		for (auto &item : m_Settings.cases)
			if (item.id == m_Settings.activeCaseId)
				item.settings = m_Settings;
		return m_Settings.cases;
	}

	const std::string &PhononPanelSession::ActiveCaseId() const { return m_Settings.activeCaseId; }

	std::string PhononPanelSession::AddCase(std::string name)
	{
		(void)Cases();
		if (m_Settings.nextCaseNumber < 1 || m_Settings.nextCaseNumber == std::numeric_limits<int>::max())
		{
			m_Error = "Nie można dodać przypadku: brak kolejnych numerów.";
			return {};
		}
		const auto id = "case-" + std::to_string(m_Settings.nextCaseNumber++);
		if (name.empty()) name = "Przypadek " + id.substr(5);
		m_Settings.cases.push_back({id, std::move(name), {}, {}});
		(void)ActivateCase(id);
		return id;
	}

	std::string PhononPanelSession::DuplicateCase(const std::string &id)
	{
		const auto &cases = Cases();
		const auto found = std::find_if(cases.begin(), cases.end(), [&](const auto &item) { return item.id == id; });
		if (found == cases.end()) return {};
		auto copy = *found;
		copy.settings.outputDirectory = {};
		copy.settings.ccdOutputDirectory = {};
		copy.settings.ccdAnalysisOutputDirectory = {};
		const auto newId = AddCase(copy.name + " (kopia)");
		if (newId.empty()) return {};
		static_cast<PhononCaseSettings &>(m_Settings) = std::move(copy.settings);
		m_EmbeddingMult = {12, 12, 1};
		if (!m_Settings.embeddingMult.empty())
		{
			std::istringstream mult(m_Settings.embeddingMult);
			char separator;
			mult >> m_EmbeddingMult[0] >> separator >> m_EmbeddingMult[1] >> separator >> m_EmbeddingMult[2];
		}
		if (!m_Settings.resultDirectory.Empty()) loadResult(m_Settings.resultDirectory);
		if (!m_Settings.selectedCcdDirectory.Empty()) (void)loadCcdResult(m_Settings.selectedCcdDirectory);
		if (m_Windows) syncWindow(0.0);
		Changed();
		return newId;
	}

	void PhononPanelSession::RemoveCase(const std::string &id)
	{
		const std::string removed = id;
		(void)Cases();
		auto &cases = m_Settings.cases;
		const auto found = std::find_if(cases.begin(), cases.end(), [&](const auto &item) { return item.id == removed; });
		if (found == cases.end()) return;
		const auto index = static_cast<std::size_t>(found - cases.begin());
		const bool active = removed == ActiveCaseId();
		if (active) clearWindow();
		cases.erase(found);
		m_CaseWindows.erase(removed);
		m_CheckedCases.erase(removed);
		if (active)
		{
			m_Settings.activeCaseId.clear();
			if (!cases.empty()) (void)ActivateCase(cases[std::min(index, cases.size() - 1)].id);
			else
			{
				ClearResult();
				ClearCcdResult();
				static_cast<PhononCaseSettings &>(m_Settings) = {};
				m_CurrentDataset = {};
				m_EmbeddingSummary.clear();
				m_EmbeddingMult = {12, 12, 1};
				m_Error.clear();
				m_CcdError.clear();
				m_CcdAnalysisError.clear();
				m_CcdInputsResult.clear();
			}
		}
		++m_PlotRevision;
		m_CaseOutputDirectories.clear();
		Changed();
	}

	void PhononPanelSession::RenameCase(const std::string &id, std::string name)
	{
		if (name.empty()) return;
		for (auto &item : m_Settings.cases)
			if (item.id == id && item.name != name)
			{
				item.name = std::move(name);
				if (m_Windows)
					for (auto &window : m_Windows->get())
						if (m_CaseWindows.contains(id) && window.windowId == m_CaseWindows.at(id))
							window.title = std::string(SupercellWindowTitle) + " – " + item.name;
				m_CaseOutputDirectories.clear();
				++m_PlotRevision;
				Changed();
				return;
			}
	}

	bool PhononPanelSession::ActivateCase(const std::string &id)
	{
		const std::string requested = id;
		(void)Cases();
		const auto found = std::find_if(m_Settings.cases.begin(), m_Settings.cases.end(),
			[&](const auto &item) { return item.id == requested; });
		if (found == m_Settings.cases.end()) return false;
		if (ActiveCaseId() == requested) return true;
		const auto settings = found->settings;
		clearWindow();
		cacheActiveResult();
		m_Result.reset();
		m_CcdResult.reset();
		m_Error.clear();
		m_CcdError.clear();
		m_CcdAnalysisError.clear();
		m_CcdInputsResult.clear();
		m_EmbeddingSummary.clear();
		m_Settings.activeCaseId = requested;
		static_cast<PhononCaseSettings &>(m_Settings) = settings;
		m_EmbeddingMult = {12, 12, 1};
		if (!settings.embeddingMult.empty())
		{
			std::istringstream mult(settings.embeddingMult);
			char separator;
			mult >> m_EmbeddingMult[0] >> separator >> m_EmbeddingMult[1] >> separator >> m_EmbeddingMult[2];
		}
		if (!settings.resultDirectory.Empty()) loadResult(settings.resultDirectory);
		if (!settings.selectedCcdDirectory.Empty()) (void)loadCcdResult(settings.selectedCcdDirectory);
		m_CurrentDataset = !settings.resultDirectory.Empty() ? settings.resultDirectory : settings.selectedCcdDirectory;
		if (m_Windows) syncWindow(0.0);
		m_CaseOutputDirectories.clear();
		++m_PlotRevision;
		Changed();
		return true;
	}

	void PhononPanelSession::AttachDataset(const std::string &caseId, const Path &directory)
	{
		if (directory.Empty() || std::none_of(m_Settings.cases.begin(), m_Settings.cases.end(),
			[&](const auto &item) { return item.id == caseId; })) return;
		const Path attached = directory;
		const auto dataset = std::find_if(m_Datasets.begin(), m_Datasets.end(), [&](const auto &item) {
			return SameFilePath(item.directory, attached, m_ProjectDirectory);
		});
		if (dataset != m_Datasets.end() && dataset->kind == AnalysisDatasetKind::Dispersion) return;
		for (auto &item : m_Settings.cases)
		{
			std::erase_if(item.datasets, [&](const auto &path) { return SameFilePath(path, attached, m_ProjectDirectory); });
			if (item.id == caseId) item.datasets.push_back(attached);
		}
		m_CaseOutputDirectories.clear();
		++m_PlotRevision;
		Changed();
	}

	void PhononPanelSession::DetachDataset(const std::string &caseId, const Path &directory)
	{
		const Path detached = directory;
		for (auto &item : m_Settings.cases)
			if (item.id == caseId)
			{
				std::erase_if(item.datasets, [&](const auto &path) { return SameFilePath(path, detached, m_ProjectDirectory); });
				m_CaseOutputDirectories.clear();
				++m_PlotRevision;
				Changed();
				return;
			}
	}

	std::string PhononPanelSession::CaseWindowTitle() const
	{
		for (const auto &item : m_Settings.cases)
			if (item.id == ActiveCaseId()) return std::string(SupercellWindowTitle) + " – " + item.name;
		return std::string(SupercellWindowTitle);
	}

	bool PhononPanelSession::CaseChecked(const std::string &id) const
	{
		const auto found = m_CheckedCases.find(id);
		return found != m_CheckedCases.end() && found->second;
	}

	void PhononPanelSession::SetCaseChecked(const std::string &id, bool checked)
	{
		m_CheckedCases[id] = checked;
		Changed();
	}

	std::optional<std::reference_wrapper<const PhononAnalysisResult>> PhononPanelSession::CachedCaseResult(const std::string &id) const
	{
		for (const auto &item : Cases())
			if (item.id == id)
			{
				if (id == ActiveCaseId() && m_Result) return std::cref(*m_Result);
				for (const auto &[key, cached] : m_Spectra)
					if (cached.result && SameFilePath(Path::FromUtf8(key), item.settings.resultDirectory, m_ProjectDirectory))
						return std::cref(*cached.result);
			}
		return std::nullopt;
	}
}
