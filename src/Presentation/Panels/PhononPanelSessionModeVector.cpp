#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononModeVectorIO.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "ScientificRuntime/Python/PhononModeVectorJob.hpp"

namespace DefectStudio
{
	std::string PhononPanelSession::ModeVectorBlocker() const
	{
		if (!m_Result || !SelectedMode()) return "Wybierz mod.";
		if (m_Result->hasEigenvectors || !ModeRows(*m_Result, *SelectedMode()).eigenvector.empty()) return {};
		if (!m_ServerEmbeddedError.empty()) return m_ServerEmbeddedError;
		if (m_ServerEmbeddedDirectory.Empty()) return "Brak katalogu embedded w danych serwera.";
		if (!FileSystem::Exists((m_ServerEmbeddedDirectory / "modes.h5").Native())) return "Brak modes.h5 w katalogu embedded.";
		if (!FileSystem::Exists((m_ServerEmbeddedDirectory / "POSCAR").Native())) return "Brak POSCAR w katalogu embedded.";
		if (m_ProjectDirectory.Empty()) return "Otwórz projekt przed wczytaniem wektora modu.";
		return {};
	}

	void PhononPanelSession::restoreModeVectors()
	{
		CancelModeVector();
		m_ModeVectorError.clear();
		m_ServerEmbeddedDirectory = {};
		m_ServerEmbeddedError.clear();
		if (!m_Result || m_Result->hasEigenvectors) return;
		(void)PhononModeVectorIO::EmbeddedDirectory(m_Result->directory, m_ServerEmbeddedDirectory, m_ServerEmbeddedError);
		m_Result->fetchedModes.clear();
		std::error_code ignored;
		const auto modified = std::filesystem::last_write_time((m_Result->directory / "summary.json").Native(), ignored);
		std::erase_if(m_ModeVectorCache, [&](const auto &entry) {
			if (!SameFilePath(entry.dataset, m_Result->directory, m_ProjectDirectory) || entry.summaryModified == modified) return false;
			for (auto &[key, cached] : m_Spectra)
				if (cached.result && SameFilePath(entry.dataset, cached.result->directory, m_ProjectDirectory))
					cached.result->fetchedModes.erase(entry.vector->modeIndex);
			return true;
		});
		for (const auto &entry : m_ModeVectorCache)
			if (SameFilePath(entry.dataset, m_Result->directory, m_ProjectDirectory) &&
				entry.vector->modeIndex < m_Result->ModeCount() && entry.vector->participation.size() == m_Result->AtomCount() &&
				std::abs(entry.vector->frequencyMeV - m_Result->modes[entry.vector->modeIndex].frequencyMeV) <= 0.05)
				m_Result->fetchedModes[entry.vector->modeIndex] = entry.vector;
	}

	void PhononPanelSession::touchModeVector(std::size_t mode)
	{
		if (!m_Result) return;
		const auto found = std::find_if(m_ModeVectorCache.begin(), m_ModeVectorCache.end(), [&](const auto &entry) {
			return entry.vector->modeIndex == mode && SameFilePath(entry.dataset, m_Result->directory, m_ProjectDirectory);
		});
		if (found == m_ModeVectorCache.end()) return;
		const auto entry = *found;
		m_ModeVectorCache.erase(found);
		m_ModeVectorCache.push_back(entry);
	}

	bool PhononPanelSession::cacheModeVector(const Path &dataset, Ref<const PhononModeVector> vector)
	{
		if (!m_Result || !vector || !SameFilePath(dataset, m_Result->directory, m_ProjectDirectory) ||
			vector->modeIndex >= m_Result->ModeCount() || vector->participation.size() != m_Result->AtomCount() ||
			vector->eigenvector.size() != m_Result->AtomCount() * 3 ||
			!std::isfinite(vector->frequencyMeV) ||
			std::abs(vector->frequencyMeV - m_Result->modes[vector->modeIndex].frequencyMeV) > 0.05)
			return false;
		std::erase_if(m_ModeVectorCache, [&](const auto &entry) {
			return entry.vector->modeIndex == vector->modeIndex && SameFilePath(entry.dataset, dataset, m_ProjectDirectory);
		});
		m_Result->fetchedModes[vector->modeIndex] = vector;
		std::error_code ignored;
		const auto modified = std::filesystem::last_write_time((dataset / "summary.json").Native(), ignored);
		m_ModeVectorCache.push_back({dataset, std::move(vector), modified});
		while (m_ModeVectorCache.size() > 8)
		{
			const auto &old = m_ModeVectorCache.front();
			if (SameFilePath(old.dataset, m_Result->directory, m_ProjectDirectory)) m_Result->fetchedModes.erase(old.vector->modeIndex);
			for (auto &[key, cached] : m_Spectra)
				if (cached.result && SameFilePath(old.dataset, cached.result->directory, m_ProjectDirectory))
					cached.result->fetchedModes.erase(old.vector->modeIndex);
			m_ModeVectorCache.pop_front();
		}
		++m_PlotRevision;
		rebuildField();
		return true;
	}

	void PhononPanelSession::SubmitModeVector(bool animate)
	{
		const auto selected = SelectedMode();
		if (!selected || !m_Result || m_Settings.animationSource != "mode") return;
		if (!ModeRows(*m_Result, *selected).eigenvector.empty()) { touchModeVector(*selected); return; }
		if (m_Result->hasEigenvectors) return;
		m_ModeVectorError = ModeVectorBlocker();
		if (!m_ModeVectorError.empty()) return;
		if (m_ModeVectorJob && m_ModeVectorIndex == *selected &&
			SameFilePath(m_ModeVectorDataset, m_Result->directory, m_ProjectDirectory))
		{
			if (animate) m_ModeVectorAnimateWindow = m_WindowId;
			return;
		}
		if (IsBusy() && !m_ModeVectorJob) { m_ModeVectorError = "Poczekaj na zakończenie bieżącego zadania."; return; }
		const auto jobs = m_JobSystem.lock();
		if (!jobs) { m_ModeVectorError = "JobSystem niedostępny."; return; }
		CancelModeVector();
		m_ModeVectorDataset = m_Result->directory;
		m_ModeVectorIndex = *selected;
		m_ModeVectorCase = ActiveCaseId();
		m_ModeVectorAnimateWindow = animate ? m_WindowId : std::string{};
		const auto output = m_ProjectDirectory / "defectstudio/phonons/.mode-vectors" / ToString(GenerateUuid());
		m_ModeVectorJob = CreateRef<PhononModeVectorJob>(m_ServerEmbeddedDirectory, output, *selected);
		m_ModeVectorJobId = jobs->Submit(m_ModeVectorJob, JobPriority::Normal);
	}

	void PhononPanelSession::CancelModeVector()
	{
		if (m_ModeVectorJob) cancelJob(m_ModeVectorJobId);
		m_ModeVectorJob.reset();
		m_ModeVectorJobId = 0;
		m_ModeVectorAnimateWindow.clear();
	}

	void PhononPanelSession::pollModeVectorJob()
	{
		if (!m_ModeVectorJob) return;
		const auto jobs = m_JobSystem.lock();
		const auto snapshot = jobs ? jobs->GetJob(m_ModeVectorJobId) : std::nullopt;
		if (!snapshot) { m_ModeVectorError = "Brak zadania w JobSystem."; CancelModeVector(); return; }
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)) return;
		const bool current = m_Result && m_ModeVectorCase == ActiveCaseId() &&
			SameFilePath(m_ModeVectorDataset, m_Result->directory, m_ProjectDirectory);
		const auto animateWindow = m_ModeVectorAnimateWindow;
		bool loaded = false;
		if (current && snapshot && !snapshot->cancellationRequested && snapshot->status == JobStatus::Completed && m_ModeVectorJob->GetResult())
		{
			PhononModeVector vector;
			if (PhononModeVectorIO::Load(*m_ModeVectorJob->GetResult(), vector, m_ModeVectorError))
			{
				loaded = vector.modeIndex == m_ModeVectorIndex && cacheModeVector(m_ModeVectorDataset, CreateRef<PhononModeVector>(std::move(vector)));
				m_ModeVectorError = loaded ? "" : "Wektor modu nie pasuje do wybranego zestawu (indeks, liczba atomów lub częstotliwość).";
			}
		}
		else if (current)
			m_ModeVectorError = m_ModeVectorJob->GetError() ? m_ModeVectorJob->GetError()->userMessage + "\n" +
				m_ModeVectorJob->GetError()->technicalDetails : snapshot && !snapshot->errorMessage.empty() ?
				snapshot->errorMessage : "Wczytywanie wektora przerwane.";
		if (m_ModeVectorJob->GetResult())
		{
			std::error_code ignored;
			(void)FileSystem::RemoveAll(m_ModeVectorJob->GetResult()->Native(), ignored);
		}
		m_ModeVectorJob.reset();
		m_ModeVectorJobId = 0;
		m_ModeVectorAnimateWindow.clear();
		if (loaded && SelectedMode() == m_ModeVectorIndex && m_Settings.animationSource == "mode" && !animateWindow.empty())
			if (const auto window = targetWindow(); window && window->get().windowId == animateWindow) StartAnimation(window->get());
	}

	void PhononPanelSession::StartAnimation(RendererWindowState &window)
	{
		std::string error;
		if (m_Settings.animationSource == "mode" && m_Result && SelectedMode() &&
			ModeRows(*m_Result, *SelectedMode()).eigenvector.empty() && WindowMatches(window, error))
		{
			SubmitModeVector(true);
			return;
		}
		if (IsAnimating() || Field().real.empty() || !WindowMatches(window, error) ||
			Field().equilibriumAngstrom.size() != window.structure.atoms.size())
			return;
		m_RestPositions.emplace();
		for (const auto &atom : window.structure.atoms)
			m_RestPositions->push_back(atom.cartesianPosition);
		m_Phase = 0.0;
		m_AnimationSource = m_Settings.animationSource;
		if (m_AnimationSource == "ccd_direction")
		{
			const double maximum = CcdSweepMaximum();
			if (Field().motion == VibrationMotion::Sweep && maximum > 0.0)
				m_Phase = std::acos(1.0 - 2.0 * std::clamp(m_Settings.ccdSweepQ / maximum, 0.0, 1.0));
			SetPhase(window, m_Phase);
		}
		if (m_AnimationSource == "bulk_q") SetPhase(window, m_Phase);
		(void)ApplyArrows(window, error);
	}

}
