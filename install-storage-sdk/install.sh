#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

echo "Settingup sdk .."

apt-get install libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
cp license.config /opt/networkoptix/mediaserver/bin/
cp libs3_storage_plugin.so /opt/networkoptix/mediaserver/bin/plugins/
systemctl restart networkoptix-mediaserver.service

echo "Settingup sdk done"

