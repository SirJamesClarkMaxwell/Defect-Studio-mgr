#pragma once

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
	};

	class ElementPropertiesTable
	{
	public:
		bool LoadFromFile(const std::string &path);

		[[nodiscard]] const ElementProperties &Get(const std::string &symbol) const;
		[[nodiscard]] float CovalentRadius(const std::string &symbol) const;

	private:
		std::unordered_map<std::string, ElementProperties> m_Data;
		ElementProperties m_Fallback;
	};
} // namespace DefectStudio
