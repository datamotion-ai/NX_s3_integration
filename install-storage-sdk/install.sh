#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

echo "Settingup sdk .."

current_directory=$(pwd)
echo "Current directory: $current_directory"

license_file="$current_directory/license.config"

if [ -f $license_file ]; then
    rm -f $license_file
fi

./Storage_SDK_Installation "61e66cbda21b59c53713" "6bc96cb9bb0a54d43615c4" 

if [ -f $license_file ]; then

    cp license.config /mnt/plugin/dwspectrum/mediaserver/bin/
    cp s3.config /mnt/plugin/dwspectrum/mediaserver/bin/
    cp libs3_storage_plugin.so /mnt/plugin/dwspectrum/mediaserver/bin/plugins/

    echo "Settingup sdk done"

else
    echo "File $license_file does not exist."
    exit 1
fi