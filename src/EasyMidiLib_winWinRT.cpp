#ifdef _WIN32

#include "EasyMidiLib.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <windows.h>
#include <conio.h>
#include <stdarg.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <thread>
#include <mutex>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Midi;
using namespace Windows::Storage::Streams;

#define AVOID_STA_BEGIN  { auto sta_thread = [&]() { init_apartment();
#define AVOID_STA_END   }; std::thread enumThread(sta_thread); enumThread.join();}

//--------------------------------------------------------------------------------------------------------------------------

static bool                 initialized       = false;
static std::string          lastError         = "";
static EasyMidiLibListener* mainListener      = 0;

//--------------------------------------------------------------------------------------------------------------------------

static DeviceWatcher    inputsWatcher  = nullptr;
static DeviceWatcher    outputsWatcher = nullptr;

struct MidiDeviceInfo 
{
    EasyMidiLibDevice             userDev   = {};

    DeviceInformation             device    = 0;
    IMidiOutPort                  outPort   = nullptr;
    IAsyncOperation<IMidiOutPort> outPortOp = nullptr;
    MidiInPort                    inPort    = nullptr;
    IAsyncOperation<MidiInPort>   inPortOp  = nullptr;
    std::vector<uint8_t>          inputQueue;
};

static std::mutex                           devicesMutex;
static std::map<std::string,MidiDeviceInfo> inputs ;
static std::map<std::string,MidiDeviceInfo> outputs;

//--------------------------------------------------------------------------------------------------------------------------

static void deviceConnected ( DeviceInformation const& info, std::map<std::string,MidiDeviceInfo>& devices )
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    std::string id   = winrt::to_string(info.Id  ());
    std::string name = winrt::to_string(info.Name());

    const char* deviceType = (&devices==&inputs) ? "input" : "output";

    auto it = devices.find(id);
    bool alreadyExists = (it==devices.end()) ? false : true;
    if ( alreadyExists )
    {
        MidiDeviceInfo& d = it->second;
        if ( !d.userDev.connected )
        {
            d.inputQueue.clear();
            d.userDev.connected=true;
            if ( mainListener )
                mainListener->deviceReconnected ( &d.userDev );
        }
        else
        {
            // should not happen
        }
    }
    else
    {
        MidiDeviceInfo d;
        d.userDev.isInput         = (&devices==&inputs);
        d.userDev.name            = name;
        d.userDev.id              = id;
        d.userDev.connected       = true;
        d.userDev.opened          = false;
        d.userDev.userPtrParam    = 0;
        d.userDev.userIntParam    = 0;
        d.userDev.internalHandler = 0;
        d.device                  = info;
        d.inputQueue.reserve(10240);

        devices[d.userDev.id] = d;
        devices[d.userDev.id].userDev.internalHandler = &devices[d.userDev.id];

        if ( mainListener )
            mainListener->deviceConnected ( &d.userDev );

    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void deviceDisconnected ( const DeviceInformationUpdate& info, std::map<std::string,MidiDeviceInfo>& devices )
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    const char* deviceType = (&devices==&inputs) ? "input" : "output";

    std::string id = winrt::to_string(info.Id());
    auto it = devices.find(id);
    if ( it != devices.end() )
    {
        MidiDeviceInfo& d = it->second;
        d.inputQueue.clear();
        d.userDev.connected=false;

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
    else
    {
        printf ( "EasyMidiLib: Untracked %s disconnected (id:%s) (this shouldn't happen)\n", deviceType, id.c_str() );
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

    // Set verbose level
    mainListener = listener;

    // Init apartment - safe to call multiple times due to reference counting
    if ( ok )
    {
        try 
        {
            //init_apartment(apartment_type::single_threaded); // also supported
            init_apartment(apartment_type::multi_threaded);
        }
        catch (winrt::hresult_error const& ex)
        {
            // If apartment already initialized with different mode, continue
            if (ex.code() == RPC_E_CHANGED_MODE)
            {
                // Already initialized by host application - continue normally
            }
            else
            {
                setLastErrorf("Failed to initialize WinRT apartment: 0x%X", ex.code().value);
                ok = false;
            }
        }
        catch (...)
        {
            setLastErrorf("Failed to initialize WinRT apartment");
            ok = false;
        }
    }

    // Enum inputs
    if ( ok )
    {
        AVOID_STA_BEGIN;

        IAsyncOperation<DeviceInformationCollection> devicesOp = DeviceInformation::FindAllAsync(MidiInPort::GetDeviceSelector());
        DeviceInformationCollection devices = devicesOp.get();
        for (uint32_t i = 0; i < devices.Size(); ++i) 
            deviceConnected ( devices.GetAt(i), inputs );

        AVOID_STA_END;
    }

    // Enum outputs
    if ( ok )
    {
        AVOID_STA_BEGIN;

        init_apartment();
        IAsyncOperation<DeviceInformationCollection> devicesOp = DeviceInformation::FindAllAsync(MidiOutPort::GetDeviceSelector());
        DeviceInformationCollection devices = devicesOp.get();
        for (uint32_t i = 0; i < devices.Size(); ++i) 
            deviceConnected ( devices.GetAt(i), outputs );

        AVOID_STA_END;
    }

    // Init inputs device watcher
    if ( ok )
    {
        inputsWatcher = DeviceInformation::CreateWatcher(MidiInPort::GetDeviceSelector());
        inputsWatcher.Added  ([](DeviceWatcher const&, DeviceInformation       const& info) { deviceConnected    ( info, inputs ); } );
        inputsWatcher.Removed([](DeviceWatcher const&, DeviceInformationUpdate const& info) { deviceDisconnected ( info, inputs ); } );
        inputsWatcher.Start();
    }

    // Init outputs device watcher
    if ( ok )
    {
        outputsWatcher = DeviceInformation::CreateWatcher(MidiOutPort::GetDeviceSelector());
        outputsWatcher.Added  ([](DeviceWatcher const&, DeviceInformation       const& info) { deviceConnected    ( info, outputs ); } );
        outputsWatcher.Removed([](DeviceWatcher const&, DeviceInformationUpdate const& info) { deviceDisconnected ( info, outputs ); } );
        outputsWatcher.Start();
    }

    // Done if errors or set as initialized if ok
    if (!ok)
        EasyMidiLib_done();
    else
    {
        initialized=true;
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
    // Stop inputs watcher
    if ( inputsWatcher )
    {
        inputsWatcher.Stop();
        inputsWatcher = 0;
    }

    // Stop outputs watcher
    if ( outputsWatcher )
    {
        outputsWatcher.Stop();
        outputsWatcher = 0;
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
    initialized  =false;
    mainListener = 0;
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


    // Open
    if ( ok )
    {
        AVOID_STA_BEGIN;

        device->inPortOp = MidiInPort::FromIdAsync(device->device.Id());
        if (device->inPortOp.wait_for(std::chrono::seconds(5)) == winrt::Windows::Foundation::AsyncStatus::Completed) 
        {
            device->inPort = device->inPortOp.GetResults();
        } 
            else 
        {
            device->inPortOp.Cancel();
            device->inPortOp = nullptr;
            ok = false;
            setLastErrorf ( "Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str() );        
        }

        AVOID_STA_END;
    }

    // Open
    if ( ok )
    {
        device->userDev.userPtrParam = userPtrParam;
        device->userDev.userIntParam = userIntParam;
        device->userDev.opened       = true        ;

        if ( mainListener )
            mainListener->deviceOpen(dev);

        device->inPort.MessageReceived
        (
            [&,device](IMidiInPort const&, MidiMessageReceivedEventArgs const& args) 
            {
                if ( mainListener )
                {
                    std::lock_guard<std::mutex> lock(devicesMutex);

                    IBuffer raw = args.Message().RawData();
                    size_t incommingDataLen = raw.Length();
                    DataReader reader = DataReader::FromBuffer(raw);

                    size_t prevSize = device->inputQueue.size();
                    device->inputQueue.resize(prevSize+incommingDataLen);

                    reader.ReadBytes(winrt::array_view<uint8_t>(device->inputQueue.data()+prevSize, device->inputQueue.data()+prevSize+incommingDataLen));

                    size_t consumedBytes = mainListener->deviceInData(&device->userDev, device->inputQueue.data(), device->inputQueue.size());
                    if ( consumedBytes>0 )
                    {
                        if ( consumedBytes > device->inputQueue.size() )
                            consumedBytes = device->inputQueue.size();

                        device->inputQueue.erase( device->inputQueue.begin(), device->inputQueue.begin()+consumedBytes );
                    }
                }
            }
        );
    }

    // Close if errors
    if ( !ok )
    {
        EasyMidiLib_inputClose ( dev );
        ok = false;
        setLastErrorf ( "Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_inputClose ( const EasyMidiLibDevice* dev )
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->userDev.opened ;

    if (device->inPort)
        device->inPort.Close();
  
    device->inPortOp = nullptr;
    device->inPort   = nullptr;    

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
        setLastErrorf("EasyMidiLib_inputOpen index %zu out of range (enum elements %zu)", enumIndex, userOutputsEnumeration.size() );
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

    // Open
    if ( ok )
    {
        AVOID_STA_BEGIN;

        device->outPortOp = MidiOutPort::FromIdAsync(device->device.Id());
        device->outPort   = device->outPortOp.get();
        if ( device->outPort )
        {
            device->userDev.opened       = true        ;
            device->userDev.userPtrParam = userPtrParam;
            device->userDev.userIntParam = userIntParam;

            if ( mainListener )
                mainListener->deviceOpen(dev);
        }

        AVOID_STA_END;
    }

    // Close if errors
    if ( !ok )
    {
        EasyMidiLib_outputClose ( dev );
        ok = false;
        setLastErrorf ( "Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_outputClose ( const EasyMidiLibDevice* dev )
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->userDev.opened;

    if (device->outPort)
        device->outPort.Close();
  
    device->outPortOp = nullptr;
    device->outPort   = nullptr;

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
        setLastErrorf ( "EasyMidiLib_outputOpen: can't send using input midi device:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    // Check already opened
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if ( ok )
    {
        if ( !device->userDev.opened )
        {
            ok = false;
            setLastErrorf ( "EasyMidiLib_outputOpen: closed device:%s(%s)", dev->name.c_str(), dev->id.c_str() );
        }
    }

    if ( ok ) 
    {
        if ( mainListener )
            mainListener->deviceInData(&device->userDev, data, size );

        DataWriter writer;
        writer.WriteBytes(winrt::array_view<uint8_t const>(data, data + size));
        IBuffer raw = writer.DetachBuffer();
        device->outPort.SendBuffer(raw);
    }

    return ok;

}

//--------------------------------------------------------------------------------------------------------------------------

#endif //_WIN32
