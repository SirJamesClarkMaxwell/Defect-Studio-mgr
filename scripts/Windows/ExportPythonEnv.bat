@echo off
setlocal
set "WRAPPER_DIR=%~dp0"
for %%I in ("%WRAPPER_DIR%..") do set "SCRIPTS_DIR=%%~fI"
set "PYTHONPATH=%SCRIPTS_DIR%;%PYTHONPATH%"
python "%SCRIPTS_DIR%\python\export_python_env.py" %*
exit /b %ERRORLEVEL%
