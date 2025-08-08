#!/bin/bash

CONFIG="Release"
if [ "$1" = "debug" ]; then
    CONFIG="Debug"
fi

echo "Building Linux $CONFIG configuration..."

# Create directories
mkdir -p lib/linux/x64/$CONFIG
mkdir -p bin/$CONFIG
mkdir -p _intermediate/$CONFIG

if [ "$CONFIG" = "Debug" ]; then
    # Debug build
    echo "Compiling library (Debug)..."
    clang++ -c -g -O0 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Debug/EasyMidiLib.o
    clang++ -c -g -O0 -Iinclude src/EasyMidiLib_linuxAlsa.cpp -o _intermediate/Debug/EasyMidiLib_linuxAlsa.o
    ar rcs lib/linux/x64/Debug/libEasyMidiLib.a _intermediate/Debug/EasyMidiLib.o _intermediate/Debug/EasyMidiLib_linuxAlsa.o
    
    echo "Compiling test app (Debug)..."
    clang++ -g -O0 -Iinclude src/EasyMidiLibTest.cpp lib/linux/x64/Debug/libEasyMidiLib.a -lasound -lpthread -o bin/Debug/EasyMidiLibTest
else
    # Release build
    echo "Compiling library (Release)..."
    clang++ -c -O2 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Release/EasyMidiLib.o
    clang++ -c -O2 -Iinclude src/EasyMidiLib_linuxAlsa.cpp -o _intermediate/Release/EasyMidiLib_linuxAlsa.o
    ar rcs lib/linux/x64/Release/libEasyMidiLib.a _intermediate/Release/EasyMidiLib.o _intermediate/Release/EasyMidiLib_linuxAlsa.o
    
    echo "Compiling test app (Release)..."
    clang++ -O2 -Iinclude src/EasyMidiLibTest.cpp lib/linux/x64/Release/libEasyMidiLib.a -lasound -lpthread -o bin/Release/EasyMidiLibTest
fi

echo "Linux $CONFIG build completed!"