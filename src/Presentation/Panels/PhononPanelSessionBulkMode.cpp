#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <regex>
#include "Core/JobSystem/JobSystem.hpp"
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
		submitBulkMode(m_Settings.bulkSupercell);
	}

	std::optional<std::size_t> PhononPanelSession::DispersionRow()
	{
		if (!m_Dispersion || m_Dispersion->qpoints.empty() || m_PickedDispersionDirectory != m_Settings.selectedDispersionDirectory)
		{
			m_PickedDispersionRow.reset();
			m_DispersionClickArmed = m_DispersionDragHeld = false;
			m_DispersionInputPlot = 0;
			m_PickedDispersionDirectory = m_Settings.selectedDispersionDirectory;
		}
		if (!m_Dispersion || m_Dispersion->qpoints.empty()) return std::nullopt;
		const auto &result = *m_Dispersion;
		const auto count = std::min({result.qpoints.size(), result.distance.size(), result.segment.size()});
		const auto matches = [&](std::size_t row) {
			const auto delta = result.qpoints[row] - m_Settings.bulkQ;
			return std::abs(delta.x) <= 1e-6 && std::abs(delta.y) <= 1e-6 && std::abs(delta.z) <= 1e-6;
		};
		if (m_PickedDispersionRow && *m_PickedDispersionRow < count &&
			(m_DispersionDragHeld || matches(*m_PickedDispersionRow)))
		{
			m_PickedDispersionSegment = result.segment[*m_PickedDispersionRow];
			return m_PickedDispersionRow;
		}
		m_PickedDispersionRow.reset();
		for (std::size_t row = 0; row < count; ++row)
			if (matches(row))
			{
				m_PickedDispersionRow = row;
				m_PickedDispersionSegment = result.segment[row];
				break;
			}
		return m_PickedDispersionRow;
	}

	void PhononPanelSession::PickDispersionRow(std::size_t row, int band)
	{
		if (!m_Dispersion || row >= m_Dispersion->qpoints.size() || row >= m_Dispersion->segment.size() ||
			row >= m_Dispersion->distance.size() || band < 0 || static_cast<std::size_t>(band) >= m_Dispersion->bandsMeV.size() ||
			row >= m_Dispersion->bandsMeV[band].size()) return;
		m_Settings.bulkQ = m_Dispersion->qpoints[row];
		m_Settings.bulkBand = band;
		m_PickedDispersionRow = row;
		m_PickedDispersionSegment = m_Dispersion->segment[row];
		m_PickedDispersionDirectory = m_Settings.selectedDispersionDirectory;
		SetAnimationSource("bulk_q");
		Changed();
		submitBulkMode(bulkModeSupercellForPick());
	}

	std::string PhononPanelSession::bulkModeSupercellForPick()
	{
		auto size = m_Settings.bulkSupercell;
		if (size == "auto")
			if (const auto window = targetWindow(); window && m_BulkMode)
			{
				const auto &bound = m_BulkWindowSizes.try_emplace(window->get().windowId, m_BulkMode->supercell).first->second;
				size = std::format("{}x{}x{}", bound[0], bound[1], bound[2]);
			}
		return size;
	}

	Path PhononPanelSession::bulkModeOutputPath(const std::string &supercell) const
	{
		const auto coordinate = [](double value) {
			auto text = std::format("{:.6f}", std::abs(value) < 0.0000005 ? 0.0 : value);
			std::replace(text.begin(), text.end(), '-', 'm');
			std::replace(text.begin(), text.end(), '.', 'p');
			return text;
		};
		const auto base = defaultDatasetPath({m_Settings.bulkForceConstantDirectory}, "bulk_mode");
		return base.parent_path() / std::format("bulk_mode_q{}_{}_{}_b{}_{}", coordinate(m_Settings.bulkQ.x),
			coordinate(m_Settings.bulkQ.y), coordinate(m_Settings.bulkQ.z), m_Settings.bulkBand, supercell);
	}

	void PhononPanelSession::submitBulkMode(const std::string &supercell)
	{
		if (IsBusy() && !m_PendingModeJob) { m_BulkModeError = "Poczekaj na zakończenie bieżącego zadania."; return; }
		m_BulkModeError = BulkModeBlocker();
		if (!m_BulkModeError.empty()) return;
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_BulkModeError = "JobSystem niedostępny."; return; }
		CancelBulkMode();
		const auto output = bulkModeOutputPath(supercell);
		m_PendingModeCaseId = ActiveCaseId();
		m_PendingModeRequestId = m_ModeRequestId;
		m_PendingModeJob = CreateRef<PhononModeJob>(m_Settings.bulkForceConstantDirectory, output,
			m_Settings.bulkQ, m_Settings.bulkBand, supercell, m_Settings.dispersionInPlane);
		m_PendingModeJobId = jobs->Submit(m_PendingModeJob, JobPriority::Normal);
		m_BulkModeError.clear();
		Changed();
	}

	void PhononPanelSession::CancelBulkMode()
	{
		++m_ModeRequestId;
		if (m_PendingModeJob) cancelJob(m_PendingModeJobId);
		m_PendingModeJob.reset();
		m_PendingModeJobId = 0;
		m_PendingModeRequestId = 0;
		m_PendingModeCaseId.clear();
	}

	bool PhononPanelSession::applyBulkModeResult(std::uint64_t requestId, const std::string &caseId, const Path &output)
	{
		if (requestId == 0 || requestId != m_ModeRequestId || requestId != m_PendingModeRequestId) return false;
		if (caseId == ActiveCaseId())
		{
			if (!LoadBulkMode(output, false)) return false;
		}
		else
		{
			const auto item = std::find_if(m_Settings.cases.begin(), m_Settings.cases.end(),
				[&](const auto &entry) { return entry.id == caseId; });
			if (item == m_Settings.cases.end()) return false;
			item->settings.selectedBulkModeDirectory = output;
		}
		if (auto dataset = ReadDatasetRef(output)) AddDataset(std::move(*dataset));
		Changed();
		return true;
	}

	void PhononPanelSession::pollBulkModeJob()
	{
		if (!m_PendingModeJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_PendingModeJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		const auto caseId = m_PendingModeCaseId;
		if (m_PendingModeRequestId != m_ModeRequestId) { CancelBulkMode(); return; }
		if (snapshot && !snapshot->cancellationRequested && snapshot->status == JobStatus::Completed && m_PendingModeJob->GetResult())
		{
			const auto output = *m_PendingModeJob->GetResult();
			(void)applyBulkModeResult(m_PendingModeRequestId, caseId, output);
		}
		else if (caseId == ActiveCaseId())
			m_BulkModeError = m_PendingModeJob->GetError() ? m_PendingModeJob->GetError()->userMessage :
				snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie modu przerwane.";
		m_PendingModeJob.reset();
		m_PendingModeJobId = 0;
		m_PendingModeRequestId = 0;
		m_PendingModeCaseId.clear();
	}

	void PhononPanelSession::SetBulkMode(PhononModeField mode)
	{
		if (m_Settings.animationSource == "bulk_q")
		{
			const auto window = targetWindow();
			if (window && m_BulkMode) m_BulkWindowSizes.try_emplace(window->get().windowId, m_BulkMode->supercell);
			if (!window || window->get().structure.atoms.size() != mode.symbols.size() ||
				!std::equal(mode.symbols.begin(), mode.symbols.end(), window->get().structure.atoms.begin(),
					[](const auto &symbol, const auto &atom) { return symbol == atom.element; })) clearWindow();
		}
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
		// The existing binding gate uses the source path; adopt the new dataset without rebuilding the window.
		if (m_Settings.animationSource == "bulk_q")
			if (const auto window = targetWindow()) window->get().structure.sourcePath = directory / "POSCAR";
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
		if (m_BulkWindows.empty()) m_BulkWindowSizes.clear();
		m_PickedDispersionRow.reset();
		m_PickedDispersionDirectory = {};
		m_DispersionInputPlot = 0;
		m_DispersionClickArmed = m_DispersionDragHeld = false;
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
