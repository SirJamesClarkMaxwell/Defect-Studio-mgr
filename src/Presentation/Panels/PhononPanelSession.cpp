#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Core/Platform/PlatformPaths.hpp"
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

	bool PhononPanelSession::SameFilePath(const Path &left, const Path &right, const Path &projectDirectory)
	{
		if (left.Empty() || right.Empty()) return false;
		const auto normalized = [&](const Path &path) {
			auto native = path.Native();
			if (native.is_relative()) native = projectDirectory.Native() / native;
			std::error_code error;
			native = FileSystem::Absolute(native, error);
			if (error) return FilePath{};
			return native.lexically_normal();
		};
		const auto normalizedLeft = normalized(left), normalizedRight = normalized(right);
		if (normalizedLeft.empty() || normalizedRight.empty()) return false;
		return Platform::FileSystemNamesEqual(normalizedLeft, normalizedRight);
	}

	std::pair<std::size_t, std::size_t> PhononPanelSession::GridIndexRange(std::span<const double> grid,
		double minimum, double maximum)
	{
		if (grid.empty() || minimum > maximum) return {0, 0};
		std::size_t first = static_cast<std::size_t>(std::lower_bound(grid.begin(), grid.end(), minimum) - grid.begin());
		std::size_t last = static_cast<std::size_t>(std::upper_bound(grid.begin(), grid.end(), maximum) - grid.begin());
		if (first > 0) --first;
		if (last < grid.size()) ++last;
		return {first, last};
	}

	std::pair<double, double> PhononPanelSession::UsefulRange(const PhononAnalysisResult &result, bool pl, std::string_view plZpl)
	{
		if (pl)
		{
			if (result.plPhotonEnergyEv.empty()) return {0.0, 1.0};
			const auto choice = PlCurveChoice(result, plZpl);
			const auto &curve = PlCurve(result, choice);
			const auto zpl = choice == "computed" ? result.zplEv.computed : choice == "spin_purified" ? result.zplEv.spinPurified :
				choice == "formula" ? result.zplEv.formula : result.zplEv.experimental;
			double minimum = result.plPhotonEnergyEv.front();
			for (std::size_t i = 0; i < curve.size(); ++i)
				if (curve[i] > 1e-3) { minimum = result.plPhotonEnergyEv[i]; break; }
			const double maximum = zpl.value_or(result.plPhotonEnergyEv.back()) + 0.15;
			return {std::min(minimum, maximum - 0.15), maximum};
		}
		if (!result.modes.empty())
		{
			const auto [lowest, highest] = std::minmax_element(result.modes.begin(), result.modes.end(),
				[](const auto &a, const auto &b) { return a.frequencyMeV < b.frequencyMeV; });
			return {std::min(0.0, lowest->frequencyMeV) - 5.0, 1.1 * highest->frequencyMeV};
		}
		const auto &density = result.spectralDensityPerMeV;
		if (density.empty()) return {-5.0, 1.0};
		const double peak = *std::max_element(density.begin(), density.end());
		std::size_t last = 0;
		for (std::size_t i = 0; i < density.size(); ++i)
			if (density[i] > 1e-3 * peak) last = i;
		return {-5.0, 1.1 * result.spectralEnergyMeV[last]};
	}

	std::pair<double, double> PhononPanelSession::ModeEnergyRangeMeV() const
	{
		return m_Result ? UsefulRange(*m_Result, false, m_Settings.plZpl) : std::pair{-5.0, 1.0};
	}

	std::pair<double, double> PhononPanelSession::PlEnergyRangeEv() const
	{
		return m_Result ? UsefulRange(*m_Result, true, m_Settings.plZpl) : std::pair{0.0, 1.0};
	}

	void PhononPanelSession::SetResult(PhononAnalysisResult result)
	{
		StopAnimation();
		++m_PlotRevision;
		if (m_Result) m_Settings.mixAngleDegrees = 0.0;
		m_Result = std::move(result);
		if (SelectedMode())
		{
			m_MixAngle = m_Settings.mixAngleDegrees * std::numbers::pi / 180.0;
			rebuildField();
			return;
		}
		m_Settings.selectedMode = -1;
		for (std::size_t i = 0; i < m_Result->ModeCount(); ++i)
			if (!m_Result->modes[i].isTranslation && (m_Settings.selectedMode < 0 ||
				m_Result->modes[i].huangRhys > m_Result->modes[m_Settings.selectedMode].huangRhys))
				m_Settings.selectedMode = static_cast<int>(i);
		m_MixAngle = m_Settings.mixAngleDegrees * std::numbers::pi / 180.0;
		rebuildField();
	}

	void PhononPanelSession::ClearResult()
	{
		StopAnimation();
		++m_PlotRevision;
		m_Result.reset();
		m_Settings.selectedMode = -1;
		m_Settings.mixAngleDegrees = 0.0;
		m_MixAngle = 0.0;
		rebuildField();
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
			if (!m_Result->modes[i].isTranslation && m_Result->modes[i].huangRhys >= m_Settings.minHuangRhys &&
				(m_Settings.irrepFilter.empty() ||
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
		{
			const bool running = IsAnimating();
			const bool paused = m_Paused;
			StopAnimation();
			if (m_Settings.selectedMode != modeIndex) m_Settings.mixAngleDegrees = 0.0;
			m_Settings.selectedMode = modeIndex;
			rebuildField();
			(void)Field();
			if (const auto window = targetWindow(); window && running) StartAnimation(window->get());
			m_Paused = paused;
		}
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
		return FieldArrows(Field(), std::nullopt, m_Settings.participationThreshold,
			m_Settings.arrowLengthAngstrom).size();
	}

	void PhononPanelSession::rebuildField() const
	{
		m_FieldMode = SelectedMode();
		m_FieldSource = m_Settings.animationSource;
		m_FieldMotion = m_Settings.ccdMotion;
		m_FieldCase = ActiveCaseId();
		m_FieldRevision = m_PlotRevision;
		m_MixAngle = m_Settings.mixAngleDegrees * std::numbers::pi / 180.0;
		if (m_FieldSource == "ccd_direction")
		{
			m_Field = m_CcdResult && m_CcdResult->displacement ? CcdDirectionField(*m_CcdResult->displacement) : VibrationField{};
			m_Field.motion = m_FieldMotion == "sweep" ? VibrationMotion::Sweep : VibrationMotion::Oscillate;
		}
		else
			m_Field = m_FieldMode ? DefectModeField(*m_Result, *m_FieldMode,
				DegeneratePartnerOfSelected(), m_MixAngle) : VibrationField{};
	}

	const VibrationField &PhononPanelSession::Field() const
	{
		// Settings() remains mutable: detect legacy callers changing the selection directly.
		if (m_FieldMode != SelectedMode() && m_FieldCase == ActiveCaseId() && m_FieldRevision == m_PlotRevision)
		{
			m_Settings.mixAngleDegrees = 0.0;
			m_MixAngle = 0.0;
			rebuildField();
		}
		if (m_FieldRevision != m_PlotRevision || m_FieldCase != ActiveCaseId() ||
			m_FieldSource != m_Settings.animationSource || m_FieldMotion != m_Settings.ccdMotion ||
			m_MixAngle != m_Settings.mixAngleDegrees * std::numbers::pi / 180.0)
			rebuildField();
		return m_Field;
	}

	void PhononPanelSession::SetMixAngle(double radians)
	{
		(void)Field();
		if (!std::isfinite(radians) || !DegeneratePartnerOfSelected())
			return;
		radians = std::clamp(radians, 0.0, std::numbers::pi);
		if (m_MixAngle == radians)
			return;
		m_MixAngle = radians;
		m_Settings.mixAngleDegrees = radians * 180.0 / std::numbers::pi;
		rebuildField();
		Changed();
	}

	double PhononPanelSession::MixAngle() const
	{
		(void)Field();
		return m_MixAngle;
	}

	std::optional<std::size_t> PhononPanelSession::DegeneratePartnerOfSelected() const
	{
		const auto mode = SelectedMode();
		return mode ? DegeneratePartner(*m_Result, *mode) : std::nullopt;
	}

	std::size_t PhononPanelSession::AtomsCarryingSelectedFraction() const
	{
		const auto mode = SelectedMode();
		return mode ? AtomsCarryingFraction(*m_Result, *mode, m_Settings.carriedFraction) : 0;
	}

	bool PhononPanelSession::WindowMatches(const RendererWindowState &window, std::string &outError) const
	{
		outError.clear();
		if (m_Settings.animationSource == "ccd_direction")
		{
			outError = CcdDirectionBlocker();
			if (!outError.empty()) return false;
			const auto &symbols = Field().symbols;
			if (window.structure.atoms.size() != symbols.size())
				outError = "Liczba atomów okna różni się od geometrii CCD.";
			else
				for (std::size_t i = 0; i < symbols.size(); ++i)
					if (window.structure.atoms[i].element != symbols[i])
					{
						outError = "Pierwiastki lub kolejność atomów różnią się od geometrii CCD.";
						break;
					}
			return outError.empty();
		}
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

	bool PhononPanelSession::CanBindWindow(const RendererWindowState &window, const Path &supercell,
		const Path &projectDirectory) const
	{
		for (const auto &[id, windowId] : m_CaseWindows)
			if (id != ActiveCaseId() && windowId == window.windowId) return false;
		for (const auto &item : m_Settings.cases)
			if (item.id != ActiveCaseId() && window.title != CaseWindowTitle() &&
				window.title == std::string(SupercellWindowTitle) + " – " + item.name)
				return false;
		if (m_Settings.animationSource == "ccd_direction")
		{
			std::string error;
			return WindowMatches(window, error);
		}
		if (SameFilePath(window.structure.sourcePath, supercell, projectDirectory)) return true;
		std::string error;
		return (window.title == CaseWindowTitle() || window.title == SupercellWindowTitle) && WindowMatches(window, error);
	}

	bool PhononPanelSession::ApplyArrows(RendererWindowState &window, std::string &outError) const
	{
		window.modeArrows.reset();
		if (m_Settings.animationSource != "ccd_direction" && !m_Result)
		{
			outError.clear();
			return true;
		}
		if (!WindowMatches(window, outError))
			return false;
		if (Field().real.empty() || !m_Settings.showArrows)
			return true;
		ModeArrowsState arrows;
		arrows.lowColor = m_Settings.arrowLowColor;
		arrows.highColor = m_Settings.arrowHighColor;
		const auto &field = Field();
		// Keep cycle-scaled arrows: they shrink to zero at equilibrium, like the animated atoms.
		const bool followPhase = IsAnimating() && m_Settings.arrowsFollowPhase;
		const auto displacements = followPhase ? FieldDisplacements(field, m_Phase, m_Settings.arrowLengthAngstrom,
			m_Settings.ccdExtent) :
			std::vector<glm::dvec3>{};
		for (const auto &arrow : FieldArrows(field, std::nullopt, m_Settings.participationThreshold,
			m_Settings.arrowLengthAngstrom))
			arrows.arrows.push_back({arrow.atomIndex, glm::vec3(followPhase ? displacements[arrow.atomIndex] : arrow.vectorAngstrom)});
		window.modeArrows = std::move(arrows);
		return true;
	}

	void PhononPanelSession::StartAnimation(RendererWindowState &window)
	{
		std::string error;
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
		(void)ApplyArrows(window, error);
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
		if (!IsAnimating() || Field().real.empty() || !std::isfinite(phaseRadians) || !WindowMatches(window, error) ||
			m_RestPositions->size() != window.structure.atoms.size() ||
			Field().equilibriumAngstrom.size() != window.structure.atoms.size())
			return;
		m_Phase = phaseRadians;
		const auto &field = Field();
		const auto frame = FieldDisplacements(field, m_Phase, m_Settings.animationAmplitudeAngstrom, m_Settings.ccdExtent);
		for (std::size_t i = 0; i < frame.size(); ++i)
			window.structure.atoms[i].cartesianPosition = (*m_RestPositions)[i] +
				glm::vec3(frame[i]);
		if (m_Settings.animationSource == "ccd_direction" && field.motion == VibrationMotion::Sweep)
		{
			m_Settings.ccdSweepQ = CcdSweepMaximum() * (1.0 - std::cos(m_Phase)) / 2.0;
		}
		(void)ApplyArrows(window, error);
	}

	void PhononPanelSession::StopAnimation(RendererWindowState &window)
	{
		if (IsAnimating() && m_AnimationSource == "ccd_direction") Changed();
		if (m_RestPositions && m_RestPositions->size() == window.structure.atoms.size())
			for (std::size_t i = 0; i < m_RestPositions->size(); ++i)
				window.structure.atoms[i].cartesianPosition = (*m_RestPositions)[i];
		m_RestPositions.reset();
		m_Phase = 0.0;
		std::string error;
		(void)ApplyArrows(window, error);
	}

	std::string PhononPanelSession::CcdDirectionBlocker() const
	{
		if (!m_CcdResult) return "Brak wybranego zestawu CCD.";
		if (!m_CcdResult->displacement) return "Zestaw CCD bez ΔR — uruchom ponownie „Policz CCD”.";
		if (m_Settings.animationSource == "ccd_direction" && Field().real.empty()) return "Nieprawidłowy wektor ΔR w zestawie CCD.";
		return {};
	}

	void PhononPanelSession::SetAnimationSource(std::string source)
	{
		if ((source != "mode" && source != "ccd_direction") || m_Settings.animationSource == source) return;
		StopAnimation();
		if (const auto window = targetWindow()) window->get().modeArrows.reset();
		m_Settings.animationSource = std::move(source);
		rebuildField();
		if (m_Windows) syncWindow(0.0);
		Changed();
	}

	void PhononPanelSession::SetCcdMotion(std::string motion)
	{
		if ((motion != "sweep" && motion != "oscillate") || m_Settings.ccdMotion == motion) return;
		StopAnimation();
		m_Settings.ccdMotion = std::move(motion);
		rebuildField();
		Changed();
	}

	double PhononPanelSession::CcdSweepMaximum() const
	{
		return m_CcdResult && m_CcdResult->displacement ? m_Settings.ccdExtent * m_CcdResult->displacement->deltaQ : 0.0;
	}

	void PhononPanelSession::SetCcdSweepQ(RendererWindowState &window, double q)
	{
		const double maximum = CcdSweepMaximum();
		if (!std::isfinite(q) || maximum <= 0.0 || m_Settings.animationSource != "ccd_direction" ||
			m_Settings.ccdMotion != "sweep") return;
		std::string error;
		if (!WindowMatches(window, error)) return;
		if (!IsAnimating()) StartAnimation(window);
		m_Paused = true;
		SetPhase(window, std::acos(1.0 - 2.0 * std::clamp(q / maximum, 0.0, 1.0)));
		Changed();
	}

	void PhononPanelSession::SetCcdExtent(double extent)
	{
		if (!std::isfinite(extent)) return;
		m_Settings.ccdExtent = std::clamp(extent, 1.0, 2.0);
		m_Settings.ccdSweepQ = std::min(m_Settings.ccdSweepQ, CcdSweepMaximum());
		if (const auto window = targetWindow(); window && IsAnimating() && m_Paused)
			SetCcdSweepQ(window->get(), m_Settings.ccdSweepQ);
		Changed();
	}
}
