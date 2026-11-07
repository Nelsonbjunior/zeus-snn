@echo off
rem Compila e roda o simulador do display (MSVC do Visual Studio 2022).
rem Saida: sim\out\*.pbm e sim\out\folha.png
setlocal
set HERE=%~dp0
set MAIN=%HERE%..\firmware\main
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%HERE%out" mkdir "%HERE%out"
if not exist "%HERE%build" mkdir "%HERE%build"
cl /nologo /W3 /O2 /I"%MAIN%" /Fo"%HERE%build\\" /Fe"%HERE%build\zeus_sim.exe" ^
   "%HERE%sim_main.c" "%MAIN%\zeus_face.c" "%MAIN%\screens\ssd1306_fonts_generic.c" || exit /b 1
"%HERE%build\zeus_sim.exe" "%HERE%out" || exit /b 1
py -3.13 "%HERE%render_sheet.py" "%HERE%out" "%HERE%out\folha.png"
