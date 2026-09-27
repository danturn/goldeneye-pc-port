@echo off
REM D356-session fix: build the PC port from a plain cmd environment.
REM
REM Background: the pi agent shell (git-bash/MSYS) silently drops nearly all
REM inherited env vars at the boundary into msys-2.0.dll programs (sh/gcc) --
REM they see only ~7 MSYS-standard vars, so gcc has no TEMP/TMP and fails
REM with "Cannot create temporary file in C:\Windows\: Permission denied"
REM (silent exit 1 under ninja). cmd.exe carries the full Windows env, so we
REM build through it with the MSYS2 toolchain prepended to PATH (needed for
REM cc1's sibling DLLs) and a guaranteed-writable temp dir.
setlocal
set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
set "TEMP=C:\Users\james\AppData\Local\Temp"
set "TMP=%TEMP%"
cd /d C:\Users\james\Source\Repos\gh-fullhist
C:\msys64\mingw64\bin\cmake.exe --build build-pc -j %*
endlocal & exit /b %errorlevel%
