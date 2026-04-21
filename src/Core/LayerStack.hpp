#pragma once

#include <string_view>

#include "Core/Utils/Memory.hpp"
#include "Core/Layer.hpp"

namespace DefectStudio
{
	enum class LayerId
	{
		Core,
		IO,
		Storage,
		ScientificRuntime,
		Domain,
		ImGui,
		Editor,
		Demo,
		Debug,
		Unknown
	};
	class LayerStack
	{
	public:

		using Layers = std::vector<Ref<Layer>>;

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
		[[nodiscard]] WeakRef<Layer> FindLayer(std::string_view layerName);
		[[nodiscard]] WeakRef<const Layer> FindLayer(std::string_view layerName) const;
		[[nodiscard]] WeakRef<Layer> FindLayer(LayerId layerId);
		[[nodiscard]] WeakRef<const Layer> FindLayer(LayerId layerId) const;

		template <typename TLayer>
		[[nodiscard]] WeakRef<TLayer> FindLayerAs(LayerId layerId);

		template <typename TLayer>
		[[nodiscard]] WeakRef<const TLayer> FindLayerAs(LayerId layerId) const;

		Iterator begin();
		Iterator end();
		ConstIterator begin() const;
		ConstIterator end() const;
		ReverseIterator rbegin();
		ReverseIterator rend();
		ConstReverseIterator rbegin() const;
		ConstReverseIterator rend() const;

	private:
		[[nodiscard]] static std::string_view toLayerName(LayerId layerId);

		Layers m_Layers;
		std::size_t m_LayerInsertIndex = 0;
	};

	template <typename TLayer>
	WeakRef<TLayer> LayerStack::FindLayerAs(LayerId layerId)
	{
		Ref<Layer> layer = FindLayer(layerId).lock();
		if (layer == nullptr)
			return {};

		Ref<TLayer> typedLayer = std::dynamic_pointer_cast<TLayer>(layer);
		return typedLayer == nullptr ? WeakRef<TLayer>{} : CreateWeakRef(typedLayer);
	}

	template <typename TLayer>
	WeakRef<const TLayer> LayerStack::FindLayerAs(LayerId layerId) const
	{
		Ref<const Layer> layer = FindLayer(layerId).lock();
		if (layer == nullptr)
			return {};

		Ref<const TLayer> typedLayer = std::dynamic_pointer_cast<const TLayer>(layer);
		return typedLayer == nullptr ? WeakRef<const TLayer>{} : CreateWeakRef(typedLayer);
	}
} // namespace DefectStudio
