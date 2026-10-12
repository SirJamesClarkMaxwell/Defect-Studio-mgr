#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	std::string PhononPanelSession::DirectoryLabel(const Path &path, const Path &projectDirectory,
		const std::vector<ProjectRootEntry> &roots)
	{
		if (path.Empty()) return {};
		const auto relativeTo = [&](const Path &root) -> std::optional<std::string> {
			if (root.Empty()) return std::nullopt;
			const auto relative = path.Native().lexically_normal().lexically_relative(root.Native().lexically_normal());
			if (relative.empty() || relative.is_absolute() ||
				std::any_of(relative.begin(), relative.end(), [](const auto &part) { return part == ".."; }))
				return std::nullopt;
			const auto text = relative.generic_u8string();
			return std::string(text.begin(), text.end());
		};
		if (const auto relative = relativeTo(projectDirectory)) return *relative;
		for (const auto &root : roots)
			if (const auto relative = relativeTo(root.path))
				return (root.label.empty() ? root.path.filename().Utf8() : root.label) + "/" + *relative;
		return path.Utf8();
	}

	Path PhononPanelSession::ResolveDirectoryText(std::string_view text, const Path &projectDirectory)
	{
		const auto first = text.find_first_not_of(" \t\r\n\"'");
		if (first == std::string_view::npos) return {};
		text = text.substr(first, text.find_last_not_of(" \t\r\n\"'") - first + 1);
		const Path path = Path::FromUtf8(text);
		const Path base = projectDirectory.Empty() ? Path::FromResolved(FileSystem::CurrentPath()) : projectDirectory;
		return Path::FromResolved((path.Native().is_absolute() ? path.Native() :
			base.Native() / path.Native()).lexically_normal());
	}

	std::pair<double, double> PhononPanelSession::ModeEnergyRangeMeV() const
	{
		if (!m_Result || m_Result->modes.empty()) return {-5.0, 1.0};
		const auto [lowest, highest] = std::minmax_element(m_Result->modes.begin(), m_Result->modes.end(),
			[](const auto &a, const auto &b) { return a.frequencyMeV < b.frequencyMeV; });
		return {std::min(0.0, lowest->frequencyMeV) - 5.0, 1.1 * highest->frequencyMeV};
	}

	std::pair<double, double> PhononPanelSession::PlEnergyRangeEv() const
	{
		if (!m_Result || m_Result->plPhotonEnergyEv.empty()) return {0.0, 1.0};
		const auto &result = *m_Result;
		const auto &curve = m_Settings.plZpl == "computed" ? result.plComputed :
			m_Settings.plZpl == "spin_purified" ? result.plSpinPurified : result.plExperimental;
		const auto zpl = m_Settings.plZpl == "computed" ? result.zplEv.computed :
			m_Settings.plZpl == "spin_purified" ? result.zplEv.spinPurified : result.zplEv.experimental;
		double minimum = result.plPhotonEnergyEv.front();
		for (std::size_t i = 0; i < curve.size(); ++i)
			if (curve[i] > 1e-3) { minimum = result.plPhotonEnergyEv[i]; break; }
		const double maximum = zpl.value_or(result.plPhotonEnergyEv.back()) + 0.15;
		return {std::min(minimum, maximum - 0.15), maximum};
	}

	void PhononPanelSession::SetResult(PhononAnalysisResult result)
	{
		m_Result = std::move(result);
		if (SelectedMode())
			return;
		m_Settings.selectedMode = -1;
		for (std::size_t i = 0; i < m_Result->ModeCount(); ++i)
			if (!m_Result->modes[i].isTranslation && (m_Settings.selectedMode < 0 ||
				m_Result->modes[i].huangRhys > m_Result->modes[m_Settings.selectedMode].huangRhys))
				m_Settings.selectedMode = static_cast<int>(i);
	}

	void PhononPanelSession::ClearResult()
	{
		m_Result.reset();
		m_Settings.selectedMode = -1;
	}

	const PhononAnalysisResult *PhononPanelSession::Result() const noexcept
	{
		return m_Result ? &*m_Result : nullptr;
	}

	std::vector<std::size_t> PhononPanelSession::VisibleModes() const
	{
		std::vector<std::size_t> modes;
		if (!m_Result)
			return modes;
		for (std::size_t i = 0; i < m_Result->ModeCount(); ++i)
			if (!m_Result->modes[i].isTranslation && (m_Settings.irrepFilter.empty() ||
				m_Result->modes[i].irrep == m_Settings.irrepFilter))
				modes.push_back(i);
		std::stable_sort(modes.begin(), modes.end(), [&](auto a, auto b) {
			return m_Result->modes[a].frequencyMeV < m_Result->modes[b].frequencyMeV;
		});
		return modes;
	}

	std::vector<std::string> PhononPanelSession::Irreps() const
	{
		std::vector<std::string> irreps;
		if (m_Result)
			for (const auto &mode : m_Result->modes)
				if (!mode.isTranslation)
					irreps.push_back(mode.irrep);
		std::sort(irreps.begin(), irreps.end());
		irreps.erase(std::unique(irreps.begin(), irreps.end()), irreps.end());
		return irreps;
	}

	void PhononPanelSession::SelectMode(int modeIndex)
	{
		if (m_Result && modeIndex >= 0 && static_cast<std::size_t>(modeIndex) < m_Result->ModeCount() &&
			!m_Result->modes[modeIndex].isTranslation)
			m_Settings.selectedMode = modeIndex;
	}

	std::optional<std::size_t> PhononPanelSession::SelectedMode() const
	{
		const int index = m_Settings.selectedMode;
		if (!m_Result || index < 0 || static_cast<std::size_t>(index) >= m_Result->ModeCount() ||
			m_Result->modes[index].isTranslation)
			return std::nullopt;
		return static_cast<std::size_t>(index);
	}

	std::size_t PhononPanelSession::ArrowCount() const
	{
		const auto mode = SelectedMode();
		return mode ? BuildModeArrows(*m_Result, *mode, m_Settings.participationThreshold,
			m_Settings.arrowLengthAngstrom).size() : 0;
	}

	std::size_t PhononPanelSession::AtomsCarryingSelectedFraction() const
	{
		const auto mode = SelectedMode();
		return mode ? AtomsCarryingFraction(*m_Result, *mode, m_Settings.carriedFraction) : 0;
	}

	bool PhononPanelSession::WindowMatches(const RendererWindowState &window, std::string &outError) const
	{
		outError.clear();
		if (!m_Result)
			outError = "Brak wyniku fononowego.";
		else if (window.structure.atoms.size() != m_Result->AtomCount())
			outError = "Liczba atomów okna różni się od superkomórki FC.";
		else
			for (std::size_t i = 0; i < m_Result->AtomCount(); ++i)
				if (window.structure.atoms[i].element != m_Result->symbols[i])
				{
					outError = "Pierwiastki lub kolejność atomów różnią się od superkomórki FC.";
					break;
				}
		return outError.empty();
	}

	bool PhononPanelSession::ApplyArrows(RendererWindowState &window, std::string &outError) const
	{
		window.modeArrows.reset();
		if (!m_Result)
		{
			outError.clear();
			return true;
		}
		if (!WindowMatches(window, outError))
			return false;
		const auto mode = SelectedMode();
		if (!mode || !m_Settings.showArrows)
			return true;
		ModeArrowsState arrows;
		arrows.lowColor = m_Settings.arrowLowColor;
		arrows.highColor = m_Settings.arrowHighColor;
		for (const auto &arrow : BuildModeArrows(*m_Result, *mode, m_Settings.participationThreshold,
			m_Settings.arrowLengthAngstrom))
			arrows.arrows.push_back({arrow.atomIndex, glm::vec3(arrow.vectorAngstrom)});
		window.modeArrows = std::move(arrows);
		return true;
	}

	void PhononPanelSession::StartAnimation(RendererWindowState &window)
	{
		std::string error;
		if (IsAnimating() || !SelectedMode() || !WindowMatches(window, error) ||
			m_Result->positionsAngstrom.size() != window.structure.atoms.size())
			return;
		m_RestPositions.emplace();
		for (const auto &atom : window.structure.atoms)
			m_RestPositions->push_back(atom.cartesianPosition);
		m_Phase = 0.0;
	}

	void PhononPanelSession::AdvanceAnimation(RendererWindowState &window, double deltaSeconds)
	{
		if (!IsAnimating() || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0 ||
			!std::isfinite(m_Settings.animationPeriodSeconds) || m_Settings.animationPeriodSeconds <= 0.0)
			return;
		SetPhase(window, std::remainder(m_Phase + 2.0 * std::numbers::pi * deltaSeconds /
			m_Settings.animationPeriodSeconds, 2.0 * std::numbers::pi));
	}

	void PhononPanelSession::SetPhase(RendererWindowState &window, double phaseRadians)
	{
		std::string error;
		const auto mode = SelectedMode();
		if (!IsAnimating() || !mode || !std::isfinite(phaseRadians) || !WindowMatches(window, error) ||
			m_RestPositions->size() != window.structure.atoms.size() ||
			m_Result->positionsAngstrom.size() != window.structure.atoms.size())
			return;
		m_Phase = phaseRadians;
		const auto frame = AnimatedPositions(*m_Result, *mode, m_Phase, m_Settings.animationAmplitudeAngstrom);
		for (std::size_t i = 0; i < frame.size(); ++i)
			window.structure.atoms[i].cartesianPosition = (*m_RestPositions)[i] +
				glm::vec3(frame[i] - m_Result->positionsAngstrom[i]);
	}

	void PhononPanelSession::StopAnimation(RendererWindowState &window)
	{
		if (m_RestPositions && m_RestPositions->size() == window.structure.atoms.size())
			for (std::size_t i = 0; i < m_RestPositions->size(); ++i)
				window.structure.atoms[i].cartesianPosition = (*m_RestPositions)[i];
		m_RestPositions.reset();
		m_Phase = 0.0;
	}
}
