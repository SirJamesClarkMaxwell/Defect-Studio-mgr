#include "Core/dspch.hpp"

#include "Core/LayerStack.hpp"

#include "Core/Utils/Assert.hpp"



namespace DefectStudio
{
	std::string_view LayerStack::toLayerName(LayerId layerId)
	{
		switch (layerId)
		{
		case LayerId::Core:
			return "CoreLayer";
		case LayerId::IO:
			return "IOLayer";
		case LayerId::Storage:
			return "StorageLayer";
		case LayerId::ScientificRuntime:
			return "ScientificRuntimeLayer";
		case LayerId::Domain:
			return "DomainLayer";
		case LayerId::Renderer:
			return "RendererLayer";
		case LayerId::ImGui:
			return "ImGuiLayer";
		case LayerId::Editor:
			return "EditorLayer";
		case LayerId::Demo:
			return "DemoLayer";
		case LayerId::Debug:
			return "DebugLayer";
		default:
			return {};
		}
	}

	LayerStack::~LayerStack()
	{
		Clear();
	}

	void LayerStack::PushLayer(Unique<Layer> layer)
	{
		DS_ASSERT(layer != nullptr, "LayerStack::PushLayer requires a valid layer");
		Ref<Layer> layerRef(std::move(layer));
		m_Layers.insert(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), std::move(layerRef));
		++m_LayerInsertIndex;
		m_Layers[m_LayerInsertIndex - 1]->OnAttach();
	}

	void LayerStack::PushOverlay(Unique<Layer> overlay)
	{
		DS_ASSERT(overlay != nullptr, "LayerStack::PushOverlay requires a valid overlay");
		m_Layers.push_back(Ref<Layer>(std::move(overlay)));
		m_Layers.back()->OnAttach();
	}

	void LayerStack::PopLayer(Layer &layer)
	{
		auto it = std::find_if(
			m_Layers.begin(), m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex),
			[&layer](const Ref<Layer> &candidate) { return candidate.get() == &layer; });
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
			[&overlay](const Ref<Layer> &candidate) { return candidate.get() == &overlay; });
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

	WeakRef<Layer> LayerStack::FindLayer(std::string_view layerName)
	{
		if (layerName.empty())
			return {};

		auto it = std::find_if(m_Layers.begin(), m_Layers.end(), [layerName](const Ref<Layer> &layer) {
			return layer != nullptr && layer->GetName() == layerName;
		});

		return it == m_Layers.end() ? WeakRef<Layer>{} : CreateWeakRef(*it);
	}

	WeakRef<const Layer> LayerStack::FindLayer(std::string_view layerName) const
	{
		if (layerName.empty())
			return {};

		auto it = std::find_if(m_Layers.begin(), m_Layers.end(), [layerName](const Ref<Layer> &layer) {
			return layer != nullptr && layer->GetName() == layerName;
		});

		if (it == m_Layers.end())
			return {};

		Ref<const Layer> layer = *it;
		return CreateWeakRef(layer);
	}

	WeakRef<Layer> LayerStack::FindLayer(LayerId layerId)
	{
		return FindLayer(toLayerName(layerId));
	}

	WeakRef<const Layer> LayerStack::FindLayer(LayerId layerId) const
	{
		return FindLayer(toLayerName(layerId));
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
