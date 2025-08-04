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
#include <mutex>

using namespace winrt;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Midi;
using namespace Windows::Storage::Streams;

//--------------------------------------------------------------------------------------------------------------------------

static bool                 initialized       = false;
static std::string          lastError         = "";
static EasyMidiLibListener* mainListener      = 0;

//--------------------------------------------------------------------------------------------------------------------------

static DeviceWatcher    inputsWatcher  = nullptr;
static DeviceWatcher    outputsWatcher = nullptr;

struct MidiDeviceInfo 
{
    EasyMidiLibDevice   info      = {};
    bool                connected = false;
    DeviceInformation   device    = 0;

    Windows::Devices::Midi::IMidiOutPort                                        outPort   = nullptr;
    Windows::Foundation::IAsyncOperation<Windows::Devices::Midi::IMidiOutPort>  outPortOp = nullptr;
    Windows::Devices::Midi::MidiInPort                                          inPort    = nullptr;
    Windows::Foundation::IAsyncOperation<Windows::Devices::Midi::MidiInPort>    inPortOp  = nullptr;
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
        if ( !d.connected )
        {
            if ( mainListener )
                mainListener->deviceReconnected ( &d.info );
        }
        else
        {
        }

    }
    else
    {
        MidiDeviceInfo d;
        d.info.isInput         = (&devices==&inputs);
        d.info.name            = name;
        d.info.id              = id;
        d.info.opened          = false;
        d.info.internalHandler = 0;
        d.device               = info;
        d.connected            = true;

        devices[d.info.id] = d;
        devices[d.info.id].info.internalHandler = &devices[d.info.id];

        if ( mainListener )
            mainListener->deviceConnected ( &d.info );

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
        d.connected=false;
        
        if ( mainListener )
            mainListener->deviceDisconnected ( &d.info );
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
        if ( it.second.connected )
            dst.push_back(&it.second.info);
}

//--------------------------------------------------------------------------------------------------------------------------

static std::vector<const EasyMidiLibDevice*> userInputsEnumeration;

void EasyMidiLib_updateInputsEnumeration()
{
    devicesEnumeration ( inputs, userInputsEnumeration );
}

const std::vector<const EasyMidiLibDevice*>& EasyMidiLib_getInputDevices()
{
    return userInputsEnumeration;
}

//--------------------------------------------------------------------------------------------------------------------------

static std::vector<const EasyMidiLibDevice*> userOutputsEnumeration;

void EasyMidiLib_updateOutputsEnumeration()
{
    devicesEnumeration ( outputs, userOutputsEnumeration );
}

const std::vector<const EasyMidiLibDevice*>& EasyMidiLib_getOutputDevices()
{
    return userOutputsEnumeration;
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

    // Init apartament
    if ( ok )
    {
        try 
        {
            init_apartment();
        }
        catch (...)
        {
            setLastErrorf("Failed to initialize WinRT apartment");
            ok = false;
        }
    }

    // Enum inputs
    {
        Windows::Foundation::IAsyncOperation<DeviceInformationCollection> devicesOp = DeviceInformation::FindAllAsync(MidiInPort::GetDeviceSelector());
        DeviceInformationCollection devices = devicesOp.get();
        for (uint32_t i = 0; i < devices.Size(); ++i) 
            deviceConnected ( devices.GetAt(i), inputs );
    }

    // Enum outputs
    {
        Windows::Foundation::IAsyncOperation<DeviceInformationCollection> devicesOp = DeviceInformation::FindAllAsync(MidiOutPort::GetDeviceSelector());
        DeviceInformationCollection devices = devicesOp.get();
        for (uint32_t i = 0; i < devices.Size(); ++i) 
            deviceConnected ( devices.GetAt(i), outputs );
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

void EasyMidiLib_update ( )
{
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_done()
{
    if ( outputsWatcher )
    {
        outputsWatcher.Stop();
        outputsWatcher = 0;
    }

    if ( inputsWatcher )
    {
        inputsWatcher.Stop();
        inputsWatcher = 0;
    }

    if ( initialized && mainListener )
        mainListener->libDone();

    inputs .clear();
    outputs.clear();

    userInputsEnumeration .clear();
    userOutputsEnumeration.clear();

    initialized       =false;
    mainListener      = 0;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen ( size_t enumIndex, EasyMidiLibInputListener* inListener )
{
    bool ok = true;

    if ( enumIndex<userInputsEnumeration.size() )
        return EasyMidiLib_inputOpen ( userInputsEnumeration[enumIndex], inListener );
    else
        setLastErrorf("EasyMidiLib_inputOpen index %zu out of range (enum elements %zu)", enumIndex, userInputsEnumeration.size() );

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen ( const EasyMidiLibDevice* dev, EasyMidiLibInputListener* inListener )
{
    bool ok = true;

    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if ( !device->info.opened )
    {
        bool ok = true;

        device->inPortOp = MidiInPort::FromIdAsync(device->device.Id());
        device->inPort   = device->inPortOp.get();
        if ( device->inPort )
        {
            device->inPort.MessageReceived([&](IMidiInPort const&, MidiMessageReceivedEventArgs const& args) 
            {
                IBuffer raw = args.Message().RawData();
                DataReader reader = DataReader::FromBuffer(raw);
                while (reader.UnconsumedBufferLength() > 0) 
                {
                    uint8_t b = reader.ReadByte();
                    std::cout << (b<=0xf?"0":"") << std::hex << static_cast<int>(b) << " ";
                    std::cout << "\n";

                    //static size_t line_output_count = 0;
                    //line_output_count++;
                    //if (line_output_count>=36)
                    //{
                    //    std::wcout << L"\n";
                    //    line_output_count=0;
                    //}
                }
            });

            device->info.opened = true;

            if ( mainListener )
                mainListener->deviceOpen(dev);
        }
        else
        {
            EasyMidiLib_inputClose ( dev );
            ok = false;
            setLastErrorf ( "Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
        }
    }
    else
    {
        ok = false;
        setLastErrorf ( "Already open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_inputClose ( const EasyMidiLibDevice* dev )
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->info.opened ;

    if (device->inPort)
        device->inPort.Close();
  
    device->inPortOp = nullptr;
    device->inPort   = nullptr;    

    device->info.opened = false;

    if ( wasOpened && mainListener )
        mainListener->deviceClose(dev);
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen ( size_t enumIndex )
{
    if ( enumIndex<userOutputsEnumeration.size() )
        return EasyMidiLib_outputOpen ( userOutputsEnumeration[enumIndex] );
    else
        setLastErrorf("EasyMidiLib_inputOpen index %zu out of range (enum elements %zu)", enumIndex, userOutputsEnumeration.size() );
}

//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen ( const EasyMidiLibDevice* dev )
{
    bool ok = true;

    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    if ( !device->info.opened )
    {
        bool ok = true;

        device->outPortOp = MidiOutPort::FromIdAsync(device->device.Id());
        device->outPort   = device->outPortOp.get();
        if ( device->outPort )
        {
            device->info.opened = true;

            if ( mainListener )
                mainListener->deviceOpen(dev);
        }
        else
        {
            EasyMidiLib_outputClose ( dev );
            ok = false;
            setLastErrorf ( "Unable to open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
        }
    }
    else
    {
        ok = false;
        setLastErrorf ( "Already open:%s(%s)", dev->name.c_str(), dev->id.c_str() );
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_outputClose ( const EasyMidiLibDevice* dev )
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->info.opened;

    if (device->outPort)
        device->outPort.Close();
  
    device->outPortOp = nullptr;
    device->outPort   = nullptr;

    device->info.opened = false;

    if ( wasOpened && mainListener )
        mainListener->deviceClose(dev);
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_outputSend ( const EasyMidiLibDevice* dev )
{
    MidiDeviceInfo* device = (MidiDeviceInfo*)dev->internalHandler;
    bool wasOpened = device->info.opened;

    if (device->outPort)
        device->outPort.Close();
  
    device->outPortOp = nullptr;
    device->outPort   = nullptr;

    device->info.opened = false;

    if ( wasOpened && mainListener )
        mainListener->deviceClose(dev);
}


/*
                            IBuffer raw = args.Message().RawData();
                            if (outputValid && outPort)
                                outPort.SendBuffer(raw);
*/


//--------------------------------------------------------------------------------------------------------------------------
