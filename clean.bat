@echo off
set MSYS2=C:\devkitPro\msys2
"%MSYS2%\usr\bin\bash.exe" -lc "cd \"$(cygpath -u \"%~dp0\")\" && make clean"
