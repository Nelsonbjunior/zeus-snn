@echo off
rem Compila e roda o banco de testes da rede neural (SNN) do Zeus no PC (MSVC do Visual Studio 2022).
setlocal
set HERE=%~dp0
set MAIN=%HERE%..\firmware\main
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%HERE%build" mkdir "%HERE%build"
cl /nologo /W3 /O2 /fp:fast /I"%MAIN%" /Fo"%HERE%build\\" /Fe"%HERE%build\snn_teste.exe" ^
   "%HERE%snn_teste.c" "%MAIN%\zeus_snn.c" || exit /b 1
"%HERE%build\snn_teste.exe"
