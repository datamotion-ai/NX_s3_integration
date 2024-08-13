#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

echo "Settingup sdk .."

apt-get install libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev zenity

current_directory=$(pwd)
echo "Current directory: $current_directory"

license_file="$current_directory/license.config"

if [ -f $license_file ]; then
    rm -f $license_file
fi

./Storage_SDK_License_Config "61e66cbda21b59c53713" "6bc96cb9bb0a54d43615c4" "78f73f8fa50a43801321ffbe" "7ff72785" "6ae729bbeb2c5bcf3104" "7dfb2d869d1a52" "79f7288fa44f74c52a14db92bc"

if [ -f $license_file ]; then

    selected_folder=$(zenity --file-selection --directory --title="Select mediaserver folder")

    if [ -z $selected_folder ]; then
        echo "No folder was selected."
        exit 1
    fi

    echo "Selected folder: $selected_folder"

    cp $license_file $selected_folder/bin/
    cp $current_directory/s3.config $selected_folder/bin/
    cp $current_directory/libs3_storage_plugin.so $selected_folder/bin/plugins/

    echo "Settingup sdk done"

else
    echo "File $license_file does not exist."
    exit 1
fi