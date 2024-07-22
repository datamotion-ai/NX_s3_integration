#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

echo "Settingup sdk .."

systemctl stop hanwha-mediaserver.service

apt-get install libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
./Storage_SDK_License_Config "6bc96cb9bb0a54d43615c4"
cp license.config /opt/hanwha/mediaserver/bin/
cp s3.config /opt/hanwha/mediaserver/bin/
cp libs3_storage_plugin.so /opt/hanwha/mediaserver/bin/plugins/

systemctl start hanwha-mediaserver.service

echo "Settingup sdk done"

