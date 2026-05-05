#pragma once

#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Input/KeyBinding.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::AppEvents::Keymap
{
	struct BindingsSaveRequested final : public BusEvent
	{
		BindingsSaveRequested(std::vector<KeyBinding> bindingsList, Path path)
			: bindings(std::move(bindingsList)), targetPath(std::move(path))
		{
		}

		std::vector<KeyBinding> bindings;
		Path targetPath;
	};

	struct BindingsSaved final : public BusEvent
	{
		explicit BindingsSaved(Path path)
			: savedPath(std::move(path))
		{
		}

		Path savedPath;
	};

	struct BindingsSaveFailed final : public BusEvent
	{
		explicit BindingsSaveFailed(std::string failure)
			: error(std::move(failure))
		{
		}

		std::string error;
	};

	struct BindingsLoadRequested final : public BusEvent
	{
		explicit BindingsLoadRequested(Path path)
			: sourcePath(std::move(path))
		{
		}

		Path sourcePath;
	};

	struct BindingsLoaded final : public BusEvent
	{
		explicit BindingsLoaded(std::vector<KeyBinding> bindingsList)
			: bindings(std::move(bindingsList))
		{
		}

		std::vector<KeyBinding> bindings;
	};

	struct BindingsLoadFailed final : public BusEvent
	{
		explicit BindingsLoadFailed(std::string failure)
			: error(std::move(failure))
		{
		}

		std::string error;
	};
} // namespace DefectStudio::AppEvents::Keymap
