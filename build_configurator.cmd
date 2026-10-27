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
    /out:build\DeadSpace2008RandomizerConfig.exe ^
    /win32icon:configurator\assets\DS2008RandomizerIcon.ico ^
    /resource:configurator\assets\DS2008RandomizerBanner.png,DS2008Banner ^
    /resource:configurator\SpoilerData.txt,DS2008Spoiler ^
    /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll ^
    /reference:System.IO.Compression.dll ^
    configurator\App.cs configurator\Catalog.cs configurator\IniDocument.cs configurator\Theme.cs configurator\SeedTools.cs ^
    configurator\shared\Theme.cs
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
