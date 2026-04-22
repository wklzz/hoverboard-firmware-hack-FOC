@echo off
pushd "%~dp0"
powershell -ExecutionPolicy Bypass -File build.ps1
popd
pause
