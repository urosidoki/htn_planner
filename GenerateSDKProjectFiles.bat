@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1
ThirdParty\premake\bin\premake5.exe --sdk %* vs2022
set "HTN_SDK_RESULT=%ERRORLEVEL%"
popd
exit /b %HTN_SDK_RESULT%
