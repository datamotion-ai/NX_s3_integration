#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

echo "Settingup sdk .."

./Storage_SDK_Installation

cp license.config /mnt/plugin/networkoptix/mediaserver/bin/
cp s3.config /mnt/plugin/networkoptix/mediaserver/bin/
cp libs3_storage_plugin.so /mnt/plugin/networkoptix/mediaserver/bin/plugins/

echo "Settingup sdk done"

