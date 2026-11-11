@echo off
setlocal
set "SRC=%~dp0."
if not exist "C:\mcphub\CMakeLists.txt" (
  if exist "C:\mcphub" rmdir "C:\mcphub"
  mklink /J "C:\mcphub" "%SRC%" >nul
  if errorlevel 1 (
    echo [fail] cannot create junction C:\mcphub -^> %SRC%
    exit /b 1
  )
)
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
cmake -S "C:\mcphub" -B "C:\mcphub\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
if errorlevel 1 goto :fail
cmake --build "C:\mcphub\build" --parallel
if errorlevel 1 goto :fail
echo [deps] make build\bin self-contained
call "%SRC%\scripts\make-selfcontained.cmd" "%SRC%\build\bin"
if errorlevel 1 goto :fail
echo.
echo [ok] %SRC%\build\bin\mcphub.exe
echo [ok] %SRC%\build\bin\mcphubd.exe
exit /b 0
:fail
echo.
echo [fail] build failed
exit /b 1
