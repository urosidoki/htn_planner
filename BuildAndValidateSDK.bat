@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0SDK\BuildAndValidate.ps1" %*
exit /b %ERRORLEVEL%
