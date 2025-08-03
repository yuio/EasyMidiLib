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
static size_t               debugVerboseLevel = 0;
static std::string          lastError         = "";
static EasyMidiLibListener* mainListener      = 0;

//--------------------------------------------------------------------------------------------------------------------------

static DeviceWatcher    inputsWatcher  = nullptr;
static DeviceWatcher    outputsWatcher = nullptr;

struct MidiDeviceInfo 
{
    EasyMidiLibDevice   info      = {};
    DeviceInformation   device    = 0;
    bool                connected = false;
};

static std::mutex                           devicesMutex;
static std::map<std::string,MidiDeviceInfo> inputs ;
static std::map<std::string,MidiDeviceInfo> outputs;

//--------------------------------------------------------------------------------------------------------------------------

static void deviceConnected ( DeviceInformation const& info, std::map<std::string,MidiDeviceInfo>& devices )
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    const char* deviceType = (&devices==&inputs) ? "input" : "output";

    MidiDeviceInfo d;
    d.info.isInput         = (&devices==&inputs);
    d.info.name            = winrt::to_string(info.Name());
    d.info.id              = winrt::to_string(info.Id  ());
    d.info.open            = false;
    d.info.internalHandler = 0;
    d.device               = info;
    d.connected            = true;

    devices[d.info.id] = d;
    devices[d.info.id].info.internalHandler = &devices[d.info.id];
    
    if ( mainListener )
        mainListener->deviceConnected ( &d.info );

    if ( debugVerboseLevel>0 )
        printf ( "EasyMidiLib: MIDI %s connected %s (id:%s)\n", deviceType, d.info.name.c_str(), d.info.id.c_str() );

}

//--------------------------------------------------------------------------------------------------------------------------

static void deviceDisconnected ( const DeviceInformationUpdate& info, std::map<std::string,MidiDeviceInfo>& devices )
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    const char* deviceType = (&devices==&inputs) ? "input" : "output";

    std::string id = winrt::to_string(info.Id());
    if ( debugVerboseLevel>0 )
        printf ( "EasyMidiLib: MIDI %s disconnected (id:%s)\n", deviceType, id.c_str() );

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

void EasyMidiLib_setLastErrorf ( const char* textf, ... )
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

bool EasyMidiLib_init( EasyMidiLibListener* listener, size_t verboseLevel )
{
    if (initialized) return true;

    bool ok = true;

    // Set verbose level
    debugVerboseLevel = verboseLevel;
    mainListener      = listener;

    // Init apartament
    if ( ok )
    {
        try 
        {
            init_apartment();
        }
        catch (...)
        {
            EasyMidiLib_setLastErrorf("Failed to initialize WinRT apartment");
            ok = false;
        }
    }

    // Init inputs device watcher
    if ( ok )
    {
        inputsWatcher = DeviceInformation::CreateWatcher(MidiInPort::GetDeviceSelector());
        inputsWatcher.Added([](DeviceWatcher const&, DeviceInformation const& info)
            { deviceConnected ( info, inputs ); } );
        inputsWatcher.Removed([](DeviceWatcher const&, DeviceInformationUpdate const& info) 
            { deviceDisconnected ( info, inputs ); } );
        inputsWatcher.Start();
    }

    // Init outputs device watcher
    if ( ok )
    {
        outputsWatcher = DeviceInformation::CreateWatcher(MidiOutPort::GetDeviceSelector());
        outputsWatcher.Added([](DeviceWatcher const&, DeviceInformation const& info)
            { deviceConnected ( info, outputs ); } );
        outputsWatcher.Removed([](DeviceWatcher const&, DeviceInformationUpdate const& info) 
            { deviceDisconnected ( info, outputs ); } );
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

    initialized       =false;
    debugVerboseLevel = 0;
    mainListener      = 0;
}

//--------------------------------------------------------------------------------------------------------------------------








































//--------------------------------------------------------------------------------------------------------------------------

void enumMidiDevices( bool inputs, DeviceInformationCollection& devices ) 
{    
    Windows::Foundation::IAsyncOperation<DeviceInformationCollection> devicesOp = DeviceInformation::FindAllAsync(inputs?MidiInPort::GetDeviceSelector():MidiOutPort::GetDeviceSelector());
    devices = devicesOp.get();
    uint32_t deviceCount = devices.Size();
    for (uint32_t i = 0; i < deviceCount; ++i) 
    {
        DeviceInformation device = devices.GetAt(i);
    }
}



//--------------------------------------------------------------------------------------------------------------------------

int getIndexFromString(const std::wstring& str) 
{
    bool all_digits = std::all_of(str.begin(), str.end(), 
                                  [](wchar_t c) 
                                    { 
                                        return std::isdigit(c); 
                                    });
    return (all_digits&&str.size()) ? std::stoi(str) : -1;
}

//--------------------------------------------------------------------------------------------------------------------------

int getDeviceIndexFromString(DeviceInformationCollection& devices, const std::wstring& substring )
{
    uint32_t deviceCount = devices.Size();
    for (uint32_t i = 0; i < deviceCount; ++i) 
    {
        DeviceInformation device = devices.GetAt(i);
        std::wstring deviceName = device.Name().c_str();
        if ( deviceName.find(substring)!=std::string::npos) 
            return i;
    }

    return -1;
}

//--------------------------------------------------------------------------------------------------------------------------

void trim_spaces (std::wstring& str)
{
    size_t start = 0;
    while (start < str.size() && std::isspace(str[start])) 
        ++start;

    if (start == str.size()) {
        str.clear();
        return;
    }

    size_t end = str.size() - 1;
    while (end > start && std::isspace(str[end]))
        --end;

    str = str.substr(start, end - start + 1);
}

//--------------------------------------------------------------------------------------------------------------------------

bool loadArgsFile ( const std::wstring& argsFileName, std::wstring& inputName, std::wstring& outputName ) 
{    
    std::wifstream file(argsFileName);
    if (!file.is_open()) 
    {
        std::wcerr << L"ERROR: Unable to open file: " << argsFileName << std::endl;
        return false;
    }

    std::getline(file, inputName );
    std::getline(file, outputName);
    file.close();

    if (inputName.empty() || outputName.empty()) {
        return false;
    }

    // Trim spaces
    trim_spaces(inputName);
    trim_spaces(outputName);

    return true;
}

//--------------------------------------------------------------------------------------------------------------------------

int EasyMidiRouterMain(int argc, char* argv[])
{
    std::wcout.setf(std::ios::unitbuf);

    init_apartment();

    // Enumerate MIDI devices
    DeviceInformationCollection inputs = nullptr;
    DeviceInformationCollection outputs = nullptr;
    enumMidiDevices( true, inputs  );
    enumMidiDevices( false, outputs );
    std::wcout << L"\n";

    // Initialization and loop
    {
        // Main variables
        std::atomic<bool> outputValid{ false };
        DeviceInformation output = nullptr;
        uint64_t outputConnectionTryTime = 0;
        Windows::Devices::Midi::IMidiOutPort outPort = nullptr;        
        Windows::Foundation::IAsyncOperation<Windows::Devices::Midi::IMidiOutPort> outPortOp = nullptr;

        std::atomic<bool> inputValid { false };
        DeviceInformation input  = nullptr;
        uint64_t inputConnectionTryTime = 0;
        Windows::Devices::Midi::MidiInPort inPort = nullptr;
        Windows::Foundation::IAsyncOperation<Windows::Devices::Midi::MidiInPort>inPortOp = nullptr;

        // Prepare output watcher
        DeviceWatcher outputWatcher = DeviceInformation::CreateWatcher(MidiOutPort::GetDeviceSelector());
        outputWatcher.Removed([&](DeviceWatcher const&, DeviceInformationUpdate const& info) 
        {
            if (outputValid)
            {
                if (info.Id() == output.Id())
                {
                    outputValid = false;
                    std::wcout << L"\nMIDI OUTPUT disconnected.\n";
                }
            }
        });
        outputWatcher.Start();

        // Prepare input watcher
        DeviceWatcher inputWatcher = DeviceInformation::CreateWatcher(MidiInPort::GetDeviceSelector());
        inputWatcher.Removed([&](DeviceWatcher const&, DeviceInformationUpdate const& info) {
            if (inputValid)
            {
                if (info.Id() == input.Id()) 
                {
                    inputValid = false;
                    std::wcout << L"\nMIDI INPUT disconnected.\n";
                }
            }
        });

        inputWatcher.Start();

        // Display initialization message
        std::wcout << "\n";
        std::wcout << L"Initializing...";
        std::wcout << L"(Press CTRL+Q to exit)\n";

        // Main Loop
        while (true) 
        {
            const int reconnectionInterval = 1000;
            bool reconnecting = (!outputValid || !inputValid );
    
            /*
            // reconnect output
            if ( !outputValid && (GetTickCount64()-outputConnectionTryTime)>reconnectionInterval )
            {
                outputConnectionTryTime = GetTickCount64(); 
                enumMidiDevices( false, outputs, false );
                int outputIndex = getDeviceIndexFromString(outputs, outputName);
                if (outputIndex>=0)
                {
                    output=outputs.GetAt(outputIndex);
                    outPortOp = MidiOutPort::FromIdAsync(output.Id());
                    outPort = outPortOp.get();
                    if (!outPort)
                    {   
                        std::wcout << L"INFO: Failed to open output device '"<<outputName<<L"'.\n";
                    }
                    else
                    {
                        std::wcout << L"INFO: Output device opened successfully. ["<<outputIndex<<"] '"<<outputName<<L"'.\n";
                        outputValid = true;
                    }
                }
                else 
                {
                    std::wcout << L"INFO: Output device '"<<outputName<<L"' not found.\n";
                    args_error=true;
                }
            }   

            // reconnect input
            if ( !inputValid && (GetTickCount64()-inputConnectionTryTime)>reconnectionInterval )
            {
                inputConnectionTryTime = GetTickCount64(); 
                enumMidiDevices( true, inputs, false );
                int inputIndex = getDeviceIndexFromString(inputs, inputName);
                if (inputIndex>=0)
                {
                    input=inputs.GetAt(inputIndex);
                    inPortOp = MidiInPort::FromIdAsync(input.Id());
                    inPort = inPortOp.get();
                    if (!inPort)
                    {
                        std::wcout << L"INFO: Failed to open input device '"<<inputName<<L"'.\n";
                    }
                    else
                    {
                        std::wcout << L"INFO: Input device opened successfully. ["<<inputIndex<<"] '"<<inputName<<L"'.\n";
                        inputValid = true;
                        inPort.MessageReceived([&](IMidiInPort const&, MidiMessageReceivedEventArgs const& args) 
                        {
                            IBuffer raw = args.Message().RawData();
                            if (outputValid && outPort)
                                outPort.SendBuffer(raw);
                
                            DataReader reader = DataReader::FromBuffer(raw);
                            while (reader.UnconsumedBufferLength() > 0) 
                            {
                                uint8_t b = reader.ReadByte();
                                std::wcout << (b<=0xf?L"0":L"") << std::hex << static_cast<int>(b) << L" ";

                                static size_t line_output_count = 0;
                                line_output_count++;
                                if (line_output_count>=36)
                                {
                                    std::wcout << L"\n";
                                    line_output_count=0;
                                }
                            }
                        });
                    }
                }
                else 
                {
                    std::wcout << L"INFO: Input device '"<<inputName<<L"' not found.\n";
                    args_error=true;
                }
            }   
            */

            // Reconection check
            if ( reconnecting && outputValid && inputValid )
            {
                std::wcout << L"\n";
                std::wcout << L"Routing messages from '"<<input.Name().c_str()<<"' to '"<<output.Name().c_str()<<"'";
                std::wcout << L"(Press CTRL+Q to exit)\n";
            }

            // Exit key check
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 17) {
                    break;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    std::wcout << L"Exiting...\n";

    return 0;
}

//--------------------------------------------------------------------------------------------------------------------------
