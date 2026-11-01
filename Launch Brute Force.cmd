@echo off
pushd "%~dp0"
python -m launcher %*
if errorlevel 1 pause
popd
