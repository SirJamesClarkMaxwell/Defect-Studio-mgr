#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <cmath>
#include <sstream>
#include "IO/PhononAnalysisIO.hpp"

namespace DefectStudio
{
	PhononAnalysisRequest PhononPanelSession::computationRequest(const PhononCaseSettings &settings, const std::string &caseId) const
	{
		PhononAnalysisRequest request;
		request.forceConstantDirectory = settings.forceConstantDirectory;
		request.groundStateDirectory = settings.groundStateDirectory;
		request.excitedStateDirectory = settings.excitedStateDirectory;
		if (!settings.tripletDirectory.Empty()) request.tripletDirectory = settings.tripletDirectory;
		request.zplFormula = settings.zplFormula;
		request.groundAtExcitedDirectory = settings.groundAtExcitedDirectory;
		request.excitedAtGroundDirectory = settings.excitedAtGroundDirectory;
		request.sigmaMeV = settings.sigmaMeV;
		request.gammaMeV = settings.gammaMeV;
		if (!settings.embeddingMult.empty())
		{
			request.bulkForceConstantDirectory = settings.bulkForceConstantDirectory;
			std::array<int, 3> mult = {12, 12, 1};
			std::istringstream input(settings.embeddingMult);
			char separator;
			input >> mult[0] >> separator >> mult[1] >> separator >> mult[2];
			request.embeddingMult = mult;
			request.dcutoffAngstrom = settings.dcutoffAngstrom;
			request.bcutoffAngstrom = settings.bcutoffAngstrom;
		}
		request.outputDirectory = computationOutputPath(settings, caseId);
		return request;
	}

	std::string PhononPanelSession::ComputationBlocker() const
	{
		return ComputationBlocker(Settings(), ActiveCaseId());
	}

	std::string PhononPanelSession::ComputationBlocker(const PhononCaseSettings &settings, const std::string &caseId) const
	{
		if (m_JobSystem.expired()) return "JobSystem niedostępny.";
		const auto output = computationOutputPath(settings, caseId);
		if (settings.forceConstantDirectory.Empty()) return "Ustaw katalog stałych siłowych (FC).";
		if (settings.groundStateDirectory.Empty()) return "Ustaw stan podstawowy.";
		if (settings.excitedStateDirectory.Empty()) return "Ustaw stan wzbudzony.";
		if (output.Empty()) return "Ustaw katalog projektu lub katalog wyników.";
		if (!settings.embeddingMult.empty() && settings.bulkForceConstantDirectory.Empty())
			return "Embedding włączony bez katalogu FC bulku - ustaw go albo odznacz Embedding.";
		for (const Path &input : {settings.forceConstantDirectory, settings.groundStateDirectory,
			settings.excitedStateDirectory, settings.tripletDirectory, settings.groundAtExcitedDirectory,
			settings.excitedAtGroundDirectory, settings.embeddingMult.empty() ? Path{} : settings.bulkForceConstantDirectory})
		{
			if (PhononPanelSession::OutputContainsInput(output, input))
				return "Katalog wyników nie może być katalogiem wejściowym ani go zawierać.";
			if (input.Empty()) continue;
			std::error_code error;
			if (!std::filesystem::is_directory(input.Native(), error)) return "Katalog wejściowy nie istnieje: " + input.Utf8();
		}
		if (!std::isfinite(settings.sigmaMeV) || settings.sigmaMeV <= 0 ||
			!std::isfinite(settings.gammaMeV) || settings.gammaMeV <= 0)
			return "sigma i gamma muszą być dodatnie.";
		if (!settings.embeddingMult.empty())
		{
			std::istringstream input(settings.embeddingMult);
			std::array<int, 3> mult;
			char first = 0, second = 0;
			if (!(input >> mult[0] >> first >> mult[1] >> second >> mult[2]) || first != 'x' || second != 'x' ||
				input.peek() != std::char_traits<char>::eof() || mult[0] < 1 || mult[1] < 1 || mult[2] < 1)
				return "Embedding wymaga dodatniego mult N x M x K.";
			for (const auto cutoff : {settings.dcutoffAngstrom, settings.bcutoffAngstrom})
				if (cutoff && (!std::isfinite(*cutoff) || *cutoff <= 0)) return "Promienie embeddingu muszą być dodatnie.";
		}
		return {};

	}

	CcdAnalysisRequest PhononPanelSession::ccdAnalysisRequest(const PhononCaseSettings &s, const std::string &caseId) const
	{
		CcdAnalysisRequest request;
		request.ccdDir = s.ccdDirectory;
		if (s.ccdDirectory.Empty() && !s.groundStateDirectory.Empty())
		{
			request.gsDir = s.groundStateDirectory; request.esDir = s.excitedStateDirectory;
			request.tripletDir = s.tripletDirectory;
			request.gsAtEsDir = s.groundAtExcitedDirectory; request.esAtGsDir = s.excitedAtGroundDirectory;
		}
		request.zplFormula = s.zplFormula;
		if (s.ccdPhononCurvature) request.phononResultDir = s.resultDirectory;
		request.sigmaMeV = s.sigmaMeV; request.temperatureK = s.ccdTemperatureK;
		request.label = PhononPanelSession::DirectoryLabel(!s.ccdDirectory.Empty() ? s.ccdDirectory :
			!s.excitedStateDirectory.Empty() ? s.excitedStateDirectory : s.resultDirectory,
			m_ProjectDirectory, m_ProjectRoots);
		request.outputDirectory = ccdAnalysisOutputPath(s, caseId);
		return request;
	}

	std::string PhononPanelSession::ccdAnalysisBlocker() const
	{
		return ccdAnalysisBlocker(Settings(), ActiveCaseId());
	}

	std::string PhononPanelSession::ccdAnalysisBlocker(const PhononCaseSettings &s, const std::string &caseId,
		bool plannedPhonons) const
	{
		if (m_JobSystem.expired()) return "JobSystem niedostępny.";
		PhononAnalysisResult spectra;
		std::string phononError;
		const bool hasPhonons = plannedPhonons || (s.ccdPhononCurvature && !s.resultDirectory.Empty() &&
			(caseId == ActiveCaseId() ? Result() != nullptr : PhononAnalysisIO::LoadSpectra(s.resultDirectory, spectra, phononError)));
		if (s.ccdPhononCurvature && !plannedPhonons && !hasPhonons)
			return "Wczytaj aktywny zestaw fononów dla krzywizny.";
		std::vector<Path> inputs;
		const bool points = !s.groundStateDirectory.Empty() || !s.excitedStateDirectory.Empty();
		if ((!s.ccdDirectory.Empty() || !points) &&
			(s.zplFormula.find("E_gs") != std::string::npos || s.zplFormula.find("E_es") != std::string::npos || s.zplFormula.find("E_T") != std::string::npos))
			return "Wzór z energiami wymaga punktów GS/ES. Dla ścieżki CCD / samego modu fononowego podaj liczbę ZPL lub zostaw wzór pusty.";
		if (!s.ccdDirectory.Empty()) inputs.push_back(s.ccdDirectory);
		else if (points)
		{
			if (s.groundStateDirectory.Empty() || s.excitedStateDirectory.Empty()) return "Ustaw GS i ES w sekcji Wejścia lub katalog ścieżki CCD.";
			if (s.groundAtExcitedDirectory.Empty() && s.excitedAtGroundDirectory.Empty() && !s.ccdPhononCurvature)
				return "Brak krzywizny: ustaw E_gs@es / E_es@gs lub włącz krzywiznę z fononów.";
			inputs = {s.groundStateDirectory, s.excitedStateDirectory, s.tripletDirectory,
				s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
		}
		else if (!s.ccdPhononCurvature) return "Ustaw GS/ES, ścieżkę CCD lub krzywiznę z aktywnych fononów.";
		if (s.ccdPhononCurvature) inputs.push_back(s.resultDirectory);
		const auto output = ccdAnalysisOutputPath(s, caseId);
		if (output.Empty()) return "Ustaw katalog projektu lub katalog wyników CCD.";
		for (const auto &input : inputs)
		{
			if (input.Empty()) continue;
			std::error_code error;
			if (!(plannedPhonons && s.ccdPhononCurvature && SameFilePath(input, s.resultDirectory, m_ProjectDirectory)) &&
				!std::filesystem::is_directory(input.Native(), error)) return "Katalog wejściowy nie istnieje: " + input.Utf8();
			if (PhononPanelSession::OutputContainsInput(output, input)) return "Katalog wyników nie może być wejściem ani go zawierać.";
		}
		if (!std::isfinite(s.sigmaMeV) || s.sigmaMeV <= 0 || !std::isfinite(s.ccdTemperatureK) || s.ccdTemperatureK < 0)
			return "sigma musi być dodatnia, a T nieujemne.";
		return {};
	}

	Path PhononPanelSession::computationOutputPath(const PhononCaseSettings &s, const std::string &caseId) const
	{
		return CaseOutputDirectory(s.embeddingMult.empty() ? "phonons" : "phonons_emb_" + s.embeddingMult, s, caseId);
	}

	Path PhononPanelSession::ccdAnalysisOutputPath(const PhononCaseSettings &s, const std::string &caseId) const
	{
		return CaseOutputDirectory("ccd", s, caseId);
	}
}
