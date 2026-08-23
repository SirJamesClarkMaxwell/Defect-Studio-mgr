#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace DefectStudio
{
	struct ElementProperties
	{
		int atomicNumber = 0;
		float mass = 0.0f;
		float covalentRadius = 0.77f;
		float vanDerWaalsRadius = 1.50f;
		// Pauling scale; 0 = not defined (noble gases and a few others have no meaningful value -
		// see pymatgen's Element.X, the source this and electronConfiguration were generated from).
		float electronegativity = 0.0f;
		// Ground-state subshell filling, space-separated "<n><subshell><count>" tokens in fill order
		// (e.g. "1s2 2s2 2p2" for carbon) - kept as the raw string rather than parsed here since the
		// only consumer is ElementCatalogPanel's electron-configuration table; see
		// ElementCatalogPanel.cpp's ParseElectronConfiguration.
		std::string electronConfiguration;
	};

	class ElementPropertiesTable
	{
	public:
		void ReplaceData(std::unordered_map<std::string, ElementProperties> data);
		void Clear();

		[[nodiscard]] const ElementProperties &Get(const std::string &symbol) const;
		[[nodiscard]] float CovalentRadius(const std::string &symbol) const;
		[[nodiscard]] bool Empty() const;
		[[nodiscard]] std::size_t Size() const;

	private:
		std::unordered_map<std::string, ElementProperties> m_Data;
		ElementProperties m_Fallback;
	};
} // namespace DefectStudio
