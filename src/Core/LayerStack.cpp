#include "Core/dspch.hpp"

#include "Core/LayerStack.hpp"

#include "Core/Assert.hpp"

#include <algorithm>

namespace DefectStudio
{
	LayerStack::~LayerStack()
	{
		Clear();
	}

	void LayerStack::PushLayer(Unique<Layer> layer)
	{
		DS_ASSERT(layer != nullptr, "LayerStack::PushLayer requires a valid layer");
		m_Layers.insert(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), std::move(layer));
		++m_LayerInsertIndex;
		m_Layers[m_LayerInsertIndex - 1]->OnAttach();
	}

	void LayerStack::PushOverlay(Unique<Layer> overlay)
	{
		DS_ASSERT(overlay != nullptr, "LayerStack::PushOverlay requires a valid overlay");
		m_Layers.push_back(std::move(overlay));
		m_Layers.back()->OnAttach();
	}

	void LayerStack::PopLayer(Layer &layer)
	{
		auto it = std::find_if(
			m_Layers.begin(), m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex),
			[&layer](const Unique<Layer> &candidate) { return candidate.get() == &layer; });
		if (it == m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex))
			return;

		(*it)->OnDetach();
		m_Layers.erase(it);
		--m_LayerInsertIndex;
	}

	void LayerStack::PopOverlay(Layer &overlay)
	{
		auto it = std::find_if(
			m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), m_Layers.end(),
			[&overlay](const Unique<Layer> &candidate) { return candidate.get() == &overlay; });
		if (it == m_Layers.end())
			return;

		(*it)->OnDetach();
		m_Layers.erase(it);
	}

	void LayerStack::Clear()
	{
		for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
			(*it)->OnDetach();

		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}

	LayerStack::Iterator LayerStack::begin()
	{
		return m_Layers.begin();
	}

	LayerStack::Iterator LayerStack::end()
	{
		return m_Layers.end();
	}

	LayerStack::ConstIterator LayerStack::begin() const
	{
		return m_Layers.begin();
	}

	LayerStack::ConstIterator LayerStack::end() const
	{
		return m_Layers.end();
	}

	LayerStack::ReverseIterator LayerStack::rbegin()
	{
		return m_Layers.rbegin();
	}

	LayerStack::ReverseIterator LayerStack::rend()
	{
		return m_Layers.rend();
	}

	LayerStack::ConstReverseIterator LayerStack::rbegin() const
	{
		return m_Layers.rbegin();
	}

	LayerStack::ConstReverseIterator LayerStack::rend() const
	{
		return m_Layers.rend();
	}
} // namespace DefectStudio