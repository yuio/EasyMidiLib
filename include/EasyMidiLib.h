#ifndef _EASYMIDILIB_H
#define _EASYMIDILIB_H

#include <string>

//--------------------------------------------------------------------------------------------------------------------------

struct EasyMidiLibDevice        ;
class  EasyMidiLibListener      ;

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

void                     EasyMidiLib_updateInputsEnumeration ();
size_t                   EasyMidiLib_getInputDevicesNum      ();
const EasyMidiLibDevice* EasyMidiLib_getInputDevice          ( size_t i );

void                     EasyMidiLib_updateOutputsEnumeration();
size_t                   EasyMidiLib_getOutputDevicesNum     ();
const EasyMidiLibDevice* EasyMidiLib_getOutputDevice         ( size_t i );

//--------------------------------------------------------------------------------------------------------------------------
// Input
//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_inputOpen  ( size_t enumIndex            , void* userPtrParam=0, int64_t userIntParam=0 );
bool EasyMidiLib_inputOpen  ( const EasyMidiLibDevice* dev, void* userPtrParam=0, int64_t userIntParam=0 );
void EasyMidiLib_inputClose ( const EasyMidiLibDevice* dev );

//--------------------------------------------------------------------------------------------------------------------------
// Output
//--------------------------------------------------------------------------------------------------------------------------

bool EasyMidiLib_outputOpen  ( size_t enumIndex            , void* userPtrParam=0, int64_t userIntParam=0 );
bool EasyMidiLib_outputOpen  ( const EasyMidiLibDevice* dev, void* userPtrParam=0, int64_t userIntParam=0 );
void EasyMidiLib_outputClose ( const EasyMidiLibDevice* dev );

bool EasyMidiLib_outputSend  ( const EasyMidiLibDevice* dev, const uint8_t* data, size_t size );

//--------------------------------------------------------------------------------------------------------------------------
// EasyMidiLibDevice
//--------------------------------------------------------------------------------------------------------------------------

struct EasyMidiLibDevice
{ 
    bool        isInput        ;
    std::string name           ;
    std::string id             ;
    bool        opened         ;
    void*       userPtrParam   ;
    int64_t     userIntParam   ;
    const void* internalHandler;

};

//--------------------------------------------------------------------------------------------------------------------------
// EasyMidiLibListener
//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiLibListener
{
    public:
        virtual void    libInit            ( )                                                                  {}                   // caller  thread
        virtual void    libDone            ( )                                                                  {}                   // caller  thread
        virtual void    deviceConnected    ( const EasyMidiLibDevice* d )                                       {}                   // unknown thread
        virtual void    deviceReconnected  ( const EasyMidiLibDevice* d )                                       {}                   // unknown thread
        virtual void    deviceDisconnected ( const EasyMidiLibDevice* d )                                       {}                   // unknown thread
        virtual void    deviceOpen         ( const EasyMidiLibDevice* d )                                       {}                   // caller  thread
        virtual void    deviceClose        ( const EasyMidiLibDevice* d )                                       {}                   // caller  thread
        virtual size_t  deviceInData       ( const EasyMidiLibDevice* d, const uint8_t* data, size_t dataSize ) { return dataSize; } // unknown thread
        virtual void    deviceOutData      ( const EasyMidiLibDevice* d, const uint8_t* data, size_t dataSize ) {}                   // caller  thread
};
//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiLibTestListener : public EasyMidiLibListener
{
    void    libInit            ( ) override                                { printf("listener -> libInit()\n"); }
    void    libDone            ( ) override                                { printf("listener -> libDone()\n"); }
    void    deviceConnected    ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceConnected %s (%s)\n"   , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    void    deviceReconnected  ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceReconnected %s (%s)\n" , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    void    deviceDisconnected ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceDisconnected %s (%s)\n", d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    void    deviceOpen         ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceOpen %s (%s)\n"        , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    void    deviceClose        ( const EasyMidiLibDevice* d ) override     { printf("listener -> %s deviceClosed %s (%s)\n"      , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
    size_t  deviceInData       ( const EasyMidiLibDevice* d, const uint8_t* data, size_t dataSize ) override
    { 
        printf("listener -> %s deviceInData %s (%s)\n    "      , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() );
        for ( size_t i=0; i!=dataSize; i++  )
             printf ( "%02X ", data[i] );
        printf ("\n");

        return SIZE_MAX; 
    }

    void    deviceOutData      ( const EasyMidiLibDevice* d, const uint8_t* data, size_t dataSize ) override 
    { 
        printf("listener -> %s deviceOutData %s (%s)\n    "      , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() );
        for ( size_t i=0; i!=dataSize; i++  )
             printf ( "%02X ", data[i] );
        printf ("\n");
    }
};
extern EasyMidiLibTestListener EasyMidiLib_testListener;

//--------------------------------------------------------------------------------------------------------------------------

#endif //_EASYMIDILIB_H
