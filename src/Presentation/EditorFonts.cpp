#include "Core/dspch.hpp"

#include "Presentation/EditorFonts.hpp"

namespace DefectStudio
{
	namespace
	{
		ImFont *s_MonospaceFont = nullptr;
		ImFont *s_MathFont = nullptr;
	}

	ImFont *GetEditorMathFont()
	{
		return s_MathFont;
	}

	void SetEditorMathFont(ImFont *font)
	{
		s_MathFont = font;
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
