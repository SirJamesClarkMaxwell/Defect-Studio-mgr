#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"

namespace DefectStudio::Demo
{
	inline const CommandID kCommandIncrement{"demo.backend.increment"};
	inline const CommandID kCommandDecrement{"demo.backend.decrement"};
	inline const CommandID kCommandReset{"demo.backend.reset"};
	inline const CommandID kCommandUndo{"demo.backend.undo"};
	inline const CommandID kCommandRedo{"demo.backend.redo"};
	inline const CommandID kCommandNotify{"demo.backend.notify"};
	inline const CommandID kCommandMissingCapability{"demo.backend.requires_missing_capability"};
	inline const CommandID kCommandSetSlider{"demo.backend.slider.set"};
	inline const CommandID kCommandSetFlag{"demo.backend.flag.set"};
	inline const CommandID kCommandAppendToken{"demo.backend.tokens.append"};
	inline const CommandID kCommandClearTokens{"demo.backend.tokens.clear"};

	class DemoValueDeltaCommand final : public ICommand
	{
	public:
		DemoValueDeltaCommand(int &value, int delta, std::string description);

		[[nodiscard]] Result<void> Execute(CommandContext &context) override;
		[[nodiscard]] Result<void> Undo(CommandContext &context) override;
		[[nodiscard]] std::string Description() const override;
		[[nodiscard]] bool IsUndoable() const noexcept override;
		[[nodiscard]] bool CanMerge(const ICommand &next) const noexcept override;
		[[nodiscard]] Result<void> Merge(std::unique_ptr<ICommand> next) override;

	private:
		int *m_Value = nullptr;
		int m_Delta = 0;
		std::string m_Description;
	};

	class DemoSetValueCommand final : public ICommand
	{
	public:
		DemoSetValueCommand(int &value, int newValue, std::string description = "Set demo value");

		[[nodiscard]] Result<void> Execute(CommandContext &context) override;
		[[nodiscard]] Result<void> Undo(CommandContext &context) override;
		[[nodiscard]] Result<void> Redo(CommandContext &context) override;
		[[nodiscard]] std::string Description() const override;
		[[nodiscard]] bool IsUndoable() const noexcept override;

	private:
		int *m_Value = nullptr;
		int m_NewValue = 0;
		int m_OldValue = 0;
		std::string m_Description;
	};

	class DemoSetBoolCommand final : public ICommand
	{
	public:
		DemoSetBoolCommand(bool &value, bool newValue, std::string description);

		[[nodiscard]] Result<void> Execute(CommandContext &context) override;
		[[nodiscard]] Result<void> Undo(CommandContext &context) override;
		[[nodiscard]] Result<void> Redo(CommandContext &context) override;
		[[nodiscard]] std::string Description() const override;
		[[nodiscard]] bool IsUndoable() const noexcept override;

	private:
		bool *m_Value = nullptr;
		bool m_NewValue = false;
		bool m_OldValue = false;
		std::string m_Description;
	};

	class DemoAppendTokenCommand final : public ICommand
	{
	public:
		DemoAppendTokenCommand(std::vector<std::string> &tokens, std::string token);

		[[nodiscard]] Result<void> Execute(CommandContext &context) override;
		[[nodiscard]] Result<void> Undo(CommandContext &context) override;
		[[nodiscard]] Result<void> Redo(CommandContext &context) override;
		[[nodiscard]] std::string Description() const override;
		[[nodiscard]] bool IsUndoable() const noexcept override;

	private:
		std::vector<std::string> *m_Tokens = nullptr;
		std::string m_Token;
	};

	class DemoClearTokensCommand final : public ICommand
	{
	public:
		explicit DemoClearTokensCommand(std::vector<std::string> &tokens);

		[[nodiscard]] Result<void> Execute(CommandContext &context) override;
		[[nodiscard]] Result<void> Undo(CommandContext &context) override;
		[[nodiscard]] Result<void> Redo(CommandContext &context) override;
		[[nodiscard]] std::string Description() const override;
		[[nodiscard]] bool IsUndoable() const noexcept override;

	private:
		std::vector<std::string> *m_Tokens = nullptr;
		std::vector<std::string> m_PreviousTokens;
	};

	class DemoCallbackCommand final : public ICommand
	{
	public:
		DemoCallbackCommand(std::string description, std::function<Result<void>(CommandContext &)> callback);

		[[nodiscard]] Result<void> Execute(CommandContext &context) override;
		[[nodiscard]] std::string Description() const override;

	private:
		std::string m_Description;
		std::function<Result<void>(CommandContext &)> m_Callback;
	};
} // namespace DefectStudio::Demo
