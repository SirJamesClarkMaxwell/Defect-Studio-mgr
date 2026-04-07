# Logging and Profiling

Defect Studio uses a small logger wrapper around `spdlog` for startup and runtime diagnostics.

## Logger

The logger lives in `src/Core/Logger.hpp` and `src/Core/Logger.cpp`.

It provides:

- `LoggerOptions` for runtime configuration
- `Logger::Initialize()` and `Logger::Shutdown()` lifecycle management
- `DS_LOG_TRACE`, `DS_LOG_DEBUG`, `DS_LOG_INFO`, `DS_LOG_WARN`, `DS_LOG_ERROR`, `DS_LOG_CRITICAL` macros

The application startup path forwards CLI flags into the logger:

- `--log-level=<trace|debug|info|warn|error>`
- `--log-file`
- `--log-file=<path>`

If `--log-file` is passed without a path, the default file is `logs/DefectStudio.log`.

## Tracy Profiler

The Tracy profiler binary is vendored under:

- `Vendor/Binaries/Windows/Tracy/tracy-profiler.exe`

The vendored release archive also includes helper tools such as:

- `tracy-capture.exe`
- `tracy-csvexport.exe`
- `tracy-import-chrome.exe`
- `tracy-import-fuchsia.exe`
- `tracy-update.exe`

The current repository configuration only vendors the official Windows x64 Tracy release binary. Tracy is not part of the Linux build pipeline yet.

## Tool Discovery

The bootstrap scripts now discover Tracy through the same vendored-tool path used by Premake and mdBook.

If the profiler is missing, run the Tracy release download step again and keep the extracted files under `Vendor/Binaries/Windows/Tracy`.