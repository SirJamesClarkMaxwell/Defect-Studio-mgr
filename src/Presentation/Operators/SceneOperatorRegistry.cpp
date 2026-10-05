#include "Core/dspch.hpp"

#include "Presentation/Operators/SceneOperatorRegistry.hpp"

#include <algorithm>
#include <utility>
#include <string>
#include <utility>

namespace DefectStudio
{
	Result<void> SceneOperatorRegistry::Register(SceneOperator op)
	{
		// Read the id before the move: the order of the two arguments is unspecified, and a moved-from
		// std::string leaves an empty key behind.
		std::string id = op.id;
		const auto [it, inserted] = m_Operators.emplace(std::move(id), std::move(op));
		if (!inserted)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"Rejestracja operacji sceny nie powiodła się.",
				"Operator sceny o identyfikatorze '" + it->first + "' jest już zarejestrowany.",
				"Użyj unikalnego identyfikatora operatora.",
				"SceneOperatorRegistry",
				"scene_operator.register.duplicate"};
		}

		return {};
	}

	const SceneOperator *SceneOperatorRegistry::Find(const std::string &id) const
	{
		const auto it = m_Operators.find(id);
		return it == m_Operators.end() ? nullptr : &it->second;
	}

	std::vector<std::string> SceneOperatorRegistry::ListIds() const
	{
		std::vector<std::string> ids;
		ids.reserve(m_Operators.size());
		for (const auto &entry : m_Operators)
			ids.push_back(entry.first);
		std::sort(ids.begin(), ids.end());
		return ids;
	}
}
