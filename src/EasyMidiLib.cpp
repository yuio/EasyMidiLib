#include "EasyMidiLib.h"
#include <cstdio>

//--------------------------------------------------------------------------------------------------------------------------

const EasyMidiLibDevice* EasyMidiLib_getInputDevice ( const char* name )
{
    const EasyMidiLibDevice* foundDev = 0;

    for ( size_t i=0; i!=EasyMidiLib_getInputDevicesNum(); i++ )
    { 
        const EasyMidiLibDevice* testDev = EasyMidiLib_getInputDevice ( i );
        if ( testDev->name==name )
        {
            foundDev = testDev;
            break;
        }
    }

    return foundDev;
}

//--------------------------------------------------------------------------------------------------------------------------

const EasyMidiLibDevice* EasyMidiLib_getOutputDevice ( const char* name )
{
    const EasyMidiLibDevice* foundDev = 0;

    for ( size_t i=0; i!=EasyMidiLib_getOutputDevicesNum(); i++ )
    { 
        const EasyMidiLibDevice* testDev = EasyMidiLib_getOutputDevice ( i );
        if ( testDev->name==name )
        {
            foundDev = testDev;
            break;
        }
    }

    return foundDev;
}

//--------------------------------------------------------------------------------------------------------------------------

size_t EasyMidiLibListener::processInData(const uint8_t* data, size_t dataSize)
{
    size_t consumed = 0;
    
    for (size_t i = 0; i < dataSize; ++i)
    {
        uint8_t byte = data[i];
        
        // Status byte (MSB set)
        if (byte & 0x80)
        {
            m_status = byte;
            
            // System Real-Time messages (single byte)
            if (byte >= 0xF8)
            {
                systemRealtime(static_cast<EasyMidiLibSysRealtimeMsg>(byte));
                consumed = i + 1;
                continue;
            }
            
            // System Common messages
            if (byte >= 0xF0)
            {
                // Handle SysEx specially
                if (byte == 0xF0)
                {
                    // Find end of SysEx
                    size_t sysexEnd = i + 1;
                    while (sysexEnd < dataSize && data[sysexEnd] != 0xF7)
                        sysexEnd++;
                    
                    if (sysexEnd < dataSize && data[sysexEnd] == 0xF7)
                    {
                        // Complete SysEx message
                        systemExclusive(&data[i], sysexEnd - i + 1);
                        consumed = sysexEnd + 1;
                        i = sysexEnd;
                    }
                    else
                    {
                        // Incomplete SysEx, return consumed bytes
                        break;
                    }
                }
                else
                {
                    // Other system common messages
                    systemCommon(static_cast<EasyMidiLibSysCommonMsg>(byte), &data[i + 1], 0);
                    consumed = i + 1;
                }
                continue;
            }
        }
        
        // Channel messages - need status + data bytes
        if (m_status >= 0x80 && m_status <= 0xEF)
        {
            uint8_t msgType = m_status & 0xF0;
            uint8_t channel = m_status & 0x0F;
            
            // Determine how many data bytes we need
            size_t bytesNeeded = 2; // Most messages need 2 bytes
            if (msgType == 0xC0 || msgType == 0xD0) // Program Change, Channel Pressure
                bytesNeeded = 1;
            
            // Check if we have enough data
            size_t dataStart = (byte & 0x80) ? i + 1 : i; // Skip status if present
            size_t remainingBytes = dataSize - dataStart;
            
            if (remainingBytes < bytesNeeded)
            {
                // Not enough data, return what we consumed so far
                break;
            }
            
            // Extract data bytes
            uint8_t data1 = data[dataStart];
            uint8_t data2 = (bytesNeeded > 1) ? data[dataStart + 1] : 0;
            
            // Process the message
            switch (msgType)
            {
                case 0x80: // Note Off
                    noteOff(channel, static_cast<EasyMidiLibNote>(data1), data2);
                    break;
                    
                case 0x90: // Note On
                    if (data2 == 0)
                        noteOff(channel, static_cast<EasyMidiLibNote>(data1), data2);
                    else
                        noteOn(channel, static_cast<EasyMidiLibNote>(data1), data2);
                    break;
                    
                case 0xA0: // Polyphonic Pressure
                    polyPressure(channel, static_cast<EasyMidiLibNote>(data1), data2);
                    break;
                    
                case 0xB0: // Control Change
                    controlChange(channel, static_cast<EasyMidiLibCC>(data1), data2);
                    break;
                    
                case 0xC0: // Program Change
                    programChange(channel, data1);
                    break;
                    
                case 0xD0: // Channel Pressure
                    channelPressure(channel, data1);
                    break;
                    
                case 0xE0: // Pitch Bend
                    {
                        uint16_t value = (data2 << 7) | data1;
                        pitchBend(channel, value);
                    }
                    break;
            }
            
            consumed = dataStart + bytesNeeded;
            i = consumed - 1; // -1 because loop will increment
        }
        else
        {
            // No valid status byte, skip this byte
            consumed = i + 1;
        }
    }
    
    return consumed;
}

//--------------------------------------------------------------------------------------------------------------------------