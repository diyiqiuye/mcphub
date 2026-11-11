@echo off
setlocal
set "PATH=C:\msys64\ucrt64\bin;%~dp0build\bin;%PATH%"
if not exist "%~dp0build\bin\mcphub.exe" (
  echo [mcphub] not built yet, run build.cmd first
  pause
  exit /b 1
)
start "" "%~dp0build\bin\mcphub.exe"
