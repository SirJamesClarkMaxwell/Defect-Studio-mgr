#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	using PanelId = std::uint64_t;

	struct Entry
	{
		PanelId id = 0;
		Ref<IPanel> panel;
	};

	class PanelRegistry
	{
	public:
		using PanelId = DefectStudio::PanelId;

		template <typename TPanel, typename... Args>
		PanelId Add(Args &&...args);
		
		void Clear();
		[[nodiscard]] WeakRef<IPanel> Get(PanelId id);
		[[nodiscard]] WeakRef<const IPanel> Get(PanelId id) const;
		[[nodiscard]] bool Contains(PanelId id) const;
		[[nodiscard]] bool Remove(PanelId id);
		[[nodiscard]] PanelId Clone(PanelId sourceId);
		[[nodiscard]] std::vector<PanelId> GetIds() const;
		[[nodiscard]] std::vector<Entry> &Entries();
		[[nodiscard]] const std::vector<Entry> &Entries() const;

	private:
		using Iterator = std::vector<Entry>::iterator;
		using ConstIterator = std::vector<Entry>::const_iterator;

		Iterator findEntry(PanelId id);
		ConstIterator findEntry(PanelId id) const;

	private:
		PanelId m_NextId = 1;
		std::vector<Entry> m_Entries;
	};

	template <typename TPanel, typename... Args>
	PanelRegistry::PanelId PanelRegistry::Add(Args &&...args)
	{
		Entry entry;
		entry.id = m_NextId++;
		entry.panel = CreateRef<TPanel>(std::forward<Args>(args)...);
		m_Entries.push_back(std::move(entry));
		return m_Entries.back().id;
	}
} // namespace DefectStudio
