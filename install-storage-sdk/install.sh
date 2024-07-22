#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

echo "Settingup sdk .."

systemctl stop hanwha-mediaserver.service

apt-get install libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
./Storage_SDK_License_Config "78f73f8fa50a43801321ffbe"
cp license.config /opt/hanwha/mediaserver/bin/
cp s3.config /opt/hanwha/mediaserver/bin/
cp libs3_storage_plugin.so /opt/hanwha/mediaserver/bin/plugins/

systemctl start hanwha-mediaserver.service

echo "Settingup sdk done"

