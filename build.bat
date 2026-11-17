@echo off
rem Phosphor_DE build (devkitPro msys2). "build.bat SIM=1" makes the emulator test build.
set DEVKITPRO=/opt/devkitpro
set DEVKITARM=/opt/devkitpro/devkitARM
set MSYS2=C:\devkitPro\msys2
if not exist "%MSYS2%\usr\bin\bash.exe" (
  echo devkitPro msys2 not found at %MSYS2%
  pause
  exit /b 1
)
"%MSYS2%\usr\bin\bash.exe" -lc "cd \"$(cygpath -u \"%~dp0\")\" && make %*"
pause
