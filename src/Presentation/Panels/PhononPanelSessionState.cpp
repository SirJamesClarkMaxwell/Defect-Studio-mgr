#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/PhononAnalysisIO.hpp"
#include "IO/CcdAnalysisIO.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"
#include "ScientificRuntime/Python/CcdInputsJob.hpp"
#include "ScientificRuntime/Python/CcdAnalysisJob.hpp"
#include "ScientificRuntime/Python/PhononDispersionJob.hpp"

namespace DefectStudio
{
	void PhononPanelSession::SetPersistedState(PhononPanelSettings settings, Path projectDirectory)
	{
		clearWindow();
		cancelJob();
		CancelDatasetScan();
		SetDatasets({});
		ClearResult();
		ClearCcdResult();
		Settings() = std::move(settings);
		m_ProjectDirectory = std::move(projectDirectory);
		m_CurrentDataset = {};
		m_Changed = false;
		++m_PlotRevision;
		m_ObservedPlZpl = Settings().plZpl;
		m_DefaultDatasetDirectories.clear();
		m_Dispersion.reset();
		m_DispersionError.clear();
		m_Error.clear();
		m_CcdInputsResult.clear();
		m_CcdError.clear();
		m_CcdAnalysisError.clear();
		m_EmbeddingSummary.clear();
		m_EmbeddingMult = {12, 12, 1};
		if (!Settings().embeddingMult.empty())
		{
			std::istringstream mult(Settings().embeddingMult);
			char first = 0, second = 0;
			std::array<int, 3> values;
			if (mult >> values[0] >> first >> values[1] >> second >> values[2] && first == 'x' && second == 'x' &&
				mult.peek() == std::char_traits<char>::eof() && std::all_of(values.begin(), values.end(), [](int n) { return n > 0; }))
				m_EmbeddingMult = values;
			else
				Settings().embeddingMult.clear();
		}
		if (!Settings().resultDirectory.Empty())
			loadResult(Settings().resultDirectory);
		if (!Settings().selectedDispersionDirectory.Empty())
			(void)loadDispersion(Settings().selectedDispersionDirectory);
		if (!Settings().selectedCcdDirectory.Empty())
			(void)loadCcdResult(Settings().selectedCcdDirectory);
		for (const auto &path : Settings().comparedDirectories)
			if (auto ref = ReadDatasetRef(path)) AddDataset(std::move(*ref));
		// The loads above each made their own row current; on project open the phonon result wins.
		const auto &loaded = Settings();
		SetCurrentDataset(!loaded.resultDirectory.Empty() ? loaded.resultDirectory :
			!loaded.selectedCcdDirectory.Empty() ? loaded.selectedCcdDirectory : loaded.selectedDispersionDirectory);
	}

	void PhononPanelSession::SetDirectory(const std::string &role, const Path &directory)
	{
		if (role != "fc" && role != "gs" && role != "es" && role != "triplet" && role != "bulk" && role != "result") return;
		cancelJob();
		auto &settings = Settings();
		if (role == "fc")
		{
			clearWindow();
			settings.forceConstantDirectory = directory;
		}
		else if (role == "gs") settings.groundStateDirectory = directory;
		else if (role == "es") settings.excitedStateDirectory = directory;
		else if (role == "triplet") settings.tripletDirectory = directory;
		else if (role == "bulk") settings.bulkForceConstantDirectory = directory;
		else if (role == "result")
		{
			loadResult(directory);
		}
		else return;
		ShowViews(true);
		Changed();
	}

	void PhononPanelSession::loadResult(const Path &directory)
	{
		if (auto ref = ReadDatasetRef(directory)) AddDataset(std::move(*ref));
		PhononAnalysisResult result;
		if (!PhononAnalysisIO::Load(directory, result, m_Error))
			return;
		StopAnimation();
		SetResult(std::move(result));
		Settings().resultDirectory = directory;
		(void)SelectDataset(directory);
		loadEmbeddingSummary(directory);
		const auto &loaded = *Result();
		auto &zpl = Settings().plZpl;
		zpl = PhononPanelSession::PlCurveChoice(loaded, zpl);
		m_Error.clear();
	}

	void PhononPanelSession::SetProjectRoots(std::vector<ProjectRootEntry> roots)
	{
		m_ProjectRoots = std::move(roots);
		scanDatasets();
	}

	void PhononPanelSession::scanDatasets()
	{
		m_DefaultDatasetDirectories.clear();
		std::vector<Path> roots;
		if (!m_ProjectDirectory.Empty()) roots.push_back(m_ProjectDirectory);
		for (const auto &root : m_ProjectRoots) roots.push_back(root.path);
		StartDatasetScan(m_JobSystem, std::move(roots));
	}

	Path PhononPanelSession::defaultDatasetPath(const std::vector<Path> &inputs, const std::string &kind) const
	{
		std::string key = m_ProjectDirectory.Utf8() + "\n" + kind;
		for (const auto &input : inputs) key += "\n" + input.Utf8();
		const auto found = m_DefaultDatasetDirectories.find(key);
		if (found != m_DefaultDatasetDirectories.end()) return found->second;
		return m_DefaultDatasetDirectories.emplace(key,
			DefaultDatasetDirectory(inputs, kind, m_ProjectDirectory, IsDirectoryWritable)).first->second;
	}

	Path PhononPanelSession::computationOutputPath() const
	{
		const auto &s = Settings();
		if (!s.outputDirectory.Empty()) return s.outputDirectory;
		std::vector<Path> inputs{s.forceConstantDirectory, s.groundStateDirectory, s.excitedStateDirectory,
			s.tripletDirectory, s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
		if (!s.embeddingMult.empty()) inputs.push_back(s.bulkForceConstantDirectory);
		return defaultDatasetPath(inputs, s.embeddingMult.empty() ? "phonons" : "phonons_emb_" + s.embeddingMult);
	}
}
