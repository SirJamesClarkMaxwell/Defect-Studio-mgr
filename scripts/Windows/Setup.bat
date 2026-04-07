@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "WRAPPER_DIR=%~dp0"
for %%I in ("%WRAPPER_DIR%..") do set "SCRIPTS_DIR=%%~fI"

set "PYTHON_VERSION=%DEFECT_STUDIO_PYTHON_VERSION%"
if "%PYTHON_VERSION%"=="" set "PYTHON_VERSION=3.12"

echo.
echo [setup] Bootstrap start (Windows)
echo [setup] Required Python version: %PYTHON_VERSION%

where /q uv
if %ERRORLEVEL% NEQ 0 (
	echo [setup] uv not found. Installing via official installer...
	powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://astral.sh/uv/install.ps1 ^| iex"
	if %ERRORLEVEL% NEQ 0 (
		echo [error] uv installer failed.
		exit /b 1
	)

	if exist "%USERPROFILE%\.local\bin" set "PATH=%USERPROFILE%\.local\bin;%PATH%"
	if exist "%USERPROFILE%\AppData\Roaming\uv\bin" set "PATH=%USERPROFILE%\AppData\Roaming\uv\bin;%PATH%"
	if exist "%USERPROFILE%\.cargo\bin" set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
)

where /q uv
if %ERRORLEVEL% NEQ 0 (
	echo [error] uv is still not available in PATH after installation.
	exit /b 1
)

set "MANAGED_PYTHON="
for /f "usebackq delims=" %%P in (`uv python find "%PYTHON_VERSION%" 2^>NUL`) do set "MANAGED_PYTHON=%%P"

if not defined MANAGED_PYTHON (
	echo [setup] Installing Python %PYTHON_VERSION% via uv...
	uv python install "%PYTHON_VERSION%" --default
	if %ERRORLEVEL% NEQ 0 (
		echo [error] Failed to install Python %PYTHON_VERSION% with uv.
		exit /b 1
	)
	for /f "usebackq delims=" %%P in (`uv python find "%PYTHON_VERSION%" 2^>NUL`) do set "MANAGED_PYTHON=%%P"
)

if not defined MANAGED_PYTHON (
	echo [error] Unable to resolve managed Python executable for %PYTHON_VERSION%.
	exit /b 1
)

if not exist "%MANAGED_PYTHON%" (
	echo [error] Managed Python path does not exist: %MANAGED_PYTHON%
	exit /b 1
)

set "PY_CHECK_FILE=%TEMP%\defectstudio_py_check_%RANDOM%_%RANDOM%.py"
> "%PY_CHECK_FILE%" echo import pathlib
>> "%PY_CHECK_FILE%" echo import sys
>> "%PY_CHECK_FILE%" echo import sysconfig
>> "%PY_CHECK_FILE%" echo include_dir = pathlib.Path(sysconfig.get_path("include") or "")
>> "%PY_CHECK_FILE%" echo plat_include_dir = pathlib.Path(sysconfig.get_path("platinclude") or "")
>> "%PY_CHECK_FILE%" echo has_header = ^(include_dir / "Python.h"^).exists^(^) or ^(plat_include_dir / "Python.h"^).exists^(^)
>> "%PY_CHECK_FILE%" echo if not has_header:
>> "%PY_CHECK_FILE%" echo     raise SystemExit("Python.h not found in include directories")
>> "%PY_CHECK_FILE%" echo if sys.platform.startswith("win"):
>> "%PY_CHECK_FILE%" echo     libs_dir = pathlib.Path(sys.base_prefix^) / "libs"
>> "%PY_CHECK_FILE%" echo     lib_name = f"python{sys.version_info[0]}{sys.version_info[1]}.lib"
>> "%PY_CHECK_FILE%" echo     lib_path = libs_dir / lib_name
>> "%PY_CHECK_FILE%" echo     if not lib_path.exists^(^):
>> "%PY_CHECK_FILE%" echo         raise SystemExit^(f"Missing Python import library: {lib_path}"^)
>> "%PY_CHECK_FILE%" echo print^("Python native build prerequisites: OK"^)

"%MANAGED_PYTHON%" "%PY_CHECK_FILE%"
set "PY_CHECK_EXIT=%ERRORLEVEL%"
del "%PY_CHECK_FILE%" >NUL 2>&1
if %PY_CHECK_EXIT% NEQ 0 (
	echo [error] Managed Python is not ready for native extensions.
	exit /b %PY_CHECK_EXIT%
)

set "PYTHONPATH=%SCRIPTS_DIR%;%PYTHONPATH%"
"%MANAGED_PYTHON%" "%SCRIPTS_DIR%\python\setup.py" --python-version "%PYTHON_VERSION%" %*
exit /b %ERRORLEVEL%
