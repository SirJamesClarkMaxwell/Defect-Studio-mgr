#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>

namespace DefectStudio
{
	void PhononPanelSession::SetCcdResult(CcdAnalysisResult result) { m_CcdResult = std::move(result); ++m_PlotRevision; }
	void PhononPanelSession::ClearCcdResult() { m_CcdResult.reset(); ++m_PlotRevision; }
	std::optional<std::reference_wrapper<const CcdAnalysisResult>> PhononPanelSession::CcdResult() const noexcept
	{
		return m_CcdResult ? std::optional<std::reference_wrapper<const CcdAnalysisResult>>(std::cref(*m_CcdResult)) : std::nullopt;
	}

	std::pair<double, double> PhononPanelSession::UsefulRange(const CcdAnalysisResult &result)
	{
		double minimum = result.zplEv;
		for (const auto &curve : {std::cref(result.pl1d.numerov), std::cref(result.pl1d.harmonic), std::cref(result.pl1d.phonon1d)})
			for (std::size_t i = 0; i < std::min(curve.get().size(), result.pl1d.energyEv.size()); ++i)
				if (curve.get()[i] > 1e-3) { minimum = std::min(minimum, result.pl1d.energyEv[i]); break; }
		return {minimum, result.zplEv + 0.15};
	}

	std::optional<std::pair<double, double>> PhononPanelSession::CcdLevelRange(
		const CcdAnalysisResult &result, bool excited, std::size_t n)
	{
		const auto &q = result.curves.q;
		const auto &v = excited ? result.curves.vEsEv : result.curves.vGsEv;
		const auto &levels = excited ? result.levels.esMeV : result.levels.gsMeV;
		if (n >= levels.size() || q.size() < 3 || v.size() != q.size()) return std::nullopt;
		const double energy = (excited ? result.zplEv : 0.0) + levels[n] / 1000.0;
		const auto bottom = static_cast<std::size_t>(std::min_element(v.begin(), v.end()) - v.begin());
		if (v[bottom] >= energy) return std::nullopt;
		const auto crossing = [&](std::size_t a, std::size_t b) {
			return q[a] + (energy - v[a]) * (q[b] - q[a]) / (v[b] - v[a]);
		};
		std::optional<double> left, right;
		for (std::size_t i = bottom; i > 0; --i)
			if (v[i - 1] >= energy) { left = crossing(i, i - 1); break; }
		for (std::size_t i = bottom; i + 1 < q.size(); ++i)
			if (v[i + 1] >= energy) { right = crossing(i, i + 1); break; }
		if (!left || !right || !std::isfinite(*left) || !std::isfinite(*right)) return std::nullopt;
		return std::pair{*left, *right};
	}
}
