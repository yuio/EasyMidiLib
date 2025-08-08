#ifdef __linux__

#include "EasyMidiLib.h"
#include <alsa/asoundlib.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>

//--------------------------------------------------------------------------------------------------------------------------

static bool                 initialized       = false;
static std::string          lastError         = "";
static EasyMidiLibListener* mainListener      = 0;

//--------------------------------------------------------------------------------------------------------------------------

struct MidiDeviceInfo 
{
    EasyMidiLibDevice             userDev   = {};
    
    snd_rawmidi_t*                rawmidi   = nullptr;
    std::string                   devicePath;
    std::vector<uint8_t>          inputQueue;
    bool                          inputThreadRunning = false;
    std::thread                   inputThread;
    uint64_t                      enumerationStamp = 0;
};

static std::mutex                           devicesMutex;
static std::map<std::string,MidiDeviceInfo> inputs ;
static std::map<std::string,MidiDeviceInfo> outputs;

static bool enumThreadRunning = false;
static std::thread enumThread;

//--------------------------------------------------------------------------------------------------------------------------

static void deviceConnected ( const std::string& id, const std::string& name, bool isInput, const std::string& devicePath, uint64_t stamp )
{
    std::map<std::string,MidiDeviceInfo>& devices = isInput ? inputs : outputs;

    auto it = devices.find(id);
    bool alreadyExists = (it != devices.end());
    
    if ( alreadyExists )
    {
        MidiDeviceInfo& d = it->second;
        d.enumerationStamp = stamp;
        if ( !d.userDev.connected )
        {
            d.inputQueue.clear();
            d.userDev.connected = true;
            d.devicePath = devicePath;
            if ( mainListener )
                mainListener->deviceReconnected ( &d.userDev );
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
        d.devicePath              = devicePath;
        d.enumerationStamp        = stamp;
        d.inputQueue.reserve(10240);

        devices[id] = std::move(d);
        devices[id].userDev.internalHandler = &devices[id];

        if ( mainListener )
            mainListener->deviceConnected ( &devices[id].userDev );
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void deviceDisconnected ( const std::string& id, bool isInput )
{
    std::map<std::string,MidiDeviceInfo>& devices = isInput ? inputs : outputs;

    auto it = devices.find(id);
    if ( it != devices.end() )
    {
        MidiDeviceInfo& d = it->second;
        d.inputQueue.clear();
        d.userDev.connected = false;

        if ( d.userDev.opened )
        {
            if ( d.userDev.isInput )
                EasyMidiLib_inputClose ( &d.userDev );
            else
                EasyMidiLib_outputClose ( &d.userDev );
        }
        
        if ( mainListener )
            mainListener->deviceDisconnected ( &d.userDev );
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void enumerateDevices()
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    
    // Get current timestamp for this enumeration
    uint64_t currentStamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    /*
    bool hitsEnumeration = false;
    if ( hitsEnumeration )
    {
        // Other way to enum
        void** hints = nullptr;
        if ( snd_device_name_hint(-1, "rawmidi", &hints) >= 0 )
        {
            for (void** hint = hints; *hint != nullptr; ++hint)
            {
                const char* name = snd_device_name_get_hint(*hint, "NAME");
                const char* desc = snd_device_name_get_hint(*hint, "DESC");
                const char* ioid = snd_device_name_get_hint(*hint, "IOID");

                if (name)
                {
                    std::string devicePath = name;
                    std::string deviceName = desc ? desc : name;
                    std::string deviceId = name;
                    
                    // Replace colons and other problematic characters in ID
                    std::replace(deviceId.begin(), deviceId.end(), ':', '_');
                    std::replace(deviceId.begin(), deviceId.end(), ',', '_');
                    
                    if (!ioid || strcmp(ioid, "Output") == 0)
                    {
                        // Input device or bidirectional
                        deviceConnected(deviceId + "_in", deviceName + " (Input)", true, devicePath, currentStamp);
                    }
                    
                    if (!ioid || strcmp(ioid, "Input") == 0)
                    {
                        // Output device or bidirectional  
                        deviceConnected(deviceId + "_out", deviceName + " (Output)", false, devicePath, currentStamp);
                    }
                }
            }

            snd_device_name_free_hint(hints);
        }
    }
    else
    */
    {
        // Enumerate using ALSA card interface
        int card = -1;
        while (snd_card_next(&card) >= 0 && card >= 0)
        {
            snd_ctl_t* ctl;
            char name[32];
            snprintf(name, sizeof(name), "hw:%d", card);
            
            if (snd_ctl_open(&ctl, name, 0) >= 0)
            {
                snd_ctl_card_info_t* info;
                snd_ctl_card_info_alloca(&info);
                
                if (snd_ctl_card_info(ctl, info) >= 0)
                {
                    int device = -1;
                    while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0)
                    {
                        snd_rawmidi_info_t* rawmidi_info;
                        snd_rawmidi_info_alloca(&rawmidi_info);
                        snd_rawmidi_info_set_device(rawmidi_info, device);
                        
                        // Check input
                        snd_rawmidi_info_set_stream(rawmidi_info, SND_RAWMIDI_STREAM_INPUT);
                        if (snd_ctl_rawmidi_info(ctl, rawmidi_info) >= 0)
                        {
                            std::string cardName = snd_ctl_card_info_get_name(info);
                            std::string deviceName = snd_rawmidi_info_get_name(rawmidi_info);
                            std::string fullName = cardName + ": " + deviceName;
                            std::string devicePath = "hw:" + std::to_string(card) + "," + std::to_string(device);
                            std::string deviceId = "in_card" + std::to_string(card) + "_dev" + std::to_string(device) + "_" + deviceName;
                            
                            deviceConnected(deviceId, fullName, true, devicePath, currentStamp);
                        }
                        
                        // Check output
                        snd_rawmidi_info_set_stream(rawmidi_info, SND_RAWMIDI_STREAM_OUTPUT);
                        if (snd_ctl_rawmidi_info(ctl, rawmidi_info) >= 0)
                        {
                            std::string cardName = snd_ctl_card_info_get_name(info);
                            std::string deviceName = snd_rawmidi_info_get_name(rawmidi_info);
                            std::string fullName = cardName + ": " + deviceName;
                            std::string devicePath = "hw:" + std::to_string(card) + "," + std::to_string(device);
                            std::string deviceId = "out_card" + std::to_string(card) + "_dev" + std::to_string(device) + "_" + deviceName;
                            
                            deviceConnected(deviceId, fullName, false, devicePath, currentStamp);
                        }
                    }
                }
                
                snd_ctl_close(ctl);
            }
        }
    }
    
    // Check for disconnected devices (those without current stamp)
    for (auto& it : inputs)
        if (it.second.enumerationStamp != currentStamp && it.second.userDev.connected)
            deviceDisconnected(it.first, true);
    
    for (auto& it : outputs)
        if (it.second.enumerationStamp != currentStamp && it.second.userDev.connected)
            deviceDisconnected(it.first, false);
}

//--------------------------------------------------------------------------------------------------------------------------

static void enumerationThreadFunc()
{
    while (enumThreadRunning)
    {
        enumerateDevices();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void devicesEnumeration ( std::map<std::string,MidiDeviceInfo>& src, std::vector<const EasyMidiLibDevice*>& dst )
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    dst.resize(0);
    for ( auto& it : src )
        if ( it.second.userDev.connected )
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

    // Set listener
    mainListener = listener;

    // Initial device enumeration
    if ( ok )
        enumerateDevices();

    // Start enumeration thread for device monitoring
    if ( ok )
    {
        enumThreadRunning = true;
        enumThread = std::thread(enumerationThreadFunc);
    }

    // Done if errors or set as initialized if ok
    if (!ok)
        EasyMidiLib_done();
    else
    {
        initialized = true;
        EasyMidiLib_updateInputsEnumeration ();
        EasyMidiLib_updateOutputsEnumeration();
        if ( mainListener )
            mainListener->libInit();
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_update ( )
{
    return true;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_done()
{
    // Stop enumeration thread
    if (enumThreadRunning)
    {
        enumThreadRunning = false;
        if (enumThread.joinable())
            enumThread.join();
    }

    // Notify to user using listener 'callback'
    if ( initialized && mainListener )
        mainListener->libDone();

    // Close inputs
    for ( auto& it : inputs )
        EasyMidiLib_inputClose ( &it.second.userDev );
    inputs.clear();

    // Close outputs
    for ( auto& it : outputs )
        EasyMidiLib_outputClose ( &it.second.userDev );
    outputs.clear();

    // Clear enumeration lists
    userInputsEnumeration .clear();
    userOutputsEnumeration.clear();

    // Reset status flags
    initialized  = false;
    mainListener = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

static void inputThreadFunc(MidiDeviceInfo* device)
{
    unsigned char buffer[256];
    
    while (device->inputThreadRunning)
    {
        ssize_t bytes_read = snd_rawmidi_read(device->rawmidi, buffer, sizeof(buffer));
        
        if (bytes_read > 0)
        {
            std::lock_guard<std::mutex> lock(devicesMutex);
            
            size_t prevSize = device->inputQueue.size();
            device->inputQueue.resize(prevSize + bytes_read);
            memcpy(device->inputQueue.data() + prevSize, buffer, bytes_read);
            
            if (mainListener)
            {
                size_t consumedBytes = mainListener->deviceInData(&device->userDev, device->inputQueue.data(), device->inputQueue.size());
                if (consumedBytes > 0)
                {
                    if (consumedBytes > device->inputQueue.size())
                        consumedBytes = device->inputQueue.size();
                    
                    device->inputQueue.erase(device->inputQueue.begin(), device->inputQueue.begin() + consumedBytes);
                }
            }
        }
        else if (bytes_read < 0 && bytes_read != -EAGAIN)
        {
            // Error occurred
            break;
        }
        else
        {
            // No data available, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
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

    // Open raw MIDI device for input
    if ( ok )
    {
        int err = snd_rawmidi_open(&device->rawmidi, nullptr, device->devicePath.c_str(), SND_RAWMIDI_NONBLOCK);
        if (err < 0) 
        {
            setLastErrorf("Failed to open raw MIDI device for input: %s", snd_strerror(err));
            ok = false;
        }
    }

    // Start input thread
    if ( ok )
    {
        device->userDev.userPtrParam = userPtrParam;
        device->userDev.userIntParam = userIntParam;
        device->userDev.opened       = true;

        if ( mainListener )
            mainListener->deviceOpen(dev);

        device->inputThreadRunning = true;
        device->inputThread = std::thread(inputThreadFunc, device);
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
    bool wasOpened = device->userDev.opened;

    // Stop input thread
    if (device->inputThreadRunning)
    {
        device->inputThreadRunning = false;
        if (device->inputThread.joinable())
            device->inputThread.join();
    }

    // Close raw MIDI device
    if (device->rawmidi)
    {
        snd_rawmidi_close(device->rawmidi);
        device->rawmidi = nullptr;
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

    // Open raw MIDI device for output
    if ( ok )
    {
        int err = snd_rawmidi_open(nullptr, &device->rawmidi, device->devicePath.c_str(), SND_RAWMIDI_NONBLOCK);
        if (err < 0) 
        {
            setLastErrorf("Failed to open raw MIDI device for output: %s", snd_strerror(err));
            ok = false;
        }
    }

    if ( ok )
    {
        device->userDev.opened       = true;
        device->userDev.userPtrParam = userPtrParam;
        device->userDev.userIntParam = userIntParam;

        if ( mainListener )
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

    // Close raw MIDI device
    if (device->rawmidi)
    {
        snd_rawmidi_close(device->rawmidi);
        device->rawmidi = nullptr;
    }

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

        // Send raw MIDI data directly
        ssize_t bytes_written = snd_rawmidi_write(device->rawmidi, data, size);
        
        if (bytes_written < 0)
        {
            setLastErrorf("Failed to write MIDI data: %s", snd_strerror(bytes_written));
            ok = false;
        }
        else if ((size_t)bytes_written != size)
        {
            setLastErrorf("Incomplete MIDI data write: wrote %zd of %zu bytes", bytes_written, size);
            ok = false;
        }
        else
        {
            // Ensure data is sent immediately
            snd_rawmidi_drain(device->rawmidi);
        }
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

#endif //__linux__