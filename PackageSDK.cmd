@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0SDK\PackageSDK.ps1" %*
exit /b %ERRORLEVEL%
