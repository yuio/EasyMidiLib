#!/bin/bash

# Change to script's directory
cd "$(dirname "$0")"

CONFIG="Release"
if [ "$1" = "debug" ]; then
    CONFIG="Debug"
fi

echo "Building macOS $CONFIG configuration..."

# Create directories
mkdir -p lib/mac/universal/$CONFIG
mkdir -p bin/mac/universal/$CONFIG
mkdir -p _intermediate/$CONFIG

if [ "$CONFIG" = "Debug" ]; then
    # Debug build - ARM64
    echo "Compiling library for ARM64 (Debug)..."
    clang++ -c -g -O0 -arch arm64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Debug/EasyMidiLib_macCoreMidi_arm64.o
    clang++ -c -g -O0 -arch arm64 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Debug/EasyMidiLib_arm64.o
    
    # Debug build - x86_64
    echo "Compiling library for x86_64 (Debug)..."
    clang++ -c -g -O0 -arch x86_64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Debug/EasyMidiLib_macCoreMidi_x86_64.o
    clang++ -c -g -O0 -arch x86_64 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Debug/EasyMidiLib_x86_64.o
    
    # Create universal library using libtool
    libtool -static -o lib/mac/universal/Debug/libEasyMidiLib.a _intermediate/Debug/EasyMidiLib_macCoreMidi_arm64.o _intermediate/Debug/EasyMidiLib_arm64.o _intermediate/Debug/EasyMidiLib_macCoreMidi_x86_64.o _intermediate/Debug/EasyMidiLib_x86_64.o
    
    echo "Compiling test app (Debug)..."
    clang++ -g -O0 -arch arm64 -arch x86_64 -Iinclude src/EasyMidiLibTest.cpp lib/mac/universal/Debug/libEasyMidiLib.a -framework CoreMIDI -framework CoreFoundation -o bin/mac/universal/Debug/EasyMidiLibTest
    
    # Clean up intermediate files (optional - you can keep them in _intermediate/)
    # rm _intermediate/Debug/EasyMidiLib_arm64.o _intermediate/Debug/EasyMidiLib_x86_64.o
else
    # Release build - ARM64
    echo "Compiling library for ARM64 (Release)..."
    clang++ -c -O2 -arch arm64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Release/EasyMidiLib_macCoreMidi_arm64.o
    clang++ -c -O2 -arch arm64 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Release/EasyMidiLib_arm64.o
    
    # Release build - x86_64
    echo "Compiling library for x86_64 (Release)..."
    clang++ -c -O2 -arch x86_64 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Release/EasyMidiLib_macCoreMidi_x86_64.o
    clang++ -c -O2 -arch x86_64 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Release/EasyMidiLib_x86_64.o
    
    # Create universal library using libtool
    libtool -static -o lib/mac/universal/Release/libEasyMidiLib.a _intermediate/Release/EasyMidiLib_macCoreMidi_arm64.o _intermediate/Release/EasyMidiLib_arm64.o _intermediate/Release/EasyMidiLib_macCoreMidi_x86_64.o _intermediate/Release/EasyMidiLib_x86_64.o
    
    echo "Compiling test app (Release)..."
    clang++ -O2 -arch arm64 -arch x86_64 -Iinclude src/EasyMidiLibTest.cpp lib/mac/universal/Release/libEasyMidiLib.a -framework CoreMIDI -framework CoreFoundation -o bin/mac/universal/Release/EasyMidiLibTest
    
    # Clean up intermediate files (optional - you can keep them in _intermediate/)
    # rm _intermediate/Release/EasyMidiLib_arm64.o _intermediate/Release/EasyMidiLib_x86_64.o
fi

echo "macOS $CONFIG build completed!"