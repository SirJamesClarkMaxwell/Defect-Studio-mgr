#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Core/Commands/CommandContext.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Types/CoreIds.hpp"

namespace DefectStudio
{
	enum class CommandFlags : std::uint32_t
	{
		None = 0,
		Undoable = 1u << 0u,
		AsyncCapable = 1u << 1u,
		HiddenFromPalette = 1u << 2u,
		NonUndoableBarrier = 1u << 3u
	};

	[[nodiscard]] constexpr CommandFlags operator|(CommandFlags lhs, CommandFlags rhs) noexcept
	{
		return static_cast<CommandFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
	}

	[[nodiscard]] constexpr CommandFlags operator&(CommandFlags lhs, CommandFlags rhs) noexcept
	{
		return static_cast<CommandFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
	}

	[[nodiscard]] constexpr bool HasFlag(CommandFlags flags, CommandFlags flag) noexcept
	{
		return (flags & flag) != CommandFlags::None;
	}

	struct CommandMeta
	{
		CommandID id;
		std::string name;
		std::string category;
		std::string description;
		std::vector<CapabilityID> requiredCapabilities;
		CommandFlags flags = CommandFlags::None;
	};

	struct CommandOutcome
	{
		CommandID id;
		std::string description;
		bool undoable = false;
		bool pushedToUndoStack = false;
	};

	class ICommand
	{
	public:
		virtual ~ICommand() = default;

		[[nodiscard]] virtual Result<void> Execute(CommandContext &context) = 0;
		[[nodiscard]] virtual Result<void> Undo(CommandContext &context);
		[[nodiscard]] virtual Result<void> Redo(CommandContext &context);
		[[nodiscard]] virtual std::string Description() const = 0;
		[[nodiscard]] virtual bool IsUndoable() const noexcept;
		[[nodiscard]] virtual bool CanMerge(const ICommand &next) const noexcept;
		[[nodiscard]] virtual Result<void> Merge(std::unique_ptr<ICommand> next);
	};
} // namespace DefectStudio
