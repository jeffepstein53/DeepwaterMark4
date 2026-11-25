@echo off
rem Starts Deepwater Mark 4's MCP bridge (`Deepwater.exe --mcp-stdio`), wherever Mark 4 is installed.
rem Nothing but the bridge's own JSON-RPC lines may reach standard output: every message of this script goes to standard error.
setlocal
set "DW="
rem 1. the folder the installer wrote in "installed apps"
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Deepwater Mark 4" /v InstallLocation 2^>nul') do set "DW=%%B"
if defined DW if exist "%DW%\Deepwater.exe" goto run
rem 2. the installer's default folder, then a copy unpacked in the user's folder
set "DW=%LOCALAPPDATA%\Programs\Deepwater Mark 4"
if exist "%DW%\Deepwater.exe" goto run
set "DW=%USERPROFILE%\Deepwater-Mark4"
if exist "%DW%\Deepwater.exe" goto run
echo [deepwater plugin] Deepwater Mark 4 is not installed on this computer: install it from https://github.com/jeffepstein53/DeepwaterMark4/releases 1>&2
exit /b 1
:run
"%DW%\Deepwater.exe" --mcp-stdio
exit /b %ERRORLEVEL%
