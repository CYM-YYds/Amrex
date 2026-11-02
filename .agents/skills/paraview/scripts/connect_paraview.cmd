@echo off
setlocal
rem Windows launcher: retain parse, execution-policy and runtime errors.
cd /d "%~dp0"
if not exist "%~dp0connect_paraview.ps1" (
    echo ERROR: Download connect_paraview.ps1 into this same folder first.
    pause
    exit /b 1
)
where pwsh.exe >nul 2>&1
if errorlevel 1 (
    powershell.exe -NoLogo -NoProfile -NoExit -File "%~dp0connect_paraview.ps1" %*
) else (
    pwsh.exe -NoLogo -NoProfile -NoExit -File "%~dp0connect_paraview.ps1" %*
)
set "LAUNCHER_EXIT_CODE=%ERRORLEVEL%"
echo PowerShell exited with code %LAUNCHER_EXIT_CODE%.
pause
exit /b %LAUNCHER_EXIT_CODE%
