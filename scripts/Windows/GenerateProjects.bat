@echo off
setlocal
set "WRAPPER_DIR=%~dp0"
for %%I in ("%WRAPPER_DIR%..") do set "SCRIPTS_DIR=%%~fI"
for %%I in ("%SCRIPTS_DIR%..") do set "REPO_ROOT=%%~fI"
set "PYTHONPATH=%SCRIPTS_DIR%;%PYTHONPATH%"

if exist "%REPO_ROOT%\.venv\Scripts\python.exe" (
    set "PYTHON=%REPO_ROOT%\.venv\Scripts\python.exe"
) else (
    set "PYTHON=python"
)

"%PYTHON%" "%SCRIPTS_DIR%\python\generate_projects.py" %*
exit /b %ERRORLEVEL%
