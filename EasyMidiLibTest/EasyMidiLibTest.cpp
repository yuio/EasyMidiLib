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

int main(int argc, char* argv[])
{
    bool ok = true;

    // Init library
    if (ok)
    {
        if (!EasyMidiLib_init( &testListener, 0 ))
        {
            printf ( "EasyMidiLib_init error:%s\n", EasyMidiLib_getLastError() );
            ok = false;
        }
    }

    // Loop
    if (ok)
    {
        printf ( "Press I for inputs enumeration\n");
        printf ( "Press O for outputs enumeration\n");
        printf ( "Press ESC to exit\n");
        bool running = true;
        while(running)
        {
          if (_kbhit()) {
                switch (_getch())
                {
                    case 27 : 
                        running=false;
                        break;

                    case 'i' : 
                    case 'I' : 
                        {
                            printf("INS ------------\n");
                            EasyMidiLib_updateInputsEnumeration();
                            const std::vector<const EasyMidiLibDevice*>& devices = EasyMidiLib_getInputDevices();
                            for ( size_t i=0; i!=devices.size(); i++ )                            
                            {
                                const EasyMidiLibDevice* d = devices[i];
                                printf("input %zu: %s %s (%s)\n",i, d->open?"opened":"closed", d->name.c_str(),d->id.c_str());
                            }
                            printf("----------------\n");
                        }
                        break;

                    case 'o' : 
                    case 'O' : 
                        {
                            printf("OUTS -----------\n");
                            EasyMidiLib_updateOutputsEnumeration();
                            const std::vector<const EasyMidiLibDevice*>& devices = EasyMidiLib_getOutputDevices();
                            for ( size_t i=0; i!=devices.size(); i++ )                            
                            {
                                const EasyMidiLibDevice* d = devices[i];
                                printf("output %zu: %s %s (%s)\n",i, d->open?"opened":"closed", d->name.c_str(),d->id.c_str());
                            }
                            printf("----------------\n");
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
