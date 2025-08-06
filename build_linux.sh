#!/bin/bash

CONFIG="Release"
if [ "$1" = "debug" ]; then
    CONFIG="Debug"
fi

echo "Building Linux $CONFIG configuration..."

# Create directories
mkdir -p lib/x64/$CONFIG
mkdir -p bin/x64/$CONFIG

if [ "$CONFIG" = "Debug" ]; then
    # Debug build
    echo "Compiling library (Debug)..."
    g++ -c -g -O0 -Iinclude src/EasyMidiLib_linuxAlsa.cpp -o lib/x64/Debug/EasyMidiLib.o
    ar rcs lib/x64/Debug/EasyMidiLib.a lib/x64/Debug/EasyMidiLib.o
    
    echo "Compiling test app (Debug)..."
    g++ -g -O0 -Iinclude src/EasyMidiLibTest.cpp lib/x64/Debug/EasyMidiLib.a -lasound -lpthread -o bin/x64/Debug/EasyMidiLibTest
else
    # Release build
    echo "Compiling library (Release)..."
    g++ -c -O2 -Iinclude src/EasyMidiLib_linuxAlsa.cpp -o lib/x64/Release/EasyMidiLib.o
    ar rcs lib/x64/Release/EasyMidiLib.a lib/x64/Release/EasyMidiLib.o
    
    echo "Compiling test app (Release)..."
    g++ -O2 -Iinclude src/EasyMidiLibTest.cpp lib/x64/Release/EasyMidiLib.a -lasound -lpthread -o bin/x64/Release/EasyMidiLibTest
fi

echo "Linux $CONFIG build completed!"