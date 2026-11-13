@echo off
setlocal DisableDelayedExpansion
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0Uninstall-Reloaded.ps1"
set "sparking_result=%errorlevel%"
pause
exit /b %sparking_result%
