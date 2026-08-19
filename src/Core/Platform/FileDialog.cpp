#include "Core/dspch.hpp"

#include "Core/Platform/FileDialog.hpp"

#include <nfd.h>

namespace DefectStudio::Platform
{
	namespace
	{
		StructuredError MakeFileDialogError(std::string technicalDetails, std::string code)
		{
			return StructuredError{
				ErrorCategory::IO,
				Severity::Error,
				"Folder picker failed.",
				std::move(technicalDetails),
				"Retry the folder picker, or verify a desktop session is available.",
				"Core/Platform/FileDialog",
				std::move(code)};
		}
	} // namespace

	Result<std::optional<Path>> PickFolder(const Path &defaultPath)
	{
		if (NFD_Init() != NFD_OKAY)
			return MakeFileDialogError(NFD_GetError(), "file_dialog.init_failed");

#if defined(DS_PLATFORM_WINDOWS)
		const std::wstring defaultPathNative = defaultPath.wstring();
#else
		const std::string defaultPathNative = defaultPath.String();
#endif

		nfdnchar_t *outPath = nullptr;
		const nfdresult_t dialogResult = NFD_PickFolderN(
			&outPath,
			defaultPathNative.empty() ? nullptr : defaultPathNative.c_str());

		Result<std::optional<Path>> returnValue = std::optional<Path>(std::nullopt);
		if (dialogResult == NFD_OKAY)
		{
			returnValue = std::optional<Path>(Path(std::filesystem::path(outPath)));
			NFD_FreePathN(outPath);
		}
		else if (dialogResult != NFD_CANCEL)
		{
			returnValue = MakeFileDialogError(NFD_GetError(), "file_dialog.pick_failed");
		}

		NFD_Quit();
		return returnValue;
	}
} // namespace DefectStudio::Platform
