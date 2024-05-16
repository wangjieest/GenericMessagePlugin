@echo off

pushd %~dp0
for /f "delims=" %%a in ('dir . /B *.uplugin') do set PluginName=%%~na

rem Win64+Android+Linux+Mac+iOS
set "TargetPlatforms=Win64+Android"

rem RunUAT.bat BuildPlugin -Plugin="%~dp0%PluginName%.uplugin" -Package="%OutputDir%" -TargetPlatforms=Win64+Linux -compile

setlocal EnableDelayedExpansion
set "UEThirdVersion="
set "UESecondVersion="
set "UELastestVersion="
echo Finding UnrealEngines...
for /f "tokens=*" %%a  in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine"') do (
    set "UELastestVersion=%%~nxa"

    for /f  "tokens=1,2 delims=:" %%x  in ('reg query "%%a" /v "InstalledDirectory" ^| find /i "InstalledDirectory"') do (
        set "Tmp1=%%x"
        set "Tmp2=%%y"
        set "UELastestPath=!Tmp1:~-1!:!Tmp2!"
    )
    if exist "!UELastestPath!\Engine\Build\BatchFiles\RunUAT.bat" (
        echo !UELastestVersion!
        echo>Build_!UELastestVersion!.bat @echo off
        echo>>Build_!UELastestVersion!.bat ^:Rebuild
        echo>>Build_!UELastestVersion!.bat @pushd %%~dp0
        echo>>Build_!UELastestVersion!.bat @pushd ..
        echo>>Build_!UELastestVersion!.bat @call "!UELastestPath!\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin=%%~dp0%PluginName%.uplugin -Package="%~dp0..\%PluginName%!UELastestVersion!" -TargetPlatforms=%TargetPlatforms% -compile
        echo>>Build_!UELastestVersion!.bat if ERRORLEVEL 1 pause ^& goto ^Retry
        echo>>Build_!UELastestVersion!.bat pause
        echo>>Build_!UELastestVersion!.bat rmdir /S /Q "%PluginName%!UELastestVersion!"
        echo>>Build_!UELastestVersion!.bat goto ^:eof
        echo>>Build_!UELastestVersion!.bat ^:Retry
        echo>>Build_!UELastestVersion!.bat rmdir /S /Q "%PluginName%!UELastestVersion!"
        echo>>Build_!UELastestVersion!.bat goto ^:Rebuild
    )
)

if "%UELastestVersion%" equ "" (
echo No Version Found! & pause
exit /b 1
)

