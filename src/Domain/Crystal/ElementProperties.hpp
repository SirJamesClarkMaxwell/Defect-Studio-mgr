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
