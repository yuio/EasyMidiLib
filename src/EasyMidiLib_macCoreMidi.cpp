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

static bool                 initialized       = false;
static std::string          lastError         = "";
static EasyMidiLibListener* mainListener      = 0;

//--------------------------------------------------------------------------------------------------------------------------

static MIDIClientRef        midiClient         = 0;
static MIDIPortRef          inputPort          = 0;
static MIDIPortRef          outputPort         = 0;

struct MidiDeviceInfo 
{
    EasyMidiLibDevice             userDev   = {};

    MIDIEndpointRef               endpoint  = 0;
    bool                          isSource  = false;
    std::vector<uint8_t>          inputQueue;
};

static std::mutex                           devicesMutex;
static std::map<std::string,MidiDeviceInfo> inputs ;
static std::map<std::string,MidiDeviceInfo> outputs;

//--------------------------------------------------------------------------------------------------------------------------

static std::string GetEndpointName(MIDIEndpointRef endpoint)
{
    CFStringRef name = nullptr;
    OSStatus result = MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name);
    if (result != noErr || !name)
        return "Unknown Device";
    
    char buffer[256];
    Boolean success = CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(name);
    
    return success ? std::string(buffer) : "Unknown Device";
}

//--------------------------------------------------------------------------------------------------------------------------

static std::string GetEndpointID(MIDIEndpointRef endpoint)
{
    SInt32 uniqueID = 0;
    OSStatus result = MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &uniqueID);
    if (result != noErr)
        return std::to_string((uintptr_t)endpoint); // fallback to pointer value
    
    return std::to_string(uniqueID);
}

//--------------------------------------------------------------------------------------------------------------------------

static void deviceConnected(MIDIEndpointRef endpoint, bool isInput, std::map<std::string,MidiDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    std::string id   = GetEndpointID(endpoint);
    std::string name = GetEndpointName(endpoint);

    auto it = devices.find(id);
    bool alreadyExists = (it != devices.end());
    if (alreadyExists)
    {
        MidiDeviceInfo& d = it->second;
        if (!d.userDev.connected)
        {
            d.inputQueue.clear();
            d.userDev.connected = true;
            d.endpoint = endpoint;
            if (mainListener)
                mainListener->deviceReconnected(&d.userDev);
        }
        else
        {
            // should not happen
        }
    }
    else
    {
        MidiDeviceInfo d;
        d.userDev.isInput         = isInput;
        d.userDev.name            = name;
        d.userDev.id              = id;
        d.userDev.connected       = true;
        d.userDev.opened          = false;
        d.userDev.userPtrParam    = 0;
        d.userDev.userIntParam    = 0;
        d.userDev.internalHandler = 0;
        d.endpoint                = endpoint;
        d.isSource                = isInput;
        d.inputQueue.reserve(10240);

        devices[id] = std::move(d);
        devices[id].userDev.internalHandler = &devices[id];

        if (mainListener)
            mainListener->deviceConnected(&devices[id].userDev);
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void deviceDisconnected(const std::string& deviceId, std::map<std::string,MidiDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    const char* deviceType = (&devices == &inputs) ? "input" : "output";

    auto it = devices.find(deviceId);
    if (it != devices.end())
    {
        MidiDeviceInfo& d = it->second;
        d.inputQueue.clear();
        d.userDev.connected = false;

        if (d.userDev.opened)
        {
            if (d.userDev.isInput)
                EasyMidiLib_inputClose(&d.userDev);
            else
                EasyMidiLib_outputClose(&d.userDev);
        }
        
        if (mainListener)
            mainListener->deviceDisconnected(&d.userDev);
    }
    else
    {
        printf("EasyMidiLib: Untracked %s disconnected (id:%s) (this shouldn't happen)\n", deviceType, deviceId.c_str());
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void devicesEnumeration(std::map<std::string,MidiDeviceInfo>& src, std::vector<const EasyMidiLibDevice*>& dst)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    dst.resize(0);
    for (auto& it : src)
        if (it.second.userDev.connected)
            dst.push_back(&it.second.userDev);
}

//--------------------------------------------------------------------------------------------------------------------------

static std::vector<const EasyMidiLibDevice*> userInputsEnumeration;

void EasyMidiLib_updateInputsEnumeration()
{
    devicesEnumeration(inputs, userInputsEnumeration);
}

size_t EasyMidiLib_getInputDevicesNum()
{
    return userInputsEnumeration.size();
}

const EasyMidiLibDevice* EasyMidiLib_getInputDevice(size_t i)
{
    return userInputsEnumeration[i];
}

//--------------------------------------------------------------------------------------------------------------------------

static std::vector<const EasyMidiLibDevice*> userOutputsEnumeration;

void EasyMidiLib_updateOutputsEnumeration()
{
    devicesEnumeration(outputs, userOutputsEnumeration);
}

size_t EasyMidiLib_getOutputDevicesNum()
{
    return userOutputsEnumeration.size();
}

const EasyMidiLibDevice* EasyMidiLib_getOutputDevice(size_t i)
{
    return userOutputsEnumeration[i];
}

//--------------------------------------------------------------------------------------------------------------------------

static void setLastErrorf(const char* textf, ...)
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

static void MIDINotifyCallback(const MIDINotification *message, void *refCon)
{
    printf("MIDINotifyCallback: messageID = %d\n", (int)message->messageID);
    switch (message->messageID) {
        case kMIDIMsgObjectAdded: {
            printf("Device added notification\n");
            const MIDIObjectAddRemoveNotification *addRemoveMsg = (const MIDIObjectAddRemoveNotification *)message;
            MIDIEndpointRef endpoint = (MIDIEndpointRef)addRemoveMsg->child;
            
            // Check if it's a source (input) or destination (output)  
            ItemCount sourceCount = MIDIGetNumberOfSources();
            bool isInput = false;
            
            // Check if the endpoint is in the sources list
            for (ItemCount i = 0; i < sourceCount; i++) {
                if (MIDIGetSource(i) == endpoint) {
                    isInput = true;
                    break;
                }
            }
            
            std::map<std::string,MidiDeviceInfo>& devices = isInput ? inputs : outputs;
            deviceConnected(endpoint, isInput, devices);
            break;
        }
        case kMIDIMsgObjectRemoved: {
            printf("Device removed notification\n");
            const MIDIObjectAddRemoveNotification *addRemoveMsg = (const MIDIObjectAddRemoveNotification *)message;
            MIDIEndpointRef endpoint = (MIDIEndpointRef)addRemoveMsg->child;
            std::string id = GetEndpointID(endpoint);
            
            // Check both input and output maps
            auto inputIt = inputs.find(id);
            if (inputIt != inputs.end()) {
                deviceDisconnected(id, inputs);
            }
            
            auto outputIt = outputs.find(id);
            if (outputIt != outputs.end()) {
                deviceDisconnected(id, outputs);
            }
            break;
        }
        case kMIDIMsgSetupChanged:
        case kMIDIMsgPropertyChanged:
        case kMIDIMsgThruConnectionsChanged:
        case kMIDIMsgSerialPortOwnerChanged:
        case kMIDIMsgIOError:
        default:
            // Handle other message types or ignore them
            break;
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void MIDIReadCallback(const MIDIPacketList *packetList, void *readProcRefCon, void *srcConnRefCon)
{
    if (!mainListener) return;
    
    MidiDeviceInfo* device = (MidiDeviceInfo*)srcConnRefCon;
    if (!device || !device->userDev.opened) return;

    std::lock_guard<std::mutex> lock(devicesMutex);

    const MIDIPacket *packet = &packetList->packet[0];
    for (UInt32 i = 0; i < packetList->numPackets; ++i) {
        size_t prevSize = device->inputQueue.size();
        device->inputQueue.resize(prevSize + packet->length);
        memcpy(device->inputQueue.data() + prevSize, packet->data, packet->length);

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

bool EasyMidiLib_init(EasyMidiLibListener* listener)
{
    if (initialized) return true;

    bool ok = true;

    // Set listener
    mainListener = listener;

    // Create MIDI client
    if (ok) {
        OSStatus result = MIDIClientCreate(CFSTR("EasyMidiLib"), MIDINotifyCallback, nullptr, &midiClient);
        if (result != noErr) {
            setLastErrorf("Failed to create MIDI client: %d", (int)result);
            ok = false;
        }
    }

    // Create input port
    if (ok) {
        OSStatus result = MIDIInputPortCreate(midiClient, CFSTR("Input"), MIDIReadCallback, nullptr, &inputPort);
        if (result != noErr) {
            setLastErrorf("Failed to create MIDI input port: %d", (int)result);
            ok = false;
        }
    }

    // Create output port
    if (ok) {
        OSStatus result = MIDIOutputPortCreate(midiClient, CFSTR("Output"), &outputPort);
        if (result != noErr) {
            setLastErrorf("Failed to create MIDI output port: %d", (int)result);
            ok = false;
        }
    }

    // Enumerate input sources
    if (ok) {
        ItemCount sourceCount = MIDIGetNumberOfSources();
        for (ItemCount i = 0; i < sourceCount; i++) {
            MIDIEndpointRef source = MIDIGetSource(i);
            deviceConnected(source, true, inputs);
        }
    }

    // Enumerate output destinations
    if (ok) {
        ItemCount destCount = MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < destCount; i++) {
            MIDIEndpointRef dest = MIDIGetDestination(i);
            deviceConnected(dest, false, outputs);
        }
    }

    // Done if errors or set as initialized if ok
    if (!ok)
        EasyMidiLib_done();
    else {
        initialized = true;
        EasyMidiLib_updateInputsEnumeration();
        EasyMidiLib_updateOutputsEnumeration();
        if (mainListener)
            mainListener->libInit();
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_update()
{
    return true;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_done()
{
    // Notify to user using listener 'callback'
    if (initialized && mainListener)
        mainListener->libDone();

    // Close inputs
    for (auto& it : inputs)
        EasyMidiLib_inputClose(&it.second.userDev);
    inputs.clear();

    // Close outputs
    for (auto& it : outputs)
        EasyMidiLib_outputClose(&it.second.userDev);
    outputs.clear();

    // Clear enumeration lists
    userInputsEnumeration.clear();
    userOutputsEnumeration.clear();

    // Dispose MIDI client (this also disposes ports)
    if (midiClient) {
        MIDIClientDispose(midiClient);
        midiClient = 0;
        inputPort = 0;
        outputPort = 0;
    }

    // Reset status flags
    initialized = false;
    mainListener = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen(size_t enumIndex, void* userPtrParam, int64_t userIntParam)
{
    if (enumIndex < userInputsEnumeration.size())
        return EasyMidiLib_inputOpen(userInputsEnumeration[enumIndex], userPtrParam, userIntParam);
    else {
        setLastErrorf("EasyMidiLib_inputOpen index %zu out of range (enum elements %zu)", enumIndex, userInputsEnumeration.size());
        return false;
    }
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen(const EasyMidiLibDevice* dev, void* userPtrParam, int64_t userIntParam)
{
    bool ok = true;

    // Check input type
    if (!dev->isInput) {
        ok = false;
        setLastErrorf("EasyMidiLib_inputOpen for an output midi device:%s(%s)", dev->name.c_str(), dev->id.c_str());
    }

    // Check already opened
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if (ok) {
        if (device->userDev.opened) {
            ok = false;
            setLastErrorf("Already open:%s(%s)", dev->name.c_str(), dev->id.c_str());
        }
    }

    // Connect to source
    if (ok) {
        OSStatus result = MIDIPortConnectSource(inputPort, device->endpoint, device);
        if (result != noErr) {
            ok = false;
            setLastErrorf("Unable to connect to source:%s(%s) error:%d", dev->name.c_str(), dev->id.c_str(), (int)result);
        }
    }

    // Set as opened
    if (ok) {
        device->userDev.userPtrParam = userPtrParam;
        device->userDev.userIntParam = userIntParam;
        device->userDev.opened = true;

        if (mainListener)
            mainListener->deviceOpen(dev);
    }

    // Close if errors
    if (!ok) {
        EasyMidiLib_inputClose(dev);
        setLastErrorf("Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str());
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_inputClose(const EasyMidiLibDevice* dev)
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->userDev.opened;

    if (wasOpened && inputPort) {
        MIDIPortDisconnectSource(inputPort, device->endpoint);
    }

    device->userDev.opened = false;

    if (wasOpened && mainListener)
        mainListener->deviceClose(dev);

    device->userDev.userPtrParam = 0;
    device->userDev.userIntParam = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen(size_t enumIndex, void* userPtrParam, int64_t userIntParam)
{
    if (enumIndex < userOutputsEnumeration.size())
        return EasyMidiLib_outputOpen(userOutputsEnumeration[enumIndex], userPtrParam, userIntParam);
    else {
        setLastErrorf("EasyMidiLib_outputOpen index %zu out of range (enum elements %zu)", enumIndex, userOutputsEnumeration.size());
        return false;
    }
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen(const EasyMidiLibDevice* dev, void* userPtrParam, int64_t userIntParam)
{
    bool ok = true;

    // Check output type
    if (dev->isInput) {
        ok = false;
        setLastErrorf("EasyMidiLib_outputOpen for an input midi device:%s(%s)", dev->name.c_str(), dev->id.c_str());
    }

    // Check already opened
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if (ok) {
        if (device->userDev.opened) {
            ok = false;
            setLastErrorf("Already open:%s(%s)", dev->name.c_str(), dev->id.c_str());
        }
    }

    // Set as opened (no connection needed for output)
    if (ok) {
        device->userDev.opened = true;
        device->userDev.userPtrParam = userPtrParam;
        device->userDev.userIntParam = userIntParam;

        if (mainListener)
            mainListener->deviceOpen(dev);
    }

    // Close if errors
    if (!ok) {
        EasyMidiLib_outputClose(dev);
        setLastErrorf("Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str());
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_outputClose(const EasyMidiLibDevice* dev)
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->userDev.opened;

    device->userDev.opened = false;

    if (wasOpened && mainListener)
        mainListener->deviceClose(dev);

    device->userDev.userPtrParam = 0;
    device->userDev.userIntParam = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputSend(const EasyMidiLibDevice* dev, const uint8_t* data, size_t size)
{
    bool ok = true;

    // Check output type
    if (dev->isInput) {
        ok = false;
        setLastErrorf("EasyMidiLib_outputSend: can't send using input midi device:%s(%s)", dev->name.c_str(), dev->id.c_str());
    }

    // Check already opened
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if (ok) {
        if (!device->userDev.opened) {
            ok = false;
            setLastErrorf("EasyMidiLib_outputSend: closed device:%s(%s)", dev->name.c_str(), dev->id.c_str());
        }
    }

    if (ok) {
        if (mainListener)
            mainListener->deviceOutData(&device->userDev, data, size);

        // Create MIDI packet
        Byte packetBuffer[1024];
        MIDIPacketList *packetList = (MIDIPacketList*)packetBuffer;
        MIDIPacket *packet = MIDIPacketListInit(packetList);
        
        if (packet) {
            packet = MIDIPacketListAdd(packetList, sizeof(packetBuffer), packet, 0, size, data);
            if (packet) {
                OSStatus result = MIDISend(outputPort, device->endpoint, packetList);
                if (result != noErr) {
                    ok = false;
                    setLastErrorf("MIDISend failed: %d", (int)result);
                }
            } else {
                ok = false;
                setLastErrorf("Failed to create MIDI packet");
            }
        } else {
            ok = false;
            setLastErrorf("Failed to initialize MIDI packet list");
        }
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

#endif //__APPLE__