#include "EasyMidiLib.h"
#include <windows.h>
#include <conio.h>

//--------------------------------------------------------------------------------------------------------------------------

void printEnumeration ( bool isInput )
{
    const char* type = isInput?"Input":"Output";

    printf("%ss ------------\n", type );
    
    size_t devicesNum = isInput ? EasyMidiLib_getInputDevicesNum() : EasyMidiLib_getOutputDevicesNum();
    for ( size_t i=0; i!=devicesNum; i++ )
    {
        const EasyMidiLibDevice* d = isInput ? EasyMidiLib_getInputDevice(i) : EasyMidiLib_getOutputDevice(i);
        char key = isInput ? char('0'+i) : char('A'+i);
        const char* status = d->opened ? "opened" : "closed";
        printf("%s %zu(key '%c'): %s %s (%s)\n",type, i, key, status, d->name.c_str(), d->id.c_str() );
    }
    printf("----------------\n");
}

//--------------------------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    bool ok = true;

    // Init library
    if (ok)
    {
        if (!EasyMidiLib_init( &EasyMidiLib_testListener ))
        {
            printf ( "EasyMidiLib_init error:%s\n", EasyMidiLib_getLastError() );
            ok = false;
        }
    }

    // Instructions
    if ( ok )
    {
        printEnumeration ( true  );
        printEnumeration ( false );
        printf ( "Press Z for inputs enumeration\n" );
        printf ( "Press X for outputs enumeration\n" );
        printf ( "Press ESC to exit\n" );
    }

    // Loop
    if (ok)
    {
        bool running = true;
        while(running)
        {
            EasyMidiLib_update();

          if (_kbhit()) 
          {
                int key = _getch();
                switch (key)
                {
                    case 'v' :
                    case 'V' :
                        {
                            for ( size_t i=0; i!=EasyMidiLib_getOutputDevicesNum(); i++ )
                            {
                                const EasyMidiLibDevice* device = EasyMidiLib_getOutputDevice(i);
                                if ( device->opened )
                                {
                                    static uint8_t programCount = 0;
                                    uint8_t data[2];
                                    data[0] = 0xC0;
                                    data[1] = programCount;
                                    EasyMidiLib_outputSend ( device, data, sizeof( data ) );

                                    programCount++;
                                    if (programCount>20)
                                        programCount=0;
                                }
                            }
                        }
                        break;

                    case 27 :
                        running=false;
                        break;

                    case 'z' : 
                    case 'Z' : 
                        EasyMidiLib_updateInputsEnumeration();
                        printEnumeration ( true );
                        break;

                    case 'x' : 
                    case 'X' : 
                        EasyMidiLib_updateOutputsEnumeration();
                        printEnumeration ( false );
                        break;

                    case '0' : case '1' : case '2' : case '3' : case '4' : case '5' : case '6' : case '7' : case '8' :case '9' :
                        {
                            size_t deviceIndex = size_t(key-'0');
                            if ( deviceIndex<EasyMidiLib_getInputDevicesNum() )
                            {
                                const EasyMidiLibDevice* device = EasyMidiLib_getInputDevice(deviceIndex);
                                if ( !device->opened )
                                {
                                    if ( !EasyMidiLib_inputOpen( deviceIndex ) )
                                        printf ( "EasyMidiLib_inputOpen error:%s\n", EasyMidiLib_getLastError() );
                                }
                                else
                                    EasyMidiLib_inputClose( device );
                            }
                        }
                        break;
                
                    case 'A' : case 'B' : case 'C' : case 'D' : case 'E' : case 'F' : case 'G' : case 'H' : case 'I': case 'J' :
                    case 'a' : case 'b' : case 'c' : case 'd' : case 'e' : case 'f' : case 'g' : case 'h' : case 'i': case 'j' :
                        {
                            size_t deviceIndex = std::islower(key) ? size_t(key-'a') : size_t(key-'A');
                            if ( deviceIndex<EasyMidiLib_getOutputDevicesNum() )
                            {
                                const EasyMidiLibDevice* device = EasyMidiLib_getOutputDevice(deviceIndex);
                                if ( !device->opened )
                                {
                                    if ( !EasyMidiLib_outputOpen( deviceIndex ) )
                                        printf ( "EasyMidiLib_outputOpen error:%s\n", EasyMidiLib_getLastError() );
                                }
                                else
                                    EasyMidiLib_inputClose( device );
                            }
                        }
                        break;                
                }
          }
          Sleep(10);
        }
    }

    // Done library
    EasyMidiLib_done();

    return ok?0:-1;
}

//--------------------------------------------------------------------------------------------------------------------------
