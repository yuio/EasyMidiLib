#ifndef _EASYMIDILIB_H
#define _EASYMIDILIB_H

#include <string>
#include <vector>

//--------------------------------------------------------------------------------------------------------------------------

struct EasyMidiLibDevice        ;
class  EasyMidiLibListener      ;
class  EasyMidiLibDeviceListener;
class  EasyMidiLibInputListener ;

//--------------------------------------------------------------------------------------------------------------------------
// Main control
//--------------------------------------------------------------------------------------------------------------------------

bool        EasyMidiLib_init         ( EasyMidiLibListener* listener=0 );
void        EasyMidiLib_update       ( );
void        EasyMidiLib_done         ( );
const char* EasyMidiLib_getLastError ( );

//--------------------------------------------------------------------------------------------------------------------------
// Enumeration
//--------------------------------------------------------------------------------------------------------------------------

void                                         EasyMidiLib_updateInputsEnumeration ();
const std::vector<const EasyMidiLibDevice*>& EasyMidiLib_getInputDevices         ();

void                                         EasyMidiLib_updateOutputsEnumeration();
const std::vector<const EasyMidiLibDevice*>& EasyMidiLib_getOutputDevices        ();

//--------------------------------------------------------------------------------------------------------------------------
// Input
//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen  ( size_t enumIndex            , EasyMidiLibInputListener* inListener=0 );
bool EasyMidiLib_inputOpen  ( const EasyMidiLibDevice* dev, EasyMidiLibInputListener* inListener=0 );
void EasyMidiLib_inputClose ( const EasyMidiLibDevice* dev );

//--------------------------------------------------------------------------------------------------------------------------
// Output
//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen  ( size_t enumIndex             );
bool EasyMidiLib_outputOpen  ( const EasyMidiLibDevice* dev );
void EasyMidiLib_outputClose ( const EasyMidiLibDevice* dev );

//--------------------------------------------------------------------------------------------------------------------------
// EasyMidiLibDevice
//--------------------------------------------------------------------------------------------------------------------------

struct EasyMidiLibDevice
{ 
    bool        isInput        ;
    std::string name           ;
    std::string id             ;
    bool        opened         ;
    const void* internalHandler;
};

//--------------------------------------------------------------------------------------------------------------------------
// EasyMidiLibListener
//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiLibListener
{
    public:
        virtual void libInit            ( )                             {} // caller  thread
        virtual void libDone            ( )                             {} // caller  thread
        virtual void deviceConnected    ( const EasyMidiLibDevice* d )  {} // unknown thread
        virtual void deviceReconnected  ( const EasyMidiLibDevice* d )  {} // unknown thread
        virtual void deviceDisconnected ( const EasyMidiLibDevice* d )  {} // unknown thread
        virtual void deviceOpen         ( const EasyMidiLibDevice* d )  {} // caller  thread
        virtual void deviceClose        ( const EasyMidiLibDevice* d )  {} // caller  thread
        virtual void deviceInData       ( const EasyMidiLibDevice* d )  {} // unknown thread
};

//--------------------------------------------------------------------------------------------------------------------------
// EasyMidiLibDeviceListener
//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiLibDeviceListener
{ 
    public:
        virtual void open          ( const EasyMidiLibDevice* d )       {} // caller  thread
        virtual void close         ( const EasyMidiLibDevice* d )       {} // caller  thread
        virtual void connected     ( const EasyMidiLibDevice* d )       {} // unknown thread
        virtual void disconnected  ( const EasyMidiLibDevice* d )       {} // unknown thread
};

//--------------------------------------------------------------------------------------------------------------------------
// EasyMidiLibInputListener
//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiLibInputListener
{ 
    private:
        virtual void inData        ( const EasyMidiLibDevice* d )       {} // unknown thread

};

//--------------------------------------------------------------------------------------------------------------------------

#endif //_EASYMIDILIB_H
