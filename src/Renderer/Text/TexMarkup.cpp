#include "Core/dspch.hpp"
#include "Renderer/Text/TexMarkup.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		char32_t Decode(std::string_view text, std::size_t &pos)
		{
			const unsigned char first = text[pos++];
			if (first < 0x80) return first;
			const int count = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC2 ? 1 : 0;
			if (!count || first > 0xF4) return U'\uFFFD';
			char32_t value = first & ((1 << (6 - count)) - 1);
			for (int i = 0; i < count; ++i)
			{
				if (pos == text.size() || (static_cast<unsigned char>(text[pos]) & 0xC0) != 0x80)
					return U'\uFFFD';
				value = (value << 6) | (static_cast<unsigned char>(text[pos++]) & 0x3F);
			}
			if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)
				|| value < (count == 1 ? 0x80u : count == 2 ? 0x800u : 0x10000u))
				return U'\uFFFD';
			return value;
		}

		char32_t Symbol(std::string_view name)
		{
			static constexpr std::pair<std::string_view, char32_t> symbols[] = {
				{"alpha", U'\u03B1'}, {"beta", U'\u03B2'}, {"gamma", U'\u03B3'}, {"delta", U'\u03B4'},
				{"epsilon", U'\u03B5'}, {"zeta", U'\u03B6'}, {"eta", U'\u03B7'}, {"theta", U'\u03B8'},
				{"iota", U'\u03B9'}, {"kappa", U'\u03BA'}, {"lambda", U'\u03BB'}, {"mu", U'\u03BC'},
				{"nu", U'\u03BD'}, {"xi", U'\u03BE'}, {"omicron", U'\u03BF'}, {"pi", U'\u03C0'},
				{"rho", U'\u03C1'}, {"sigma", U'\u03C3'}, {"tau", U'\u03C4'}, {"upsilon", U'\u03C5'},
				{"phi", U'\u03C6'}, {"chi", U'\u03C7'}, {"psi", U'\u03C8'}, {"omega", U'\u03C9'},
				{"Alpha", U'A'}, {"Beta", U'B'}, {"Gamma", U'\u0393'}, {"Delta", U'\u0394'},
				{"Epsilon", U'E'}, {"Zeta", U'Z'}, {"Eta", U'H'}, {"Theta", U'\u0398'},
				{"Iota", U'I'}, {"Kappa", U'K'}, {"Lambda", U'\u039B'}, {"Mu", U'M'},
				{"Nu", U'N'}, {"Xi", U'\u039E'}, {"Omicron", U'O'}, {"Pi", U'\u03A0'},
				{"Rho", U'P'}, {"Sigma", U'\u03A3'}, {"Tau", U'T'}, {"Upsilon", U'\u03A5'},
				{"Phi", U'\u03A6'}, {"Chi", U'X'}, {"Psi", U'\u03A8'}, {"Omega", U'\u03A9'},
				{"vartheta", U'\u03D1'}, {"varphi", U'\u03D5'}, {"varepsilon", U'\u03F5'},
				{"pm", U'\u00B1'}, {"mp", U'\u2213'}, {"cdot", U'\u00B7'}, {"times", U'\u00D7'},
				{"to", U'\u2192'}, {"rightarrow", U'\u2192'}, {"leftarrow", U'\u2190'},
				{"leftrightarrow", U'\u2194'}, {"uparrow", U'\u2191'}, {"downarrow", U'\u2193'},
				{"circ", U'\u00B0'}, {"deg", U'\u00B0'}, {"AA", U'\u00C5'}, {"infty", U'\u221E'},
				{"approx", U'\u2248'}, {"neq", U'\u2260'}, {"leq", U'\u2264'}, {"geq", U'\u2265'},
				{"sim", U'\u223C'}, {"prime", U'\u2032'}, {"hbar", U'\u210F'},
				{"langle", U'\u27E8'}, {"rangle", U'\u27E9'}
			};
			for (const auto &[command, codepoint] : symbols)
				if (command == name) return codepoint;
			return 0;
		}
	}

	std::vector<TexGlyph> ParseTexMarkup(std::string_view text)
	{
		struct Frame { float scale; float shift; char previousScript = 0; bool stackNext = false; };
		std::vector<Frame> frames{{1.0f, 0.0f}};
		std::vector<TexGlyph> result;
		std::size_t pos = 0;
		const auto emit = [&](char32_t cp, Frame &frame) {
			bool stacked = frame.stackNext;
			frame.stackNext = false;
			for (Frame &parent : frames)
			{
				stacked = stacked || parent.stackNext;
				parent.stackNext = false;
			}
			result.push_back({cp == U'-' && frame.scale < 1.0f ? U'\u2212' : cp, frame.scale, frame.shift, stacked});
		};
		const auto token = [&](Frame &frame) {
			if (text[pos] != '\\') { emit(Decode(text, pos), frame); return; }
			const std::size_t start = pos++;
			if (pos == text.size()) { emit(U'\\', frame); return; }
			const char c = text[pos];
			if (c == ',' || c == ' ' || c == '_' || c == '^' || c == '{' || c == '}' || c == '\\')
			{
				++pos;
				emit(c == ',' ? U'\u2009' : static_cast<char32_t>(c), frame);
				return;
			}
			while (pos < text.size() && ((text[pos] >= 'a' && text[pos] <= 'z') || (text[pos] >= 'A' && text[pos] <= 'Z'))) ++pos;
			if (const char32_t cp = Symbol(text.substr(start + 1, pos - start - 1))) emit(cp, frame);
			else for (std::size_t i = start; i < pos;) emit(Decode(text, i), frame);
		};
		// Explicit frames keep arbitrarily nested user text off the call stack.
		while (pos < text.size())
		{
			Frame &frame = frames.back();
			const char c = text[pos];
			if (c == '}' && frames.size() > 1) { ++pos; frames.pop_back(); continue; }
			if (c == '{') { ++pos; frame.previousScript = 0; frames.push_back({frame.scale, frame.shift}); continue; }
			if ((c == '_' || c == '^') && pos + 1 < text.size() && text[pos + 1] != '}')
			{
				++pos;
				const bool stacked = frame.previousScript && frame.previousScript != c;
				frame.previousScript = c;
				Frame script{frame.scale * kTexScriptScale, frame.shift + frame.scale * (c == '_' ? kTexSubscriptShift : kTexSuperscriptShift), 0, stacked};
				if (text[pos] == '{')
				{
					++pos;
					frames.push_back(script);
				}
				else token(script);
				continue;
			}
			frame.previousScript = 0;
			token(frame);
		}
		return result;
	}

	TexLayout LayoutTexGlyphs(const std::vector<TexGlyph> &glyphs, const std::function<float(char32_t)> &advance)
	{
		struct Run { float scale; float shift; float start; float end; };
		std::vector<Run> runs;
		TexLayout layout;
		float pen = 0.0f;
		for (const TexGlyph &glyph : glyphs)
		{
			while (!runs.empty() && glyph.scale > runs.back().scale)
			{
				pen = std::max(pen, runs.back().end);
				runs.pop_back();
			}
			if (glyph.stacksOnPreviousScript && !runs.empty()) pen = runs.back().start;
			if (runs.empty() || glyph.scale != runs.back().scale || glyph.baselineShift != runs.back().shift || glyph.stacksOnPreviousScript)
			{
				if (!runs.empty() && glyph.scale == runs.back().scale && !glyph.stacksOnPreviousScript)
					pen = std::max(pen, runs.back().end);
				const float end = !runs.empty() && glyph.stacksOnPreviousScript ? runs.back().end : pen;
				if (!runs.empty() && glyph.scale == runs.back().scale) runs.pop_back();
				runs.push_back({glyph.scale, glyph.baselineShift, pen, end});
			}
			layout.glyphs.push_back({glyph.codepoint, pen, glyph.baselineShift, glyph.scale});
			pen += advance(glyph.codepoint) * glyph.scale;
			for (Run &run : runs) run.end = std::max(run.end, pen);
			layout.width = std::max(layout.width, pen);
		}
		return layout;
	}
}
