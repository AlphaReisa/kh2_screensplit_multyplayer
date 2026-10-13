@echo off
rem build.bat          -> build\kh2coop_core.dll (release)
rem build.bat debug    -> build\kh2coop_core.dll with diagnostics (watchpoints, MapDraw trace)
rem build.bat loader   -> build\dinput8.dll (proxy loader, install once into the game folder)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist build mkdir build
if not exist build\obj mkdir build\obj
set MH=..\third_party\minhook
set MHSRC=%MH%\src\buffer.c %MH%\src\hook.c %MH%\src\trampoline.c %MH%\src\hde\hde64.c
set CORE=core.cpp common.cpp game.cpp split.cpp input.cpp party.cpp friend.cpp hud.cpp debug.cpp
set FLAGS=/nologo /O2 /MT /EHsc /std:c++17 /LD /utf-8 /W3 /I%MH%\include
if "%1"=="loader" goto loader
if "%1"=="debug" set FLAGS=%FLAGS% /DKH2COOP_DEBUG
cl %FLAGS% %CORE% %MHSRC% /Fobuild\obj\ /Febuild\kh2coop_core.dll /link user32.lib || exit /b 1
goto :eof
:loader
cl /nologo /O2 /MT /EHsc /std:c++17 /LD /utf-8 /W3 loader.cpp /Fobuild\obj\ /Febuild\dinput8.dll /link /DEF:dinput8.def user32.lib || exit /b 1
