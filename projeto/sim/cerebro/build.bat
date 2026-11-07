@echo off
rem Simulador do Zeus com a rede neural: DLL com o codigo do firmware (MSVC) + janela WinForms.
rem Saida: sim\cerebro\bin\ZeusCerebro.exe (com zeus_cerebro.dll ao lado)
setlocal
set HERE=%~dp0
set MAIN=%HERE%..\..\firmware\main
set FRAMEWORK=C:\Windows\Microsoft.NET\Framework64\v4.0.30319
if not exist "%HERE%bin" mkdir "%HERE%bin"
if not exist "%HERE%obj" mkdir "%HERE%obj"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /W3 /O2 /fp:fast /LD /I"%MAIN%" /Fo"%HERE%obj\\" /Fe"%HERE%bin\zeus_cerebro.dll" ^
   "%HERE%zeus_cerebro_dll.c" "%MAIN%\zeus_face.c" "%MAIN%\zeus_snn.c" "%MAIN%\zeus_cerebro.c" ^
   "%MAIN%\screens\ssd1306_fonts_generic.c" || exit /b 1
"%FRAMEWORK%\csc.exe" /nologo /optimize+ /target:winexe /platform:x64 /out:"%HERE%bin\ZeusCerebro.exe" ^
  /reference:"%FRAMEWORK%\System.dll" /reference:"%FRAMEWORK%\System.Drawing.dll" ^
  /reference:"%FRAMEWORK%\System.Windows.Forms.dll" /reference:"%FRAMEWORK%\System.Net.Http.dll" ^
  "%HERE%ZeusCerebro.cs" || exit /b 1
echo Pronto: %HERE%bin\ZeusCerebro.exe
