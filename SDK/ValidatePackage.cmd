@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ValidatePackage.ps1" %*
exit /b %ERRORLEVEL%
