#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

echo "Settingup sdk .."

systemctl stop digitalwatchdog-mediaserver.service

apt-get install libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
./Storage_SDK_License_Config "DW Spectrum"
cp license.config /opt/digitalwatchdog/mediaserver/bin/
cp s3.config /opt/digitalwatchdog/mediaserver/bin/
cp libs3_storage_plugin.so /opt/digitalwatchdog/mediaserver/bin/plugins/

systemctl start digitalwatchdog-mediaserver.service

echo "Settingup sdk done"

