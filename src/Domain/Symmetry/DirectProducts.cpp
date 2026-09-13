#include "Core/dspch.hpp"

#include "Domain/Symmetry/DirectProducts.hpp"

#include <cmath>
#include <complex>

namespace DefectStudio
{
	std::vector<std::vector<std::vector<IrrepMultiplicity>>> ComputeDirectProducts(const CharacterTable &table)
	{
		const std::size_t irrepCount = table.irrepLabels.size();
		const std::size_t classCount = table.classLabels.size();
		if (irrepCount == 0 || table.groupOrder <= 0 || table.classSizes.size() != classCount ||
			table.irrepDimensions.size() != irrepCount || table.characters.size() != irrepCount)
			return {};
		for (const auto &row : table.characters)
			if (row.size() != classCount)
				return {};

		using Complex = std::complex<double>;
		std::vector<std::vector<std::vector<IrrepMultiplicity>>> products(
			irrepCount, std::vector<std::vector<IrrepMultiplicity>>(irrepCount));
		for (std::size_t left = 0; left < irrepCount; ++left)
			for (std::size_t right = 0; right < irrepCount; ++right)
				for (std::size_t target = 0; target < irrepCount; ++target)
				{
					Complex sum = 0.0;
					for (std::size_t groupClass = 0; groupClass < classCount; ++groupClass)
					{
						const auto &leftCharacter = table.characters[left][groupClass];
						const auto &rightCharacter = table.characters[right][groupClass];
						const auto &targetCharacter = table.characters[target][groupClass];
						const Complex leftValue(leftCharacter.numeric, leftCharacter.numericImaginary);
						const Complex rightValue(rightCharacter.numeric, rightCharacter.numericImaginary);
						const Complex targetValue(targetCharacter.numeric, targetCharacter.numericImaginary);
						sum += static_cast<double>(table.classSizes[groupClass]) * leftValue * rightValue * std::conj(targetValue);
					}
					const int multiplicity = static_cast<int>(std::llround(sum.real() / table.groupOrder));
					if (multiplicity > 0)
						products[left][right].push_back({table.irrepLabels[target], multiplicity, table.irrepDimensions[target]});
				}
		return products;
	}
} // namespace DefectStudio
