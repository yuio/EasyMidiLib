#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <windows.h>
#include <conio.h>
#include <iostream>
#include <fstream>
#include <string>

using namespace winrt;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Midi;
using namespace Windows::Storage::Streams;

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

void enumMidiDevices( bool inputs, DeviceInformationCollection& devices, bool dumpInfo=true ) 
{    
    if (dumpInfo) std::wcout << L"\n";
    if (dumpInfo) std::wcout << L"-----Midi "<<(inputs?"input":"outputs")<<" devices-----\n";

    Windows::Foundation::IAsyncOperation<DeviceInformationCollection> devicesOp = DeviceInformation::FindAllAsync(inputs?MidiInPort::GetDeviceSelector():MidiOutPort::GetDeviceSelector());
    devices = devicesOp.get();
    uint32_t deviceCount = devices.Size();
    if (deviceCount == 0) 
    {
        if (dumpInfo) std::wcout << L"No MIDI devices found.\n";
    } 
    else 
    {
        for (uint32_t i = 0; i < deviceCount; ++i) 
        {
            DeviceInformation device = devices.GetAt(i);
            if (dumpInfo) std::wcout << L"    [" << i << L"] '" << device.Name().c_str() << L"'" << std::endl;
        }
    }
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
