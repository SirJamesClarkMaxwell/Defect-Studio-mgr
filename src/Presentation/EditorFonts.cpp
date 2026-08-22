#include "Core/dspch.hpp"

#include "Presentation/EditorFonts.hpp"

namespace DefectStudio
{
	namespace
	{
		ImFont *s_MonospaceFont = nullptr;
	}

	ImFont *GetEditorMonospaceFont()
	{
		return s_MonospaceFont;
	}

	void SetEditorMonospaceFont(ImFont *font)
	{
		s_MonospaceFont = font;
	}
} // namespace DefectStudio
