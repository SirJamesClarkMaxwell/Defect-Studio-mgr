#pragma once

#include <string>
#include <utility>

#include "Core/Utils/Memory.hpp"
#include "Presentation/MenuBarModel.hpp"

namespace DefectStudio
{
	class IPanel
	{
	public:
		explicit IPanel(std::string title, bool visibleByDefault = true)
			: m_Title(std::move(title)), m_Visible(visibleByDefault)
		{
		}

		virtual ~IPanel() = default;

		[[nodiscard]] const std::string &GetTitle() const
		{
			return m_Title;
		}

		void SetTitle(std::string title)
		{
			m_Title = std::move(title);
		}

		[[nodiscard]] bool IsVisible() const
		{
			return m_Visible;
		}

		void SetVisible(bool visible)
		{
			m_Visible = visible;
		}

		// Which drawer of the Widok menu this panel is listed under. Defaults to Other rather than
		// being pure, so a newly added panel is reachable from the menu the moment it is registered
		// - it just lands in the catch-all group until someone files it.
		[[nodiscard]] virtual PanelCategory GetCategory() const
		{
			return PanelCategory::Other;
		}

		[[nodiscard]] virtual Ref<IPanel> Clone() const = 0;

		virtual void Render() = 0;

	protected:

	private:
		std::string m_Title;
		bool m_Visible = true;
	};
} // namespace DefectStudio
