#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <regex>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononModeIO.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "ScientificRuntime/Python/PhononModeJob.hpp"

namespace DefectStudio
{
	bool PhononPanelSession::ValidBulkSupercell(const std::string &value)
	{
		if (value == "auto") return true;
		std::smatch parts;
		if (!std::regex_match(value, parts, std::regex("([1-9][0-9]*)[xX]([1-9][0-9]*)[xX]([1-9][0-9]*)"))) return false;
		for (int i = 1; i <= 3; ++i)
		{
			const auto text = parts[i].str();
			int number = 0;
			if (std::from_chars(text.data(), text.data() + text.size(), number).ec != std::errc{}) return false;
		}
		return true;
	}

	std::string PhononPanelSession::BulkModeBlocker() const
	{
		if (m_Settings.bulkForceConstantDirectory.Empty()) return "Ustaw katalog FC bulku w przypadku.";
		if (!ValidBulkSupercell(m_Settings.bulkSupercell)) return "Superkomórka: wpisz auto lub NxMxK (dodatnie liczby całkowite).";
		if (m_Settings.bulkBand < 0 || (m_Dispersion && m_Settings.bulkBand >= m_Dispersion->bandCount)) return "Nieprawidłowa gałąź.";
		if (!std::isfinite(m_Settings.bulkQ.x) || !std::isfinite(m_Settings.bulkQ.y) || !std::isfinite(m_Settings.bulkQ.z)) return "q musi być skończone.";
		if (defaultDatasetPath({m_Settings.bulkForceConstantDirectory}, "bulk_mode").Empty()) return "Ustaw katalog projektu lub FC bulku.";
		return {};
	}

	void PhononPanelSession::SubmitBulkMode()
	{
		if (IsBusy() && !m_PendingModeJob) { m_BulkModeError = "Poczekaj na zakończenie bieżącego zadania."; return; }
		m_BulkModeError = BulkModeBlocker();
		if (!m_BulkModeError.empty()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_BulkModeError = "JobSystem niedostępny."; return; }
		CancelBulkMode();
		// Separate publications prevent a cancelled subprocess from overwriting a newer request.
		const auto base = defaultDatasetPath({m_Settings.bulkForceConstantDirectory}, "bulk_mode");
		const auto output = base.parent_path() / ("bulk_mode_" + ToString(GenerateUuid()));
		m_PendingModeCaseId = ActiveCaseId();
		m_PendingModeJob = CreateRef<PhononModeJob>(m_Settings.bulkForceConstantDirectory, output,
			m_Settings.bulkQ, m_Settings.bulkBand, m_Settings.bulkSupercell, m_Settings.dispersionInPlane);
		m_PendingModeJobId = jobs->Submit(m_PendingModeJob, JobPriority::Normal);
		m_BulkModeError.clear();
		Changed();
	}

	void PhononPanelSession::CancelBulkMode()
	{
		if (m_PendingModeJob) cancelJob(m_PendingModeJobId);
		m_PendingModeJob.reset();
		m_PendingModeJobId = 0;
		m_PendingModeCaseId.clear();
	}

	void PhononPanelSession::pollBulkModeJob()
	{
		if (!m_PendingModeJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingModeJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		const auto caseId = m_PendingModeCaseId;
		if (snapshot && snapshot->status == JobStatus::Completed && m_PendingModeJob->GetResult())
		{
			const auto output = *m_PendingModeJob->GetResult();
			if (auto dataset = ReadDatasetRef(output)) AddDataset(std::move(*dataset));
			if (caseId == ActiveCaseId())
			{
				if (LoadBulkMode(output)) Changed();
			}
			else
				for (auto &item : m_Settings.cases)
					if (item.id == caseId) { item.settings.selectedBulkModeDirectory = output; Changed(); }
		}
		else if (caseId == ActiveCaseId())
			m_BulkModeError = m_PendingModeJob->GetError() ? m_PendingModeJob->GetError()->userMessage :
				snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie modu przerwane.";
		m_PendingModeJob.reset();
		m_PendingModeJobId = 0;
		m_PendingModeCaseId.clear();
	}

	void PhononPanelSession::SetBulkMode(PhononModeField mode)
	{
		if (m_Settings.animationSource == "bulk_q") clearWindow();
		m_BulkMode = std::move(mode);
		++m_PlotRevision;
		rebuildField();
	}

	bool PhononPanelSession::LoadBulkMode(const Path &directory, bool adoptControls)
	{
		PhononModeField mode;
		if (!PhononModeIO::Load(directory, mode, m_BulkModeError)) return false;
		SetBulkMode(std::move(mode));
		m_Settings.selectedBulkModeDirectory = directory;
		if (adoptControls)
		{
			m_Settings.bulkQ = m_BulkMode->q;
			m_Settings.bulkBand = m_BulkMode->band;
			const auto &size = m_BulkMode->supercell;
			m_Settings.bulkSupercell = std::format("{}x{}x{}", size[0], size[1], size[2]);
		}
		if (auto dataset = ReadDatasetRef(directory)) AddDataset(std::move(*dataset));
		(void)SelectDataset(directory);
		return true;
	}

	void PhononPanelSession::selectCaseDispersion()
	{
		bool changed = false;
		const auto select = [&](PhononCaseSettings &settings) {
			if (!settings.selectedDispersionDirectory.Empty()) return;
			for (const auto &dataset : m_Datasets)
				if (dataset.kind == AnalysisDatasetKind::Dispersion && dataset.inputs.contains("fc_dir") &&
					SameFilePath(dataset.inputs.at("fc_dir"), settings.bulkForceConstantDirectory, m_ProjectDirectory))
				{
					settings.selectedDispersionDirectory = dataset.directory;
					changed = true;
					return;
				}
		};
		select(m_Settings);
		for (auto &item : m_Settings.cases)
			if (item.id != ActiveCaseId()) select(item.settings);
		if (!m_Dispersion && !m_Settings.selectedDispersionDirectory.Empty())
			(void)loadDispersion(m_Settings.selectedDispersionDirectory);
		if (changed) Changed();
	}

	void PhononPanelSession::loadCaseBulk()
	{
		m_Dispersion.reset();
		m_DispersionError.clear();
		m_BulkMode.reset();
		m_BulkModeError.clear();
		selectCaseDispersion();
		if (!m_Settings.selectedBulkModeDirectory.Empty()) (void)LoadBulkMode(m_Settings.selectedBulkModeDirectory, false);
		++m_PlotRevision;
	}

	bool PhononPanelSession::isBulkWindow(const RendererWindowState &window, const Path &projectDirectory) const
	{
		if (window.title.starts_with("Superkomórka bulku (fonony)")) return true;
		for (const auto &[id, windowId] : m_BulkWindows)
			if (windowId == window.windowId) return true;
		if (!m_Settings.selectedBulkModeDirectory.Empty() &&
			SameFilePath(window.structure.sourcePath, m_Settings.selectedBulkModeDirectory / "POSCAR", projectDirectory)) return true;
		for (const auto &item : m_Settings.cases)
			if (!item.settings.selectedBulkModeDirectory.Empty() &&
				SameFilePath(window.structure.sourcePath, item.settings.selectedBulkModeDirectory / "POSCAR", projectDirectory)) return true;
		for (const auto &dataset : m_Datasets)
			if (dataset.kind == AnalysisDatasetKind::BulkMode &&
				SameFilePath(window.structure.sourcePath, dataset.directory / "POSCAR", projectDirectory)) return true;
		return false;
	}

	bool PhononPanelSession::bulkWindowMatches(const RendererWindowState &window, std::string &error) const
	{
		error.clear();
		if (!m_BulkMode) error = "Policz lub wczytaj mod bulku.";
		else if (!SameFilePath(window.structure.sourcePath, m_Settings.selectedBulkModeDirectory / "POSCAR", m_ProjectDirectory))
			error = "Otwórz superkomórkę bulku dla wybranego zestawu.";
		else if (window.structure.atoms.size() != m_BulkMode->symbols.size()) error = "Liczba atomów okna różni się od modu bulku.";
		else
			for (std::size_t atom = 0; atom < m_BulkMode->symbols.size(); ++atom)
				if (window.structure.atoms[atom].element != m_BulkMode->symbols[atom])
				{
					error = "Pierwiastki lub kolejność atomów różnią się od modu bulku.";
					break;
				}
		return error.empty();
	}

	void PhononPanelSession::OpenBulkSupercellWindow()
	{
		if (!m_BulkMode || m_Settings.selectedBulkModeDirectory.Empty()) return;
		SetAnimationSource("bulk_q");
		openSupercellWindow();
	}
}
