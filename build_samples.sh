#!/bin/bash

## Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

# build license config binary
cd Storage_SDK_Installation/
rm -rf build
./generate -b Debug --compiler-version 10 -a armv8 --build-directory build
ninja -C build
cp build/Storage_SDK_Installation ../install-storage-sdk/

# build storage sdk
cd ../samples/s3_storage_plugin/src/
rm -rf build
./generate -b Debug --compiler-version 10 -a armv8 --build-directory build
ninja -C build
cp build/libs3_storage_plugin.so ../../../install-storage-sdk/

cd ../../../
rm -rf install-storage-sdk.tar
tar -cvf install-storage-sdk.tar ./install-storage-sdk

echo "Package created as install-storage-sdk.tar"



