@echo off
setlocal
set "ROOT=%~dp0."
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=%LOCALAPPDATA%\MCPHub"
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "QTDEPLOY=C:\msys64\ucrt64\bin\windeployqt.exe"

echo [1/6] build
call "%ROOT%\build.cmd"
if errorlevel 1 goto :fail

echo [2/6] layout "%TARGET%"
if not exist "%TARGET%\bin" mkdir "%TARGET%\bin"
if not exist "%TARGET%\catalog" mkdir "%TARGET%\catalog"
if not exist "%TARGET%\tools" mkdir "%TARGET%\tools"
copy /Y "%ROOT%\build\bin\mcphub.exe" "%TARGET%\bin\" >nul
if errorlevel 1 goto :fail
copy /Y "%ROOT%\build\bin\mcphubd.exe" "%TARGET%\bin\" >nul
if errorlevel 1 goto :fail
copy /Y "%ROOT%\bundle.json" "%TARGET%\" >nul
if exist "%ROOT%\ida-tools.json" copy /Y "%ROOT%\ida-tools.json" "%TARGET%\" >nul
xcopy /E /I /Y "%ROOT%\catalog" "%TARGET%\catalog" >nul

echo [3/6] runtime (copy self-contained build\bin)
xcopy /E /I /Y "%ROOT%\build\bin\*.*" "%TARGET%\bin\" >nul

echo [4/6] launcher
> "%TARGET%\run.cmd" echo @echo off
>>"%TARGET%\run.cmd" echo start "" "%%~dp0bin\mcphub.exe"

echo [5/6] verify gateway with clean PATH
set "PATH=%SystemRoot%\system32;%SystemRoot%"
"%TARGET%\bin\mcphubd.exe" --doctor
if not "%ERRORLEVEL%"=="0" (
  echo [fail] doctor exit=%ERRORLEVEL%
  goto :fail
)

echo [6/6] verify GUI with clean PATH
del /q "%TARGET%\smoke.png" 2>nul
start "" "%TARGET%\bin\mcphub.exe" --shot "%TARGET%\smoke.png"
ping -n 9 127.0.0.1 >nul
if not exist "%TARGET%\smoke.png" (
  taskkill /F /IM mcphub.exe >nul 2>nul
  echo [fail] GUI failed to start with clean PATH
  goto :fail
)
taskkill /F /IM mcphub.exe >nul 2>nul
del /q "%TARGET%\smoke.png" 2>nul

echo.
echo done: %TARGET%
echo run:  %TARGET%\run.cmd
exit /b 0

:fail
echo [fail] deploy failed
exit /b 1
