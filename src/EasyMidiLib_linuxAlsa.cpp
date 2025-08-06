#if defined(__linux__)

#include "EasyMidiLib.h"
#include <alsa/asoundlib.h>

#include <stdarg.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

//--------------------------------------------------------------------------------------------------------------------------

EasyMidiLibTestListener EasyMidiLib_testListener;

//--------------------------------------------------------------------------------------------------------------------------

static bool                 initialized       = false;
static std::string          lastError         = "";
static EasyMidiLibListener* mainListener      = 0;

//--------------------------------------------------------------------------------------------------------------------------

static snd_seq_t*           sequencer         = nullptr;
static int                  clientId          = -1;
static int                  inputPortId       = -1;
static int                  outputPortId      = -1;
static std::atomic<bool>    pollThreadRunning{false};
static std::thread          pollThread;

struct MidiDeviceInfo 
{
    EasyMidiLibDevice   userDev         = {};
    bool                connected       = true;
    snd_seq_addr_t      address         = {};
    bool                subscribed      = false;
    
    std::vector<uint8_t> inputQueue;
};

static std::mutex                           devicesMutex;
static std::map<std::string,MidiDeviceInfo> inputs ;
static std::map<std::string,MidiDeviceInfo> outputs;

//--------------------------------------------------------------------------------------------------------------------------

static std::string GetPortName(int client, int port) 
{
    snd_seq_client_info_t *clientInfo;
    snd_seq_port_info_t *portInfo;
    
    snd_seq_client_info_alloca(&clientInfo);
    snd_seq_port_info_alloca(&portInfo);
    
    if (snd_seq_get_any_client_info(sequencer, client, clientInfo) == 0 &&
        snd_seq_get_any_port_info(sequencer, client, port, portInfo) == 0) {
        
        const char* clientName = snd_seq_client_info_get_name(clientInfo);
        const char* portName = snd_seq_port_info_get_name(portInfo);
        
        return std::string(clientName) + " " + std::string(portName);
    }
    return "Unknown Device";
}

//--------------------------------------------------------------------------------------------------------------------------

static std::string GetPortID(int client, int port) 
{
    return std::to_string(client) + ":" + std::to_string(port);
}

//--------------------------------------------------------------------------------------------------------------------------

static void ProcessMidiEvent(snd_seq_event_t *ev)
{
    if (!mainListener) return;
    
    std::string portId = GetPortID(ev->source.client, ev->source.port);
    
    std::lock_guard<std::mutex> lock(devicesMutex);
    auto it = inputs.find(portId);
    if (it == inputs.end()) return;
    
    MidiDeviceInfo* device = &it->second;
    if (!device->userDev.opened) return;
    
    // Convert ALSA event to raw MIDI data
    unsigned char midiData[4];
    int dataSize = 0;
    
    switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON:
            midiData[0] = 0x90 | ev->data.note.channel;
            midiData[1] = ev->data.note.note;
            midiData[2] = ev->data.note.velocity;
            dataSize = 3;
            break;
        case SND_SEQ_EVENT_NOTEOFF:
            midiData[0] = 0x80 | ev->data.note.channel;
            midiData[1] = ev->data.note.note;
            midiData[2] = ev->data.note.velocity;
            dataSize = 3;
            break;
        case SND_SEQ_EVENT_CONTROLLER:
            midiData[0] = 0xB0 | ev->data.control.channel;
            midiData[1] = ev->data.control.param;
            midiData[2] = ev->data.control.value;
            dataSize = 3;
            break;
        case SND_SEQ_EVENT_PGMCHANGE:
            midiData[0] = 0xC0 | ev->data.control.channel;
            midiData[1] = ev->data.control.value;
            dataSize = 2;
            break;
        case SND_SEQ_EVENT_PITCHBEND:
            midiData[0] = 0xE0 | ev->data.control.channel;
            midiData[1] = ev->data.control.value & 0x7F;
            midiData[2] = (ev->data.control.value >> 7) & 0x7F;
            dataSize = 3;
            break;
        case SND_SEQ_EVENT_SYSEX:
            if (ev->data.ext.len > 0 && ev->data.ext.len <= sizeof(midiData)) {
                memcpy(midiData, ev->data.ext.ptr, ev->data.ext.len);
                dataSize = ev->data.ext.len;
            }
            break;
    }
    
    if (dataSize > 0) {
        // Add data to input queue
        size_t prevSize = device->inputQueue.size();
        device->inputQueue.resize(prevSize + dataSize);
        memcpy(device->inputQueue.data() + prevSize, midiData, dataSize);
        
        // Process accumulated data
        size_t consumedBytes = mainListener->deviceInData(&device->userDev, device->inputQueue.data(), device->inputQueue.size());
        if (consumedBytes > 0) {
            if (consumedBytes > device->inputQueue.size())
                consumedBytes = device->inputQueue.size();
            
            device->inputQueue.erase(device->inputQueue.begin(), device->inputQueue.begin() + consumedBytes);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void PollThreadFunction()
{
    int npfd = snd_seq_poll_descriptors_count(sequencer, POLLIN);
    struct pollfd* pfd = (struct pollfd*)alloca(npfd * sizeof(struct pollfd));
    snd_seq_poll_descriptors(sequencer, pfd, npfd, POLLIN);
    
    while (pollThreadRunning) {
        if (poll(pfd, npfd, 100) > 0) {
            snd_seq_event_t *ev;
            while (snd_seq_event_input(sequencer, &ev) >= 0) {
                switch (ev->type) {
                    case SND_SEQ_EVENT_NOTEON:
                    case SND_SEQ_EVENT_NOTEOFF:
                    case SND_SEQ_EVENT_CONTROLLER:
                    case SND_SEQ_EVENT_PGMCHANGE:
                    case SND_SEQ_EVENT_PITCHBEND:
                    case SND_SEQ_EVENT_SYSEX:
                        ProcessMidiEvent(ev);
                        break;
                    case SND_SEQ_EVENT_PORT_START:
                    case SND_SEQ_EVENT_PORT_EXIT:
                        // Handle port connect/disconnect if needed
                        break;
                }
                snd_seq_free_event(ev);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------

static void EnumerateDevices() 
{
    snd_seq_client_info_t *clientInfo;
    snd_seq_port_info_t *portInfo;
    
    snd_seq_client_info_alloca(&clientInfo);
    snd_seq_port_info_alloca(&portInfo);
    
    snd_seq_client_info_set_client(clientInfo, -1);
    
    while (snd_seq_query_next_client(sequencer, clientInfo) >= 0) {
        int client = snd_seq_client_info_get_client(clientInfo);
        if (client == clientId) continue; // Skip our own client
        
        snd_seq_port_info_set_client(portInfo, client);
        snd_seq_port_info_set_port(portInfo, -1);
        
        while (snd_seq_query_next_port(sequencer, portInfo) >= 0) {
            int port = snd_seq_port_info_get_port(portInfo);
            unsigned int caps = snd_seq_port_info_get_capability(portInfo);
            
            std::string id = GetPortID(client, port);
            std::string name = GetPortName(client, port);
            
            // Check if it's an input source (can read from)
            if ((caps & SND_SEQ_PORT_CAP_READ) && (caps & SND_SEQ_PORT_CAP_SUBS_READ)) {
                MidiDeviceInfo d;
                d.userDev.isInput         = true;
                d.userDev.name            = name;
                d.userDev.id              = id;
                d.userDev.opened          = false;
                d.userDev.userPtrParam    = 0;
                d.userDev.userIntParam    = 0;
                d.userDev.internalHandler = 0;
                d.address.client          = client;
                d.address.port            = port;
                d.connected               = true;
                d.subscribed              = false;
                d.inputQueue.reserve(10240);
                
                inputs[id] = d;
                inputs[id].userDev.internalHandler = &inputs[id];
                
                if (mainListener)
                    mainListener->deviceConnected(&inputs[id].userDev);
            }
            
            // Check if it's an output destination (can write to)
            if ((caps & SND_SEQ_PORT_CAP_WRITE) && (caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) {
                MidiDeviceInfo d;
                d.userDev.isInput         = false;
                d.userDev.name            = name;
                d.userDev.id              = id;
                d.userDev.opened          = false;
                d.userDev.userPtrParam    = 0;
                d.userDev.userIntParam    = 0;
                d.userDev.internalHandler = 0;
                d.address.client          = client;
                d.address.port            = port;
                d.connected               = true;
                d.subscribed              = false;
                
                outputs[id] = d;
                outputs[id].userDev.internalHandler = &outputs[id];
                
                if (mainListener)
                    mainListener->deviceConnected(&outputs[id].userDev);
            }
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

    // Open ALSA sequencer
    if (ok) {
        int err = snd_seq_open(&sequencer, "default", SND_SEQ_OPEN_DUPLEX, 0);
        if (err < 0) {
            setLastErrorf("Failed to open ALSA sequencer: %s", snd_strerror(err));
            ok = false;
        }
    }

    // Set client name
    if (ok) {
        snd_seq_set_client_name(sequencer, "EasyMidiLib");
        clientId = snd_seq_client_id(sequencer);
    }

    // Create input port
    if (ok) {
        inputPortId = snd_seq_create_simple_port(sequencer, "EasyMidiLib Input",
                                                 SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                                 SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (inputPortId < 0) {
            setLastErrorf("Failed to create input port: %s", snd_strerror(inputPortId));
            ok = false;
        }
    }

    // Create output port
    if (ok) {
        outputPortId = snd_seq_create_simple_port(sequencer, "EasyMidiLib Output",
                                                  SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                                  SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (outputPortId < 0) {
            setLastErrorf("Failed to create output port: %s", snd_strerror(outputPortId));
            ok = false;
        }
    }

    // Start polling thread
    if (ok) {
        pollThreadRunning = true;
        pollThread = std::thread(PollThreadFunction);
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
    // ALSA handles events automatically via polling thread, no manual polling needed
}

//--------------------------------------------------------------------------------------------------------------------------

void EasyMidiLib_done()
{
    // Stop polling thread
    if (pollThreadRunning) {
        pollThreadRunning = false;
        if (pollThread.joinable()) {
            pollThread.join();
        }
    }

    // Close sequencer
    if (sequencer) {
        snd_seq_close(sequencer);
        sequencer = nullptr;
    }

    if (initialized && mainListener)
        mainListener->libDone();

    inputs .clear();
    outputs.clear();

    userInputsEnumeration .clear();
    userOutputsEnumeration.clear();

    initialized   = false;
    mainListener  = 0;
    clientId      = -1;
    inputPortId   = -1;
    outputPortId  = -1;
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

    // Subscribe to the source
    if ( ok )
    {
        int err = snd_seq_connect_from(sequencer, inputPortId, device->address.client, device->address.port);
        if (err < 0) {
            setLastErrorf("Failed to connect to MIDI source:%s(%s) error:%s", dev->name.c_str(), dev->id.c_str(), snd_strerror(err));
            ok = false;
        } else {
            device->subscribed           = true;
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

    if (device->subscribed) {
        snd_seq_disconnect_from(sequencer, inputPortId, device->address.client, device->address.port);
        device->subscribed = false;
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

    // Subscribe to the destination
    if ( ok )
    {
        int err = snd_seq_connect_to(sequencer, outputPortId, device->address.client, device->address.port);
        if (err < 0) {
            setLastErrorf("Failed to connect to MIDI destination:%s(%s) error:%s", dev->name.c_str(), dev->id.c_str(), snd_strerror(err));
            ok = false;
        } else {
            device->subscribed           = true;
            device->userDev.opened       = true;
            device->userDev.userPtrParam = userPtrParam;
            device->userDev.userIntParam = userIntParam;

            if (mainListener)
                mainListener->deviceOpen(dev);
        }
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

    if (device->subscribed) {
        snd_seq_disconnect_to(sequencer, outputPortId, device->address.client, device->address.port);
        device->subscribed = false;
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

        // Send raw MIDI data as SysEx event (most generic way)
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        
        snd_seq_ev_set_source(&ev, outputPortId);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        
        if (size >= 1) {
            // Try to parse common MIDI messages for better compatibility
            uint8_t status = data[0];
            uint8_t channel = status & 0x0F;
            
            switch (status & 0xF0) {
                case 0x80: // Note Off
                    if (size >= 3) {
                        snd_seq_ev_set_noteoff(&ev, channel, data[1], data[2]);
                        break;
                    }
                    goto send_sysex;
                    
                case 0x90: // Note On
                    if (size >= 3) {
                        snd_seq_ev_set_noteon(&ev, channel, data[1], data[2]);
                        break;
                    }
                    goto send_sysex;
                    
                case 0xB0: // Control Change
                    if (size >= 3) {
                        snd_seq_ev_set_controller(&ev, channel, data[1], data[2]);
                        break;
                    }
                    goto send_sysex;
                    
                case 0xC0: // Program Change
                    if (size >= 2) {
                        snd_seq_ev_set_pgmchange(&ev, channel, data[1]);
                        break;
                    }
                    goto send_sysex;
                    
                case 0xE0: // Pitch Bend
                    if (size >= 3) {
                        int value = data[1] | (data[2] << 7);
                        snd_seq_ev_set_pitchbend(&ev, channel, value - 8192);
                        break;
                    }
                    goto send_sysex;
                    
                default:
                send_sysex:
                    // Send as SysEx for everything else
                    snd_seq_ev_set_sysex(&ev, size, (void*)data);
                    break;
            }
        }
        
        int err = snd_seq_event_output(sequencer, &ev);
        if (err < 0) {
            setLastErrorf("Failed to send MIDI event:%s(%s) error:%s", dev->name.c_str(), dev->id.c_str(), snd_strerror(err));
            ok = false;
        } else {
            snd_seq_drain_output(sequencer);
        }
    }

    return ok;
}

//--------------------------------------------------------------------------------------------------------------------------

#endif //defined(__linux__)
