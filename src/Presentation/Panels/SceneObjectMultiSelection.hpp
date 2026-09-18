#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <vector>

#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	enum class SceneOutlinerSelectionModifier
	{
		Replace,
		Toggle,
		Range
	};

	template <typename Id>
	[[nodiscard]] std::vector<Id> ApplySceneOutlinerSelection(
		const std::vector<Id> &currentSelection,
		const std::vector<Id> &orderedRows,
		const Id clicked,
		const std::optional<Id> anchor,
		const SceneOutlinerSelectionModifier modifier)
	{
		if (modifier == SceneOutlinerSelectionModifier::Toggle)
		{
			std::vector<Id> result = currentSelection;
			const auto existing = std::find(result.begin(), result.end(), clicked);
			if (existing == result.end())
				result.push_back(clicked);
			else
				result.erase(existing);
			return result;
		}

		if (modifier != SceneOutlinerSelectionModifier::Range || !anchor.has_value())
			return {clicked};

		auto first = std::find(orderedRows.begin(), orderedRows.end(), *anchor);
		auto last = std::find(orderedRows.begin(), orderedRows.end(), clicked);
		if (first == orderedRows.end() || last == orderedRows.end())
			return {clicked};
		if (last < first)
			std::swap(first, last);
		return std::vector<Id>(first, std::next(last));
	}

	template <typename Object>
	[[nodiscard]] std::size_t FirstSelectedSceneObjectIndex(
		const std::vector<Object> &objects, const std::vector<SceneObjectId> &selection)
	{
		for (const SceneObjectId id : selection)
		{
			const std::size_t index = AnnotationIndex(objects, id);
			if (index < objects.size())
				return index;
		}
		return objects.size();
	}

	template <typename Object, typename Value>
	void ApplySelectedSceneObjectField(
		std::vector<Object> &objects, const std::vector<SceneObjectId> &selection,
		const std::size_t representativeIndex, Value Object::*field)
	{
		if (representativeIndex >= objects.size())
			return;
		const Value value = objects[representativeIndex].*field;
		for (const SceneObjectId id : selection)
		{
			const std::size_t index = AnnotationIndex(objects, id);
			if (index < objects.size())
				objects[index].*field = value;
		}
	}
} // namespace DefectStudio
