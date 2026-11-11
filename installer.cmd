@echo off
setlocal
set "ROOT=%~dp0."
set "ASCII=C:\mcphub"
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

echo [0/4] build (self-contained)
call "%ROOT%\build.cmd"
if errorlevel 1 goto :fail

if not exist "%ASCII%\CMakeLists.txt" (
  echo [fail] missing junction %ASCII%
  goto :fail
)

echo [1/4] stage payload
if exist "%ASCII%\stage" rmdir /s /q "%ASCII%\stage"
mkdir "%ASCII%\stage"
xcopy /E /I /Y "%ASCII%\build\bin" "%ASCII%\stage\bin" >nul
if errorlevel 1 goto :fail
xcopy /E /I /Y "%ASCII%\catalog" "%ASCII%\stage\catalog" >nul
if errorlevel 1 goto :fail
copy /Y "%ASCII%\bundle.json" "%ASCII%\stage\" >nul
if exist "%ASCII%\ida-tools.json" copy /Y "%ASCII%\ida-tools.json" "%ASCII%\stage\" >nul
if exist "%ASCII%\run.cmd" copy /Y "%ASCII%\run.cmd" "%ASCII%\stage\" >nul

echo [2/4] version
python "%ROOT%\scripts\version.py" "%ASCII%" > "%ASCII%\version.txt"
if errorlevel 1 goto :fail
set /p VER=<"%ASCII%\version.txt"
del /q "%ASCII%\version.txt" >nul 2>nul
if "%VER%"=="" goto :fail
echo     version=%VER%

echo [3/4] makensis
if not exist "%ASCII%\dist" mkdir "%ASCII%\dist"
del /q "%ASCII%\dist\MCPHub-Setup-*.exe" >nul 2>nul
pushd "%ASCII%\installer"
makensis /V2 /DVERSION=%VER% mcphub.nsi
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" goto :fail

echo [4/4] done
for %%f in ("%ASCII%\dist\MCPHub-Setup-*.exe") do echo     %%~nxf  %%~zf bytes
exit /b 0

:fail
echo [fail] installer build failed
exit /b 1
