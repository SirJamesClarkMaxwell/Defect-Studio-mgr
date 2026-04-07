@echo off
setlocal
set "WRAPPER_DIR=%~dp0"
for %%I in ("%WRAPPER_DIR%..") do set "SCRIPTS_DIR=%%~fI"
for %%I in ("%SCRIPTS_DIR%..") do set "REPO_ROOT=%%~fI"
set "PYTHONPATH=%REPO_ROOT%;%PYTHONPATH%"

if exist "%REPO_ROOT%\.venv\Scripts\python.exe" (
	set "PYTHON=%REPO_ROOT%\.venv\Scripts\python.exe"
) else (
	set "PYTHON=python"
)

"%PYTHON%" "%SCRIPTS_DIR%\python\test_build_matrix.py" --clean-between-runs %*
exit /b %ERRORLEVEL%
