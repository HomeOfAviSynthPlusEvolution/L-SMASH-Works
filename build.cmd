@echo off

mkdir build\x86
pushd build\x86
cmake -DCMAKE_GENERATOR_PLATFORM=Win32 -D_DIR=x86 ..\..\
popd
mkdir build\x64
pushd build\x64
cmake -DCMAKE_GENERATOR_PLATFORM=x64 -D_DIR=x64 ..\..\
popd
cmake --build build\x86 --config Release
cmake --build build\x64 --config Release
