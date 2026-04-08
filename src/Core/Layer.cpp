#include "Core/dspch.hpp"

#include "Core/Layer.hpp"

namespace DefectStudio
{
	Layer::Layer(std::string name) : m_Name(std::move(name)) {}

	Layer::~Layer() = default;

	const std::string &Layer::GetName() const
	{
		return m_Name;
	}
} // namespace DefectStudio