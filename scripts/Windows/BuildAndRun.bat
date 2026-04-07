@echo off
setlocal
set "WRAPPER_DIR=%~dp0"
for %%I in ("%WRAPPER_DIR%..") do set "SCRIPTS_DIR=%%~fI"
set "PYTHONPATH=%SCRIPTS_DIR%;%PYTHONPATH%"
python "%SCRIPTS_DIR%\python\build_and_run.py" %*
exit /b %ERRORLEVEL%
