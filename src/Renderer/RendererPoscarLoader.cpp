#include "Core/dspch.hpp"

#include "Renderer/RendererPoscarLoader.hpp"

#include <charconv>
#include <cmath>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	struct ElementRenderStyle
	{
		glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
		float atomRadius = 0.45f;
		float covalentRadius = 0.8f;
	};

	struct PoscarTokenizedLine
	{
		std::vector<std::string> tokens;
	};

	[[nodiscard]] static Result<float> ParseFloatToken(std::string_view token, std::string_view context)
	{
		const char *begin = token.data();
		const char *end = token.data() + token.size();
		float value = 0.0f;
		auto parseResult = std::from_chars(begin, end, value);
		if (parseResult.ec == std::errc() && parseResult.ptr == end)
			return value;

		return StructuredError{
			ErrorCategory::Validation,
			Severity::Error,
			"Invalid floating-point token in POSCAR.",
			"Failed to parse token '" + std::string(token) + "' in " + std::string(context),
			"Check POSCAR numeric values.",
			"renderer.poscar.invalid_float"};
	}

	[[nodiscard]] static Result<int> ParseIntToken(std::string_view token, std::string_view context)
	{
		const char *begin = token.data();
		const char *end = token.data() + token.size();
		int value = 0;
		auto parseResult = std::from_chars(begin, end, value);
		if (parseResult.ec == std::errc() && parseResult.ptr == end)
			return value;

		return StructuredError{
			ErrorCategory::Validation,
			Severity::Error,
			"Invalid integer token in POSCAR.",
			"Failed to parse token '" + std::string(token) + "' in " + std::string(context),
			"Check POSCAR counts and indexes.",
			"renderer.poscar.invalid_int"};
	}

	[[nodiscard]] static std::vector<PoscarTokenizedLine> TokenizeLines(const std::string &text)
	{
		std::vector<PoscarTokenizedLine> lines;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			std::string trimmed;
			trimmed.reserve(line.size());
			for (char character : line)
			{
				if (character == '\r')
					continue;
				trimmed.push_back(character);
			}

			if (trimmed.empty())
				continue;

			std::istringstream lineStream(trimmed);
			PoscarTokenizedLine tokenized;
			std::string token;
			while (lineStream >> token)
				tokenized.tokens.push_back(token);
			if (!tokenized.tokens.empty())
				lines.push_back(std::move(tokenized));
		}

		return lines;
	}

	[[nodiscard]] static std::unordered_map<std::string, ElementRenderStyle> CreateElementStyleTable()
	{
		return std::unordered_map<std::string, ElementRenderStyle>{
			{"H", {glm::vec3(0.95f, 0.95f, 0.95f), 0.27f, 0.31f}},
			{"B", {glm::vec3(0.95f, 0.45f, 0.30f), 0.43f, 0.85f}},
			{"N", {glm::vec3(0.25f, 0.55f, 0.95f), 0.42f, 0.71f}},
			{"Na", {glm::vec3(0.55f, 0.45f, 0.95f), 0.63f, 1.66f}},
			{"Cl", {glm::vec3(0.25f, 0.85f, 0.35f), 0.58f, 1.02f}},
			{"Cd", {glm::vec3(0.95f, 0.80f, 0.30f), 0.66f, 1.44f}},
			{"In", {glm::vec3(0.35f, 0.70f, 0.75f), 0.64f, 1.42f}},
			{"S", {glm::vec3(0.95f, 0.90f, 0.25f), 0.52f, 1.05f}}};
	}

	[[nodiscard]] static ElementRenderStyle ResolveElementStyle(
		const std::unordered_map<std::string, ElementRenderStyle> &table,
		const std::string &element)
	{
		auto found = table.find(element);
		if (found != table.end())
			return found->second;
		return ElementRenderStyle{};
	}

	[[nodiscard]] static std::vector<std::string> ExpandElementList(
		const std::vector<std::string> &elements,
		const std::vector<int> &counts)
	{
		std::vector<std::string> result;
		for (std::size_t index = 0; index < elements.size(); ++index)
		{
			const int count = counts[index];
			for (int iteration = 0; iteration < count; ++iteration)
				result.push_back(elements[index]);
		}
		return result;
	}

	[[nodiscard]] static glm::vec3 MultiplyLattice(const glm::mat3 &lattice, const glm::vec3 &fractional)
	{
		return lattice * fractional;
	}

	[[nodiscard]] static std::vector<RendererCellEdge> BuildCellEdges(const glm::mat3 &lattice)
	{
		const glm::vec3 a = lattice[0];
		const glm::vec3 b = lattice[1];
		const glm::vec3 c = lattice[2];
		const glm::vec3 p000 = glm::vec3(0.0f, 0.0f, 0.0f);
		const glm::vec3 p100 = a;
		const glm::vec3 p010 = b;
		const glm::vec3 p001 = c;
		const glm::vec3 p110 = a + b;
		const glm::vec3 p101 = a + c;
		const glm::vec3 p011 = b + c;
		const glm::vec3 p111 = a + b + c;

		return std::vector<RendererCellEdge>{
			{p000, p100}, {p000, p010}, {p000, p001},
			{p100, p110}, {p100, p101}, {p010, p110},
			{p010, p011}, {p001, p101}, {p001, p011},
			{p110, p111}, {p101, p111}, {p011, p111}};
	}

	[[nodiscard]] static std::vector<RendererBondData> BuildBonds(
		const std::vector<RendererAtomData> &atoms,
		const std::unordered_map<std::string, ElementRenderStyle> &styleTable)
	{
		std::vector<RendererBondData> bonds;
		for (std::uint32_t first = 0; first < atoms.size(); ++first)
		{
			for (std::uint32_t second = first + 1; second < atoms.size(); ++second)
			{
				const RendererAtomData &atomA = atoms[first];
				const RendererAtomData &atomB = atoms[second];
				const ElementRenderStyle styleA = ResolveElementStyle(styleTable, atomA.element);
				const ElementRenderStyle styleB = ResolveElementStyle(styleTable, atomB.element);
				const float cutoff = 1.18f * (styleA.covalentRadius + styleB.covalentRadius);
				const float cutoffSquared = cutoff * cutoff;
				const glm::vec3 delta = atomA.cartesianPosition - atomB.cartesianPosition;
				const float distanceSquared = glm::dot(delta, delta);
				if (distanceSquared > cutoffSquared || distanceSquared < 0.00001f)
					continue;

				RendererBondData bond;
				bond.firstAtomIndex = first;
				bond.secondAtomIndex = second;
				bond.radius = std::max(0.05f, 0.22f * std::min(atomA.radius, atomB.radius));
				bond.gradient.start = atomA.color;
				bond.gradient.finish = atomB.color;
				bonds.push_back(bond);
			}
		}
		return bonds;
	}

	[[nodiscard]] static Result<std::string> LoadUtf8Text(const Path &filePath)
	{
		std::ifstream stream(filePath.Native());
		if (!stream)
		{
			return StructuredError{
				ErrorCategory::IO,
				Severity::Error,
				"Renderer quick-test POSCAR could not be opened.",
				"Cannot open file: " + filePath.String(),
				"Check test POSCAR path and file permissions.",
				"renderer.poscar.open_failed"};
		}

		std::ostringstream text;
		text << stream.rdbuf();
		return text.str();
	}

	Result<RendererStructureData> LoadRendererStructureFromPoscar(const Path &filePath, std::string name)
	{
		Result<std::string> textResult = LoadUtf8Text(filePath);
		if (!textResult.HasValue())
			return textResult.Error();

		const std::vector<PoscarTokenizedLine> lines = TokenizeLines(textResult.Value());
		if (lines.size() < 8)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"POSCAR is too short for renderer loading.",
				"Expected at least 8 tokenized lines, got " + std::to_string(lines.size()),
				"Provide a valid POSCAR/CONTCAR file.",
				"renderer.poscar.too_short"};
		}

		Result<float> scaleResult = ParseFloatToken(lines[1].tokens[0], "scale line");
		if (!scaleResult.HasValue())
			return scaleResult.Error();
		const float scale = scaleResult.Value();

		glm::mat3 lattice(1.0f);
		for (int row = 0; row < 3; ++row)
		{
			const auto &tokens = lines[2 + row].tokens;
			if (tokens.size() < 3)
			{
				return StructuredError{
					ErrorCategory::Validation,
					Severity::Error,
					"POSCAR lattice row has insufficient values.",
					"Line index " + std::to_string(2 + row) + " has fewer than 3 numeric values.",
					"Fix lattice vectors in POSCAR.",
					"renderer.poscar.lattice_row_short"};
			}

			Result<float> x = ParseFloatToken(tokens[0], "lattice x");
			Result<float> y = ParseFloatToken(tokens[1], "lattice y");
			Result<float> z = ParseFloatToken(tokens[2], "lattice z");
			if (!x.HasValue())
				return x.Error();
			if (!y.HasValue())
				return y.Error();
			if (!z.HasValue())
				return z.Error();
			lattice[row] = glm::vec3(x.Value(), y.Value(), z.Value()) * scale;
		}

		std::vector<std::string> elements = lines[5].tokens;
		if (elements.empty())
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"POSCAR element list is empty.",
				"Expected at least one element symbol on line 6.",
				"Fix POSCAR header element line.",
				"renderer.poscar.no_elements"};
		}

		std::vector<int> counts;
		for (const std::string &token : lines[6].tokens)
		{
			Result<int> count = ParseIntToken(token, "element count line");
			if (!count.HasValue())
				return count.Error();
			counts.push_back(count.Value());
		}
		if (counts.size() != elements.size())
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"POSCAR element/count mismatch.",
				"Element symbols: " + std::to_string(elements.size()) + ", counts: " + std::to_string(counts.size()),
				"Fix POSCAR element/count lines.",
				"renderer.poscar.count_mismatch"};
		}

		std::size_t coordinateLineIndex = 7;
		bool selectiveDynamics = false;
		if (!lines[7].tokens.empty())
		{
			const std::string first = lines[7].tokens[0];
			if (first == "Selective" || first == "selective" || first == "SelectiveDynamics" || first == "selective_dynamics")
			{
				selectiveDynamics = true;
				coordinateLineIndex = 8;
			}
		}
		if (coordinateLineIndex >= lines.size())
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"POSCAR coordinate mode line missing.",
				"Cannot detect Direct/Cartesian marker.",
				"Fix POSCAR coordinate section.",
				"renderer.poscar.coordinate_mode_missing"};
		}

		const std::string modeToken = lines[coordinateLineIndex].tokens[0];
		const bool directCoordinates =
			modeToken == "Direct" || modeToken == "direct" || modeToken == "D" || modeToken == "d";

		std::vector<std::string> expandedElements = ExpandElementList(elements, counts);
		const std::size_t atomCount = expandedElements.size();
		const std::size_t firstAtomLine = coordinateLineIndex + 1;
		if (firstAtomLine + atomCount > lines.size())
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"POSCAR atom coordinate section is incomplete.",
				"Expected " + std::to_string(atomCount) + " coordinate lines after header.",
				"Fix POSCAR atomic coordinate list.",
				"renderer.poscar.atom_section_short"};
		}

		const auto styleTable = CreateElementStyleTable();
		std::vector<RendererAtomData> atoms;
		atoms.reserve(atomCount);
		for (std::size_t atomIndex = 0; atomIndex < atomCount; ++atomIndex)
		{
			const auto &tokens = lines[firstAtomLine + atomIndex].tokens;
			if (tokens.size() < 3)
			{
				return StructuredError{
					ErrorCategory::Validation,
					Severity::Error,
					"POSCAR atom coordinate line has insufficient values.",
					"Atom index " + std::to_string(atomIndex) + " has fewer than 3 coordinate values.",
					"Fix POSCAR atomic coordinate lines.",
					"renderer.poscar.atom_line_short"};
			}

			Result<float> x = ParseFloatToken(tokens[0], "atom x");
			Result<float> y = ParseFloatToken(tokens[1], "atom y");
			Result<float> z = ParseFloatToken(tokens[2], "atom z");
			if (!x.HasValue())
				return x.Error();
			if (!y.HasValue())
				return y.Error();
			if (!z.HasValue())
				return z.Error();

			const glm::vec3 rawValue(x.Value(), y.Value(), z.Value());
			const glm::vec3 cartesian = directCoordinates
				? MultiplyLattice(lattice, rawValue)
				: rawValue * scale;

			RendererAtomData atom;
			atom.element = expandedElements[atomIndex];
			const ElementRenderStyle style = ResolveElementStyle(styleTable, atom.element);
			atom.color = style.color;
			atom.radius = style.atomRadius;
			atom.cartesianPosition = cartesian;
			atom.visible = true;
			atoms.push_back(atom);
		}

		RendererStructureData structure;
		structure.name = std::move(name);
		structure.sourcePath = filePath;
		structure.atoms = atoms;
		structure.bonds = BuildBonds(structure.atoms, styleTable);
		structure.cellEdges = BuildCellEdges(lattice);

		if (selectiveDynamics)
		{
			DS_LOG_INFO(
				"Renderer POSCAR loader: Selective Dynamics marker found in {}, constraints are ignored for rendering.",
				filePath.String());
		}

		return structure;
	}
} // namespace DefectStudio
