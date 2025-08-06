#!/bin/bash

CONFIG="Release"
if [ "$1" = "debug" ]; then
    CONFIG="Debug"
fi

echo "Building macOS $CONFIG configuration..."

# Create directories
mkdir -p lib/arm64/$CONFIG
mkdir -p bin/arm64/$CONFIG

if [ "$CONFIG" = "Debug" ]; then
    # Debug build - ARM64
    echo "Compiling library for ARM64 (Debug)..."
    clang++ -c -g -O0 -arch arm64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Debug/EasyMidiLib_arm64.o
    
    # Debug build - x86_64
    echo "Compiling library for x86_64 (Debug)..."
    clang++ -c -g -O0 -arch x86_64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Debug/EasyMidiLib_x86_64.o
    
    # Create universal library
    lipo -create lib/arm64/Debug/EasyMidiLib_arm64.o lib/arm64/Debug/EasyMidiLib_x86_64.o -output lib/arm64/Debug/EasyMidiLib.o
    ar rcs lib/arm64/Debug/EasyMidiLib.a lib/arm64/Debug/EasyMidiLib.o
    
    echo "Compiling test app (Debug)..."
    clang++ -g -O0 -arch arm64 -arch x86_64 -Iinclude src/EasyMidiLibTest.cpp lib/arm64/Debug/EasyMidiLib.a -framework CoreMIDI -framework CoreFoundation -o bin/arm64/Debug/EasyMidiLibTest
    
    # Clean up
    rm lib/arm64/Debug/EasyMidiLib_arm64.o lib/arm64/Debug/EasyMidiLib_x86_64.o
else
    # Release build - ARM64
    echo "Compiling library for ARM64 (Release)..."
    clang++ -c -O2 -arch arm64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Release/EasyMidiLib_arm64.o
    
    # Release build - x86_64
    echo "Compiling library for x86_64 (Release)..."
    clang++ -c -O2 -arch x86_64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Release/EasyMidiLib_x86_64.o
    
    # Create universal library
    lipo -create lib/arm64/Release/EasyMidiLib_arm64.o lib/arm64/Release/EasyMidiLib_x86_64.o -output lib/arm64/Release/EasyMidiLib.o
    ar rcs lib/arm64/Release/EasyMidiLib.a lib/arm64/Release/EasyMidiLib.o
    
    echo "Compiling test app (Release)..."
    clang++ -O2 -arch arm64 -arch x86_64 -Iinclude src/EasyMidiLibTest.cpp lib/arm64/Release/EasyMidiLib.a -framework CoreMIDI -framework CoreFoundation -o bin/arm64/Release/EasyMidiLibTest
    
    # Clean up
    rm lib/arm64/Release/EasyMidiLib_arm64.o lib/arm64/Release/EasyMidiLib_x86_64.o
fi

echo "macOS $CONFIG build completed!"