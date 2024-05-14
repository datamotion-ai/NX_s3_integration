
@echo off

echo "Settingup sdk .."

copy ".\lib\*" "C:\Program Files\Network Optix\Nx Witness\MediaServer\"
copy ".\license.config" "C:\Program Files\Network Optix\Nx Witness\MediaServer\"
copy ".\s3.config" "C:\Program Files\Network Optix\Nx Witness\MediaServer\"
copy ".\s3_storage_plugin.dll" "C:\Program Files\Network Optix\Nx Witness\MediaServer\plugins\"

echo "Settingup sdk ..Done"
