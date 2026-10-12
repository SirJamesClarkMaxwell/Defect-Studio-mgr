#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	void PhononModesPanel::loadEmbeddingSummary(const Path &directory)
	{
		m_EmbeddingSummary.clear();
		std::string text, error;
		if (!TextFileIO::Load(directory.Join("summary.json"), text, error))
		{
			m_EmbeddingSummary = "Nie można odczytać metadanych embedding: " + error;
			return;
		}
		try
		{
			const auto summary = nlohmann::json::parse(text);
			if (!summary.contains("embedding") || summary.at("embedding").is_null()) return;
			const auto &embedding = summary.at("embedding");
			if (!embedding.at("mult").is_array() || embedding.at("mult").size() != 3 ||
				!embedding.at("origin_shift_A").is_array() || embedding.at("origin_shift_A").size() != 3)
				throw std::invalid_argument("Embedding mult and origin shift must have three components");
			const auto mult = embedding.at("mult").get<std::array<int, 3>>();
			const auto shift = embedding.at("origin_shift_A").get<std::array<double, 3>>();
			const double cutoff = embedding.at("dcutoff_A").get<double>();
			if (std::any_of(mult.begin(), mult.end(), [](int n) { return n < 1; }) ||
				std::any_of(shift.begin(), shift.end(), [](double n) { return !std::isfinite(n); }) ||
				!std::isfinite(cutoff) || cutoff <= 0.0)
				throw std::invalid_argument("Invalid embedding metadata");
			m_EmbeddingSummary = std::format("embedding: {}x{}x{}, przesunięcie początku ({:.4g}, {:.4g}, {:.4g}) Å, dcutoff {:.4g} Å",
				mult[0], mult[1], mult[2], shift[0], shift[1], shift[2], cutoff);
		}
		catch (const std::exception &exception)
		{
			m_EmbeddingSummary = std::string("Nie można odczytać metadanych embedding: ") + exception.what();
		}
	}
}
