@echo off
setlocal
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
    echo .NET Framework C# compiler not found.
    exit /b 1
)
pushd "%~dp0"
if not exist build mkdir build
"%CSC%" /nologo /target:winexe /platform:anycpu /optimize+ /warn:4 /warnaserror+ ^
    /out:build\DS2008RandomizerConfig-v1.0.9.exe ^
    /win32icon:configurator\assets\DS2008RandomizerIcon.ico ^
    /resource:configurator\assets\DS2008RandomizerBanner.png,DS2008Banner ^
    /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll ^
    configurator\App.cs configurator\Catalog.cs configurator\IniDocument.cs configurator\Theme.cs ^
    ..\DeadSpace2Randomizer\tools\RandomizerConfig\Theme.cs
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
