#pragma once

#include <initializer_list>

namespace DefectStudio
{
	// The N panel's layout for object properties (task/83), the way DCC and CAD apps lay them out:
	// a fixed label column on the left, every label baseline-aligned with its widget, and the widget
	// filling the rest of the row. Sections of new object kinds build on this, so all of them line
	// up the same way.
	//
	// Give every grid of one section the same labelWidth (PropertyGridLabelWidth over all of the
	// section's labels) so the columns line up across its collapsing headers too.
	[[nodiscard]] float PropertyGridLabelWidth(std::initializer_list<const char *> labels);

	class PropertyGrid
	{
	public:
		PropertyGrid(const char *id, float labelWidth);
		~PropertyGrid();
		PropertyGrid(const PropertyGrid &) = delete;
		PropertyGrid &operator=(const PropertyGrid &) = delete;

		// False when ImGui clipped the table away - skip the rows then.
		[[nodiscard]] explicit operator bool() const { return m_Open; }

		// Starts a row: `label` in the left column (with `tooltip` on hover, if any), then leaves the
		// cursor in the right column with the next widget's width set to fill it.
		void Row(const char *label, const char *tooltip = nullptr);
		// A read-only row: label and a value printed in the field column, aligned like a widget.
		void Value(const char *label, const char *value, const char *tooltip = nullptr);

	private:
		bool m_Open = false;
	};
} // namespace DefectStudio
