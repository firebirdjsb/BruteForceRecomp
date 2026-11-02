@echo off
setlocal
for %%P in ("%~dp0BruteForceLauncher.exe" "%~dp0.local\launcher-build\native_launcher\Release\BruteForceLauncher.exe" "%~dp0work\native-launcher\build\native_launcher\Release\BruteForceLauncher.exe") do (
    if exist "%%~P" (
        start "" "%%~P" %*
        exit /b 0
    )
)
echo Native launcher not built. See the Desktop application section in README.md.
pause
exit /b 1
