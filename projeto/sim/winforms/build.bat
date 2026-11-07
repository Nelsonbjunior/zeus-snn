@echo off
setlocal
set HERE=%~dp0
set FRAMEWORK=C:\Windows\Microsoft.NET\Framework64\v4.0.30319
if not exist "%HERE%bin\app" mkdir "%HERE%bin\app"
"%FRAMEWORK%\csc.exe" /nologo /optimize+ /target:winexe /platform:x64 /out:"%HERE%bin\app\ZeusFaceWindow.exe" ^
  /reference:"%FRAMEWORK%\System.dll" /reference:"%FRAMEWORK%\System.Drawing.dll" ^
  /reference:"%FRAMEWORK%\System.Windows.Forms.dll" /reference:"%FRAMEWORK%\System.Net.Http.dll" ^
  "%HERE%ZeusFaceWindow.cs" || exit /b 1
echo Pronto: %HERE%bin\app\ZeusFaceWindow.exe
