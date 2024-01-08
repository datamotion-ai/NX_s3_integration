#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

cp ../NX_s3_integration-build/s3_storage_plugin/libs3_storage_plugin.so install-storage-sdk/

rm -rf install-storage-sdk.tar
tar -cvf install-storage-sdk.tar ./install-storage-sdk

echo "Package created as install-storage-sdk.tar"
