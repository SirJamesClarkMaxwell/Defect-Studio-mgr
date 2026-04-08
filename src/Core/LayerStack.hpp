#pragma once

#include "Core/Memory.hpp"
#include "Core/Layer.hpp"

namespace DefectStudio
{
	class LayerStack
	{
	public:
		using Layers = std::vector<Unique<Layer>>;

		using Iterator = Layers::iterator;
		using ConstIterator = Layers::const_iterator;
		using ReverseIterator = Layers::reverse_iterator;
		using ConstReverseIterator = Layers::const_reverse_iterator;

		LayerStack() = default;
		~LayerStack();

		void PushLayer(Unique<Layer> layer);
		void PushOverlay(Unique<Layer> overlay);
		void PopLayer(Layer &layer);
		void PopOverlay(Layer &overlay);
		void Clear();

		Iterator begin();
		Iterator end();
		ConstIterator begin() const;
		ConstIterator end() const;
		ReverseIterator rbegin();
		ReverseIterator rend();
		ConstReverseIterator rbegin() const;
		ConstReverseIterator rend() const;

	private:
		Layers m_Layers;
		std::size_t m_LayerInsertIndex = 0;
	};
} // namespace DefectStudio
