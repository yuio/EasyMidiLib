#!/bin/bash

# Change to script's directory
cd "$(dirname "$0")"

CONFIG="Release"
if [ "$1" = "debug" ]; then
    CONFIG="Debug"
fi

echo "Building iOS $CONFIG configuration..."

# Create directories
mkdir -p lib/ios/universal/$CONFIG
mkdir -p bin/ios/universal/$CONFIG
mkdir -p _intermediate/$CONFIG

# iOS SDK paths
IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
IOS_SIM_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)

if [ "$CONFIG" = "Debug" ]; then
    # Debug build - ARM64 (device)
    echo "Compiling library for ARM64 device (Debug)..."
    clang++ -c -g -O0 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Debug/EasyMidiLib_macCoreMidi_arm64.o
    clang++ -c -g -O0 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Debug/EasyMidiLib_arm64.o
    
    # Debug build - x86_64 (simulator)
    echo "Compiling library for x86_64 simulator (Debug)..."
    clang++ -c -g -O0 -arch x86_64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Debug/EasyMidiLib_macCoreMidi_x86_64.o
    clang++ -c -g -O0 -arch x86_64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Debug/EasyMidiLib_x86_64.o
    
    # Debug build - ARM64 simulator 
    echo "Compiling library for ARM64 simulator (Debug)..."
    clang++ -c -g -O0 -arch arm64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Debug/EasyMidiLib_macCoreMidi_arm64_sim.o
    clang++ -c -g -O0 -arch arm64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Debug/EasyMidiLib_arm64_sim.o
    
    # Create universal library using libtool
    libtool -static -o lib/ios/universal/Debug/libEasyMidiLib.a _intermediate/Debug/EasyMidiLib_macCoreMidi_arm64.o _intermediate/Debug/EasyMidiLib_arm64.o _intermediate/Debug/EasyMidiLib_macCoreMidi_x86_64.o _intermediate/Debug/EasyMidiLib_x86_64.o _intermediate/Debug/EasyMidiLib_macCoreMidi_arm64_sim.o _intermediate/Debug/EasyMidiLib_arm64_sim.o
    
    echo "Note: Test app not built for iOS (requires iOS project)"
    
    # Clean up intermediate files (optional - you can keep them in _intermediate/)
    # rm _intermediate/Debug/EasyMidiLib_*.o
else
    # Release build - ARM64 (device)
    echo "Compiling library for ARM64 device (Release)..."
    clang++ -c -O2 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Release/EasyMidiLib_macCoreMidi_arm64.o
    clang++ -c -O2 -arch arm64 -isysroot $IOS_SDK -mios-version-min=12.0 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Release/EasyMidiLib_arm64.o
    
    # Release build - x86_64 (simulator)
    echo "Compiling library for x86_64 simulator (Release)..."
    clang++ -c -O2 -arch x86_64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Release/EasyMidiLib_macCoreMidi_x86_64.o
    clang++ -c -O2 -arch x86_64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Release/EasyMidiLib_x86_64.o
    
    # Release build - ARM64 simulator
    echo "Compiling library for ARM64 simulator (Release)..."
    clang++ -c -O2 -arch arm64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib_macCoreMidi.cpp -o _intermediate/Release/EasyMidiLib_macCoreMidi_arm64_sim.o
    clang++ -c -O2 -arch arm64 -isysroot $IOS_SIM_SDK -mios-simulator-version-min=12.0 -Iinclude src/EasyMidiLib.cpp -o _intermediate/Release/EasyMidiLib_arm64_sim.o
    
    # Create universal library using libtool
    libtool -static -o lib/ios/universal/Release/libEasyMidiLib.a _intermediate/Release/EasyMidiLib_macCoreMidi_arm64.o _intermediate/Release/EasyMidiLib_arm64.o _intermediate/Release/EasyMidiLib_macCoreMidi_x86_64.o _intermediate/Release/EasyMidiLib_x86_64.o _intermediate/Release/EasyMidiLib_macCoreMidi_arm64_sim.o _intermediate/Release/EasyMidiLib_arm64_sim.o
    
    echo "Note: Test app not built for iOS (requires iOS project)"
    
    # Clean up intermediate files (optional - you can keep them in _intermediate/)
    # rm _intermediate/Release/EasyMidiLib_*.o
fi

echo "iOS $CONFIG build completed!"