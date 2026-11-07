@echo off
rem Gera o documento tecnico da rede neural (SNN) do Zeus:
rem   1. compila e roda a sonda (mesma zeus_snn.c do firmware) -> dados\*.csv e dados\valores.tex
rem   2. roda o banco de testes da SNN -> dados\snn_teste.txt
rem   3. XeLaTeX duas vezes -> Rede_Neural_SNN_Zeus.pdf
setlocal
set HERE=%~dp0
set MAIN=%HERE%..\..\firmware\main
cd /d "%HERE%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
if not exist dados mkdir dados
cl /nologo /W3 /O2 /fp:fast /I"%MAIN%" /Fo"build\\" /Fe"build\sonda.exe" sonda.c "%MAIN%\zeus_snn.c" || exit /b 1
build\sonda.exe || exit /b 1
cl /nologo /W3 /O2 /fp:fast /I"%MAIN%" /Fo"build\\" /Fe"build\snn_teste.exe" ^
   "%HERE%..\..\sim\snn_teste.c" "%MAIN%\zeus_snn.c" || exit /b 1
build\snn_teste.exe > dados\snn_teste.txt
xelatex -interaction=nonstopmode -halt-on-error Rede_Neural_SNN_Zeus.tex >nul || exit /b 1
xelatex -interaction=nonstopmode -halt-on-error Rede_Neural_SNN_Zeus.tex >nul || exit /b 1
rem edicao limpa (LinkedIn): mesmo texto, palavroes trocados/mascarados
python limpa_ponte.py || exit /b 1
xelatex -interaction=nonstopmode -halt-on-error Rede_Neural_SNN_Zeus_LinkedIn.tex >nul || exit /b 1
xelatex -interaction=nonstopmode -halt-on-error Rede_Neural_SNN_Zeus_LinkedIn.tex >nul || exit /b 1
xelatex -interaction=nonstopmode -halt-on-error Rede_Neural_SNN_Zeus_LinkedIn.tex >nul || exit /b 1
echo OK: %HERE%Rede_Neural_SNN_Zeus.pdf
echo OK: %HERE%Rede_Neural_SNN_Zeus_LinkedIn.pdf
