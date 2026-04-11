
@echo off

echo "Settingup sdk .."

set BASE_DIR_WITH_BACKSLASH=%~dp0
set BASE_DIR=%BASE_DIR_WITH_BACKSLASH:~0,-1%

@echo off
%BASE_DIR%/Storage_SDK_License_Config.exe 

set ARTIFACT="env.config"
if not exist "%ARTIFACT%" (
    echo ERROR: Failed to find %ARTIFACT%.
    exit /b 70
)

echo "Select MediaServer installation folder!!"

@echo off
setlocal

:: Use PowerShell to open a folder browser dialog
set "psCommand="(new-object -COM 'Shell.Application')^
.BrowseForFolder(0,'Please choose a folder.',0,0).self.path""
for /f "usebackq delims=" %%I in (`powershell %psCommand%`) do set "folder=%%I"
echo You selected: %folder%

:: Check if the user selected a folder
if "%folder%"=="" (
    echo ERROR:"No folder was selected."
) else (

    if exist "%folder%\" (

        copy %BASE_DIR%"\lib\*" "%folder%\"
        copy ".\env.config" "%folder%\"
        copy %BASE_DIR%"\s3.config" "%folder%\"
        copy %BASE_DIR%"\s3_storage_plugin.dll" "%folder%\plugins\"

        echo "Settingup sdk ..Done"

    ) else (
        echo ERROR:"folder not exist"
    )
)

endlocal
pause
