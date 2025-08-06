#if defined(__APPLE__)

#include "EasyMidiLib.h"
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <AudioToolbox/AudioToolbox.h>

#include <stdarg.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>

//--------------------------------------------------------------------------------------------------------------------------

EasyMidiLibTestListener EasyMidiLib_testListener;

//--------------------------------------------------------------------------------------------------------------------------

static bool                 initialized       = false;
static std::string          lastError         = "";
static EasyMidiLibListener* mainListener      = 0;

//--------------------------------------------------------------------------------------------------------------------------

static MIDIClientRef        midiClient         = 0;
static MIDIPortRef          inputPort          = 0;
static MIDIPortRef          outputPort         = 0;

struct MidiDeviceInfo 
{
    EasyMidiLibDevice   userDev         = {};
    bool                connected       = true;
    MIDIEndpointRef     endpoint        = 0;
    bool                isSource        = false;  // true for input sources, false for output destinations
    
    std::vector<uint8_t> inputQueue;
};

static std::mutex                           devicesMutex;
static std::map<std::string,MidiDeviceInfo> inputs ;
static std::map<std::string,MidiDeviceInfo> outputs;

//--------------------------------------------------------------------------------------------------------------------------

static std::string GetEndpointName(MIDIEndpointRef endpoint) 
{
    CFStringRef endpointName = nullptr;
    char name[256];
    
    if (MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &endpointName) == noErr) {
        if (CFStringGetCString(endpointName, name, sizeof(name), kCFStringEncodingUTF8)) {
            CFRelease(endpointName);
            return std::string(name);
        }
        CFRelease(endpointName);
    }
    return "Unknown Device";
}

//--------------------------------------------------------------------------------------------------------------------------

static std::string GetEndpointID(MIDIEndpointRef endpoint) 
{
    SInt32 uniqueID;
    if (MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &uniqueID) == noErr) {
        return std::to_string(uniqueID);
    }
    return std::to_string((uintptr_t)endpoint); // Fallback to endpoint reference
}

//--------------------------------------------------------------------------------------------------------------------------

static void MidiInputProc(const MIDIPacketList *pktlist, void *readProcRefCon, void *srcConnRefCon) 
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)srcConnRefCon;
    
    if (!device || !mainListener) return;
    
    std::lock_guard<std::mutex> lock(devicesMutex);
    
    const MIDIPacket *packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        // Add packet data to input queue
        size_t prevSize = device->inputQueue.size();
        device->inputQueue.resize(prevSize + packet->length);
        memcpy(device->inputQueue.data() + prevSize, packet->data, packet->length);
        
        // Process accumulated data
        size_t consumedBytes = mainListener->deviceInData(&device->userDev, device->inputQueue.data(), device->inputQueue.size());
        if (consumedBytes > 0) {
            if (consumedBytes > device->inputQueue.size())
                consumedBytes = device->inputQueue.size();
            
            device->inputQueue.erase(device->inputQueue.begin(), device->inputQueue.begin() + consumedBytes);
        }
        
        packet = MIDIPacketNext(packet);
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void MidiNotifyProc(const MIDINotification *message, void *refCon) 
{
    if (!mainListener) return;
    
    std::lock_guard<std::mutex> lock(devicesMutex);
    
    switch (message->messageID) {
        case kMIDIMsgObjectAdded: {
            const MIDIMsgObjectAddRemove *addRemoveMsg = (const MIDIMsgObjectAddRemove *)message;
            MIDIEndpointRef endpoint = (MIDIEndpointRef)addRemoveMsg->child;
            
            // Check if it's a source (input) or destination (output)
            MIDIObjectType objectType;
            if (MIDIObjectGetType(endpoint, &objectType) != noErr) return;
            
            if (objectType == kMIDIObjectType_Source || objectType == kMIDIObjectType_Destination) {
                std::string id = GetEndpointID(endpoint);
                std::string name = GetEndpointName(endpoint);
                bool isInput = (objectType == kMIDIObjectType_Source);
                
                std::map<std::string,MidiDeviceInfo>& devices = isInput ? inputs : outputs;
                
                auto it = devices.find(id);
                if (it != devices.end()) {
                    // Device reconnected
                    it->second.connected = true;
                    it->second.endpoint = endpoint;
                    if (mainListener)
                        mainListener->deviceReconnected(&it->second.userDev);
                } else {
                    // New device
                    MidiDeviceInfo d;
                    d.userDev.isInput         = isInput;
                    d.userDev.name            = name;
                    d.userDev.id              = id;
                    d.userDev.opened          = false;
                    d.userDev.userPtrParam    = 0;
                    d.userDev.userIntParam    = 0;
                    d.userDev.internalHandler = 0;
                    d.endpoint                = endpoint;
                    d.connected               = true;
                    d.isSource                = isInput;
                    d.inputQueue.reserve(10240);
                    
                    devices[id] = d;
                    devices[id].userDev.internalHandler = &devices[id];
                    
                    if (mainListener)
                        mainListener->deviceConnected(&devices[id].userDev);
                }
            }
            break;
        }
        case kMIDIMsgObjectRemoved: {
            const MIDIMsgObjectAddRemove *addRemoveMsg = (const MIDIMsgObjectAddRemove *)message;
            MIDIEndpointRef endpoint = (MIDIEndpointRef)addRemoveMsg->child;
            std::string id = GetEndpointID(endpoint);
            
            // Check both input and output maps
            auto inputIt = inputs.find(id);
            if (inputIt != inputs.end()) {
                inputIt->second.connected = false;
                if (inputIt->second.userDev.opened) {
                    EasyMidiLib_inputClose(&inputIt->second.userDev);
                }
                if (mainListener)
                    mainListener->deviceDisconnected(&inputIt->second.userDev);
            }
            
            auto outputIt = outputs.find(id);
            if (outputIt != outputs.end()) {
                outputIt->second.connected = false;
                if (outputIt->second.userDev.opened) {
                    EasyMidiLib_outputClose(&outputIt->second.userDev);
                }
                if (mainListener)
                    mainListener->deviceDisconnected(&outputIt->second.userDev);
            }
            break;
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void EnumerateDevices() 
{
    // Enumerate input sources
    ItemCount numSources = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < numSources; i++) {
        MIDIEndpointRef source = MIDIGetSource(i);
        if (source) {
            std::string id = GetEndpointID(source);
            std::string name = GetEndpointName(source);
            
            MidiDeviceInfo d;
            d.userDev.isInput         = true;
            d.userDev.name            = name;
            d.userDev.id              = id;
            d.userDev.opened          = false;
            d.userDev.userPtrParam    = 0;
            d.userDev.userIntParam    = 0;
            d.userDev.internalHandler = 0;
            d.endpoint                = source;
            d.connected               = true;
            d.isSource                = true;
            d.inputQueue.reserve(10240);
            
            inputs[id] = d;
            inputs[id].userDev.internalHandler = &inputs[id];
            
            if (mainListener)
                mainListener->deviceConnected(&inputs[id].userDev);
        }
    }
    
    // Enumerate output destinations
    ItemCount numDestinations = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < numDestinations; i++) {
        MIDIEndpointRef destination = MIDIGetDestination(i);
        if (destination) {
            std::string id = GetEndpointID(destination);
            std::string name = GetEndpointName(destination);
            
            MidiDeviceInfo d;
            d.userDev.isInput         = false;
            d.userDev.name            = name;
            d.userDev.id              = id;
            d.userDev.opened          = false;
            d.userDev.userPtrParam    = 0;
            d.userDev.userIntParam    = 0;
            d.userDev.internalHandler = 0;
            d.endpoint                = destination;
            d.connected               = true;
            d.isSource                = false;
            
            outputs[id] = d;
            outputs[id].userDev.internalHandler = &outputs[id];
            
            if (mainListener)
                mainListener->deviceConnected(&outputs[id].userDev);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void devicesEnumeration ( std::map<std::string,MidiDeviceInfo>& src, std::vector<const EasyMidiLibDevice*>& dst )
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    dst.resize(0);
    for ( auto& it : src )
        if ( it.second.connected )
            dst.push_back(&it.second.userDev);
}

//--------------------------------------------------------------------------------------------------------------------------

static std::vector<const EasyMidiLibDevice*> userInputsEnumeration;

void EasyMidiLib_updateInputsEnumeration()
{
    devicesEnumeration ( inputs, userInputsEnumeration );
}

size_t EasyMidiLib_getInputDevicesNum()
{
    return userInputsEnumeration.size();
}

const EasyMidiLibDevice* EasyMidiLib_getInputDevice( size_t i )
{
    return userInputsEnumeration[i];
}

//--------------------------------------------------------------------------------------------------------------------------

static std::vector<const EasyMidiLibDevice*> userOutputsEnumeration;

void EasyMidiLib_updateOutputsEnumeration()
{
    devicesEnumeration ( outputs, userOutputsEnumeration );
}

size_t EasyMidiLib_getOutputDevicesNum()
{
    return userOutputsEnumeration.size();
}

const EasyMidiLibDevice* EasyMidiLib_getOutputDevice( size_t i )
{
    return userOutputsEnumeration[i];
}

//--------------------------------------------------------------------------------------------------------------------------

static void setLastErrorf ( const char* textf, ... )
{
    va_list args;
    va_start(args, textf);
    int count = std::vsnprintf(nullptr, 0, textf, args);
    va_end(args);

    lastError.resize(static_cast<size_t>(count) + 1);
    va_start(args, textf);
    std::vsnprintf(&lastError[0], lastError.size(), textf, args);
    va_end(args);
    
    lastError.resize(count);
}

//--------------------------------------------------------------------------------------------------------------------------

const char* EasyMidiLib_getLastError()
{
    return lastError.c_str();
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_init( EasyMidiLibListener* listener )
{
    if (initialized) return true;

    bool ok = true;
    mainListener = listener;

    // Create MIDI client
    if (ok) {
        OSStatus status = MIDIClientCreate(CFSTR("EasyMidiLib Client"), MidiNotifyProc, nullptr, &midiClient);
        if (status != noErr) {
            setLastErrorf("Failed to create MIDI client: %d", (int)status);
            ok = false;
        }
    }

    // Create input port
    if (ok) {
        OSStatus status = MIDIInputPortCreate(midiClient, CFSTR("EasyMidiLib Input"), MidiInputProc, nullptr, &inputPort);
        if (status != noErr) {
            setLastErrorf("Failed to create MIDI input port: %d", (int)status);
            ok = false;
        }
    }

    // Create output port
    if (ok) {
        OSStatus status = MIDIOutputPortCreate(midiClient, CFSTR("EasyMidiLib Output"), &outputPort);
        if (status != noErr) {
            setLastErrorf("Failed to create MIDI output port: %d", (int)status);
            ok = false;
        }
    }

    // Enumerate existing devices
    if (ok) {
        EnumerateDevices();
    }

    // Finalize initialization
    if (!ok) {
        EasyMidiLib_done();
    } else {
        initialized = true;
        EasyMidiLib_updateInputsEnumeration();
        EasyMidiLib_updateOutputsEnumeration();
        if (mainListener)
            mainListener->libInit();
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_update ( )
{
    // CoreMIDI handles callbacks automatically, no polling needed
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_done()
{
    if (inputPort) {
        MIDIPortDispose(inputPort);
        inputPort = 0;
    }

    if (outputPort) {
        MIDIPortDispose(outputPort);
        outputPort = 0;
    }

    if (midiClient) {
        MIDIClientDispose(midiClient);
        midiClient = 0;
    }

    if (initialized && mainListener)
        mainListener->libDone();

    inputs .clear();
    outputs.clear();

    userInputsEnumeration .clear();
    userOutputsEnumeration.clear();

    initialized   = false;
    mainListener  = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen ( size_t enumIndex, void* userPtrParam, int64_t userIntParam )
{
    if ( enumIndex<userInputsEnumeration.size() )
        return EasyMidiLib_inputOpen ( userInputsEnumeration[enumIndex], userPtrParam, userIntParam );
    else
    {
        setLastErrorf("EasyMidiLib_inputOpen index %zu out of range (enum elements %zu)", enumIndex, userInputsEnumeration.size() );
        return false;
    }
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen ( const EasyMidiLibDevice* dev, void* userPtrParam, int64_t userIntParam )
{
    bool ok = true;

    // Check input type
    if ( !dev->isInput )
    {
        ok = false;
        setLastErrorf ( "EasyMidiLib_inputOpen for an output midi device:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    // Check already opened
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if ( ok )
    {
        if ( device->userDev.opened )
        {
            ok = false;
            setLastErrorf ( "Already open:%s(%s)", dev->name.c_str(), dev->id.c_str() );        
        }
    }

    // Connect to source
    if ( ok )
    {
        OSStatus status = MIDIPortConnectSource(inputPort, device->endpoint, device);
        if (status != noErr) {
            setLastErrorf("Failed to connect to MIDI source:%s(%s) status:%d", dev->name.c_str(), dev->id.c_str(), (int)status);
            ok = false;
        } else {
            device->userDev.userPtrParam = userPtrParam;
            device->userDev.userIntParam = userIntParam;
            device->userDev.opened       = true;

            if (mainListener)
                mainListener->deviceOpen(dev);
        }
    }

    // Close if errors
    if ( !ok )
    {
        EasyMidiLib_inputClose ( dev );
        setLastErrorf ( "Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_inputClose ( const EasyMidiLibDevice* dev )
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->userDev.opened ;

    if (device->userDev.opened) {
        MIDIPortDisconnectSource(inputPort, device->endpoint);
    }

    device->userDev.opened = false;

    if ( wasOpened && mainListener )
        mainListener->deviceClose(dev);

    device->userDev.userPtrParam = 0;
    device->userDev.userIntParam = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen ( size_t enumIndex, void* userPtrParam, int64_t userIntParam )
{
    if ( enumIndex<userOutputsEnumeration.size() )
        return EasyMidiLib_outputOpen ( userOutputsEnumeration[enumIndex], userPtrParam, userIntParam );
    else
    {
        setLastErrorf("EasyMidiLib_outputOpen index %zu out of range (enum elements %zu)", enumIndex, userOutputsEnumeration.size() );
        return false;
    }
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen ( const EasyMidiLibDevice* dev, void* userPtrParam, int64_t userIntParam )
{
    bool ok = true;

    // Check output type
    if ( dev->isInput )
    {
        ok = false;
        setLastErrorf ( "EasyMidiLib_outputOpen for an input midi device:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    // Check already opened
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if ( ok )
    {
        if ( device->userDev.opened )
        {
            ok = false;
            setLastErrorf ( "Already open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
        }
    }

    // Mark as opened (no explicit connection needed for output)
    if ( ok )
    {
        device->userDev.opened       = true;
        device->userDev.userPtrParam = userPtrParam;
        device->userDev.userIntParam = userIntParam;

        if (mainListener)
            mainListener->deviceOpen(dev);
    }

    // Close if errors
    if ( !ok )
    {
        EasyMidiLib_outputClose ( dev );
        setLastErrorf ( "Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_outputClose ( const EasyMidiLibDevice* dev )
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->userDev.opened;

    device->userDev.opened = false;

    if ( wasOpened && mainListener )
        mainListener->deviceClose(dev);

    device->userDev.userPtrParam = 0;
    device->userDev.userIntParam = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputSend ( const EasyMidiLibDevice* dev, const uint8_t* data, size_t size  )
{
    bool ok = true;

    // Check output type
    if ( dev->isInput )
    {
        ok = false;
        setLastErrorf ( "EasyMidiLib_outputSend: can't send using input midi device:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    // Check already opened
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if ( ok )
    {
        if ( !device->userDev.opened )
        {
            ok = false;
            setLastErrorf ( "EasyMidiLib_outputSend: closed device:%s(%s)", dev->name.c_str(), dev->id.c_str() );
        }
    }

    if ( ok ) 
    {
        if ( mainListener )
            mainListener->deviceOutData(&device->userDev, data, size );

        // Create MIDI packet list
        Byte packetBuffer[1024];
        MIDIPacketList *packetList = (MIDIPacketList*)packetBuffer;
        MIDIPacket *packet = MIDIPacketListInit(packetList);
        
        // Add data to packet (timestamp 0 = send immediately)
        packet = MIDIPacketListAdd(packetList, sizeof(packetBuffer), packet, 0, size, data);
        
        if (packet) {
            OSStatus status = MIDISend(outputPort, device->endpoint, packetList);
            if (status != noErr) {
                setLastErrorf("Failed to send MIDI data:%s(%s) status:%d", dev->name.c_str(), dev->id.c_str(), (int)status);
                ok = false;
            }
        } else {
            setLastErrorf("Failed to create MIDI packet:%s(%s)", dev->name.c_str(), dev->id.c_str());
            ok = false;
        }
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

#endif //defined(__APPLE__)
