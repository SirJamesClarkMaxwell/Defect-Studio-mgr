#pragma once

#include "Core/dspch.hpp"

namespace DefectStudio
{
	template <typename T>
	using Ref = std::shared_ptr<T>;

	template <typename T>
	using WeakRef = std::weak_ptr<T>;

	template <typename T>
	using Unique = std::unique_ptr<T>;

	template <typename T, typename... Args>
	[[nodiscard]] Ref<T> CreateRef(Args &&...args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	template <typename T, typename... Args>
	[[nodiscard]] Unique<T> CreateUnique(Args &&...args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template <typename T>
	[[nodiscard]] WeakRef<T> CreateWeakRef(const Ref<T> &reference)
	{
		return WeakRef<T>(reference);
	}
} // namespace DefectStudio
