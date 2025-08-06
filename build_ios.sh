#!/bin/bash

CONFIG="Release"
if [ "$1" = "debug" ]; then
    CONFIG="Debug"
fi

echo "Building iOS $CONFIG configuration..."

# Create directories
mkdir -p lib/arm64/$CONFIG
mkdir -p bin/arm64/$CONFIG

# Get iOS SDK path
IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
IOS_SIM_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)

if [ "$CONFIG" = "Debug" ]; then
    # Debug build - Device
    echo "Compiling library for iOS Device (Debug)..."
    clang++ -c -g -O0 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Debug/EasyMidiLib_device.o
    
    # Debug build - Simulator
    echo "Compiling library for iOS Simulator (Debug)..."
    clang++ -c -g -O0 -arch x86_64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Debug/EasyMidiLib_sim.o
    
    # Create universal library
    lipo -create lib/arm64/Debug/EasyMidiLib_device.o lib/arm64/Debug/EasyMidiLib_sim.o -output lib/arm64/Debug/EasyMidiLib.o
    ar rcs lib/arm64/Debug/EasyMidiLib.a lib/arm64/Debug/EasyMidiLib.o
    
    echo "Compiling test app for iOS Device (Debug)..."
    clang++ -g -O0 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLibTest.cpp lib/arm64/Debug/EasyMidiLib.a -framework CoreMIDI -framework CoreFoundation -o bin/arm64/Debug/EasyMidiLibTest
else
    # Release build - Device
    echo "Compiling library for iOS Device (Release)..."
    clang++ -c -O2 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Release/EasyMidiLib_device.o
    
    # Release build - Simulator  
    echo "Compiling library for iOS Simulator (Release)..."
    clang++ -c -O2 -arch x86_64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -framework CoreMIDI -framework CoreFoundation -o lib/arm64/Release/EasyMidiLib_sim.o
    
    # Create universal library
    lipo -create lib/arm64/Release/EasyMidiLib_device.o lib/arm64/Release/EasyMidiLib_sim.o -output lib/arm64/Release/EasyMidiLib.o
    ar rcs lib/arm64/Release/EasyMidiLib.a lib/arm64/Release/EasyMidiLib.o
    
    echo "Compiling test app for iOS Device (Release)..."
    clang++ -O2 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLibTest.cpp lib/arm64/Release/EasyMidiLib.a -framework CoreMIDI -framework CoreFoundation -o bin/arm64/Release/EasyMidiLibTest
fi

# Clean up temporary files
rm -f lib/arm64/$CONFIG/EasyMidiLib_device.o lib/arm64/$CONFIG/EasyMidiLib_sim.o

echo "iOS $CONFIG build completed!"