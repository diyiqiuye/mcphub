@echo off
setlocal
set "BIN=%~1"
if "%BIN%"=="" exit /b 2
if not exist "%BIN%\mcphubd.exe" exit /b 3
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "QTDEPLOY=C:\msys64\ucrt64\bin\windeployqt.exe"

"%QTDEPLOY%" --no-translations --no-system-d3d-compiler --no-opengl-sw --no-system-dxc-compiler "%BIN%\mcphub.exe" >nul 2>nul
"%QTDEPLOY%" --no-translations --no-system-d3d-compiler --no-opengl-sw --no-system-dxc-compiler "%BIN%\mcphubd.exe" >nul 2>nul

for /f "usebackq delims=" %%d in ("%~dp0deps.txt") do (
  if exist "C:\msys64\ucrt64\bin\%%d" copy /Y "C:\msys64\ucrt64\bin\%%d" "%BIN%\" >nul
)
exit /b 0
