#!/bin/bash

set -e #< Exit on error.
set -u #< Prohibit undefined variables.

(set -x #< Log each command.
    rm -rf "lib"
)

mkdir lib
cd lib

echo "Settingup aws sdk"

sudo apt-get install libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
cd aws-sdk-cpp
mkdir build
cd build
cmake .. -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local/ -DCMAKE_INSTALL_PREFIX=/usr/local/ -DBUILD_ONLY="s3" -DBUILD_SHARED_LIBS=OFF -DENABLE_TESTING=OFF
make
sudo make install

echo "Settingup jsoncpp"

cd ../..
git clone https://github.com/open-source-parsers/jsoncpp
cd jsoncpp
git checkout 54fc4e2
mkdir build
cd build
cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON ..
cmake --build . --config Release

echo "Setup Done"

