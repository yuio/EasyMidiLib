#include "EasyMidiLib.h"
#include <windows.h>
#include <conio.h>

//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiTestLibListener : public EasyMidiLibListener, public EasyMidiLibDeviceListener, public EasyMidiLibInputListener
{
    // EasyMidiLibListener messages

    void libInit            ( ) override                                { printf("listener -> libInit()\n"); }
    void libDone            ( ) override                                { printf("listener -> libDone()\n"); }
    void deviceConnected    ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceConnected %s (%s)\n"   , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    void deviceReconnected  ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceReconnected %s (%s)\n" , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    void deviceDisconnected ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceDisconnected %s (%s)\n", d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    void deviceInData       ( const EasyMidiLibDevice* d ) override     { }

    // EasyMidiLibDeviceListener messages

    void open               ( const EasyMidiLibDevice* d ) override     { }
    void close              ( const EasyMidiLibDevice* d ) override     { }
    void connected          ( const EasyMidiLibDevice* d ) override     { }
    void disconnected       ( const EasyMidiLibDevice* d ) override     { }


    // EasyMidiLibInputListener messages

    void inData             ( const EasyMidiLibDevice* d ) override     { }
};

EasyMidiTestLibListener testListener;


//--------------------------------------------------------------------------------------------------------------------------

void printEnumeration ( bool isInput, const std::vector<const EasyMidiLibDevice*>& devices )
{
    const char* type = isInput?"Input":"Output";
    printf("%ss ------------\n", type );
    for ( size_t i=0; i!=devices.size(); i++ )                            
    {
        const EasyMidiLibDevice* d = devices[i];
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
        if (!EasyMidiLib_init( &testListener, 10 ))
        {
            printf ( "EasyMidiLib_init error:%s\n", EasyMidiLib_getLastError() );
            ok = false;
        }
    }

    // Instructions
    if ( ok )
    {
        printEnumeration ( true , EasyMidiLib_getInputDevices () );
        printEnumeration ( false, EasyMidiLib_getOutputDevices() );
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
          if (_kbhit()) 
          {
                int key = _getch();
                switch (key)
                {
                    case 27 :
                        running=false;
                        break;

                    case 'z' : 
                    case 'Z' : 
                        EasyMidiLib_updateInputsEnumeration();
                        printEnumeration ( true, EasyMidiLib_getInputDevices() );
                        break;

                    case 'x' : 
                    case 'X' : 
                        EasyMidiLib_updateOutputsEnumeration();
                        printEnumeration ( false, EasyMidiLib_getOutputDevices() );
                        break;

                    case '0' : case '1' : case '2' : case '3' : case '4' : case '5' : case '6' : case '7' : case '8' :case '9' :
                        {
                            size_t deviceIndex = size_t(key-'0');
                            const std::vector<const EasyMidiLibDevice*>& devices = EasyMidiLib_getOutputDevices();
                            if ( deviceIndex<devices.size() )
                            {
                                const EasyMidiLibDevice* device = devices[deviceIndex];
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
                        {
                            size_t deviceIndex = size_t(key-'A');
                            const std::vector<const EasyMidiLibDevice*>& devices = EasyMidiLib_getOutputDevices();
                            if ( deviceIndex<devices.size() )
                            {
                                const EasyMidiLibDevice* device = devices[deviceIndex];
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
