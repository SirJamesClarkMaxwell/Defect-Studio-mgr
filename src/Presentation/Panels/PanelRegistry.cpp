#include "Core/dspch.hpp"

#include "Presentation/Panels/PanelRegistry.hpp"

#include <algorithm>

namespace DefectStudio
{
	WeakRef<IPanel> PanelRegistry::Get(PanelId id)
	{
		const auto it = findEntry(id);
		return it == m_Entries.end() ? WeakRef<IPanel>{} : CreateWeakRef(it->panel);
	}

	WeakRef<const IPanel> PanelRegistry::Get(PanelId id) const
	{
		const auto it = findEntry(id);
		return it == m_Entries.end() ? WeakRef<const IPanel>{} : WeakRef<const IPanel>(it->panel);
	}

	bool PanelRegistry::Contains(PanelId id) const
	{
		return findEntry(id) != m_Entries.end();
	}

	bool PanelRegistry::Remove(PanelId id)
	{
		const auto it = findEntry(id);
		if (it == m_Entries.end())
			return false;

		m_Entries.erase(it);
		return true;
	}

	void PanelRegistry::Clear()
	{
		m_Entries.clear();
	}

	PanelRegistry::PanelId PanelRegistry::Clone(PanelId sourceId)
	{
		const auto it = findEntry(sourceId);
		if (it == m_Entries.end() || !it->panel)
			return 0;

		Entry entry;
		entry.id = m_NextId++;
		entry.panel = it->panel->Clone();
		if (!entry.panel)
			return 0;

		if (entry.panel->GetTitle() == it->panel->GetTitle())
			entry.panel->SetTitle(it->panel->GetTitle() + " copy");

		m_Entries.push_back(std::move(entry));
		return m_Entries.back().id;
	}

	std::vector<PanelRegistry::PanelId> PanelRegistry::GetIds() const
	{
		std::vector<PanelId> ids;
		ids.reserve(m_Entries.size());
		for (const auto &entry : m_Entries)
			ids.push_back(entry.id);
		return ids;
	}

	std::vector<Entry> &PanelRegistry::Entries()
	{
		return m_Entries;
	}

	const std::vector<Entry> &PanelRegistry::Entries() const
	{
		return m_Entries;
	}

	PanelRegistry::Iterator PanelRegistry::findEntry(PanelId id)
	{
		return std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) {
			return entry.id == id;
		});
	}

	PanelRegistry::ConstIterator PanelRegistry::findEntry(PanelId id) const
	{
		return std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) {
			return entry.id == id;
		});
	}
} // namespace DefectStudio
