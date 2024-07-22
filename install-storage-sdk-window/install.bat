
@echo off

echo "Settingup sdk .."

set BASE_DIR_WITH_BACKSLASH=%~dp0
set BASE_DIR=%BASE_DIR_WITH_BACKSLASH:~0,-1%

@echo off
%BASE_DIR%/Storage_SDK_License_Config.exe "78f73f8fa50a43801321ffbe"
pause

set ARTIFACT="license.config"
if not exist "%ARTIFACT%" (
    echo ERROR: Failed to find %ARTIFACT%.
    exit /b 70
)

copy %BASE_DIR%"\lib\*" "C:\Program Files\Hanwha\Wisenet WAVE\MediaServer\"
copy ".\license.config" "C:\Program Files\Hanwha\Wisenet WAVE\MediaServer\"
copy %BASE_DIR%"\s3.config" "C:\Program Files\Hanwha\Wisenet WAVE\MediaServer\"
copy %BASE_DIR%"\s3_storage_plugin.dll" "C:\Program Files\Hanwha\Wisenet WAVE\MediaServer\plugins\"

echo "Settingup sdk ..Done"

pause
