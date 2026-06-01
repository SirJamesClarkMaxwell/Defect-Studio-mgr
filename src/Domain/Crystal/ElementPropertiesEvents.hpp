#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio::DomainEvents::Crystal
{
	struct ElementPropertiesLoadRequested final : public BusEvent
	{
		explicit ElementPropertiesLoadRequested(Path path)
			: sourcePath(std::move(path))
		{
		}

		Path sourcePath;
	};

	struct ElementPropertiesLoaded final : public BusEvent
	{
		ElementPropertiesLoaded(
			Path path,
			std::unordered_map<std::string, ElementProperties> loadedEntries)
			: sourcePath(std::move(path)),
			  entries(std::move(loadedEntries))
		{
		}

		Path sourcePath;
		std::unordered_map<std::string, ElementProperties> entries;
	};

	struct ElementPropertiesLoadFailed final : public BusEvent
	{
		ElementPropertiesLoadFailed(Path path, std::string failureReason)
			: sourcePath(std::move(path)),
			  error(std::move(failureReason))
		{
		}

		Path sourcePath;
		std::string error;
	};
} // namespace DefectStudio::DomainEvents::Crystal
