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
bool        EasyMidiLib_update       ( );
void        EasyMidiLib_done         ( );
const char* EasyMidiLib_getLastError ( );

//--------------------------------------------------------------------------------------------------------------------------
// Enumeration
//--------------------------------------------------------------------------------------------------------------------------

void                     EasyMidiLib_updateInputsEnumeration ();
size_t                   EasyMidiLib_getInputDevicesNum      ();
const EasyMidiLibDevice* EasyMidiLib_getInputDevice          ( size_t i );
const EasyMidiLibDevice* EasyMidiLib_getInputDevice          ( const char* name );

void                     EasyMidiLib_updateOutputsEnumeration();
size_t                   EasyMidiLib_getOutputDevicesNum     ();
const EasyMidiLibDevice* EasyMidiLib_getOutputDevice         ( size_t i );
const EasyMidiLibDevice* EasyMidiLib_getOutputDevice         ( const char* name );

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
    bool        connected      ;
    bool        opened         ;
    void*       userPtrParam   ;
    int64_t     userIntParam   ;
    const void* internalHandler;

};

//--------------------------------------------------------------------------------------------------------------------------
// enums
//--------------------------------------------------------------------------------------------------------------------------

enum class EasyMidiLibNote : uint8_t
{  C0=12, Cs0=13, D0=14, Ds0=15, E0=16, F0=17, Fs0=18, G0=19, Gs0=20, A0=21, As0=22, B0=23
,  C1=24, Cs1=25, D1=26, Ds1=27, E1=28, F1=29, Fs1=30, G1=31, Gs1=32, A1=33, As1=34, B1=35
,  C2=36, Cs2=37, D2=38, Ds2=39, E2=40, F2=41, Fs2=42, G2=43, Gs2=44, A2=45, As2=46, B2=47
,  C3=48, Cs3=49, D3=50, Ds3=51, E3=52, F3=53, Fs3=54, G3=55, Gs3=56, A3=57, As3=58, B3=59
,  C4=60, Cs4=61, D4=62, Ds4=63, E4=64, F4=65, Fs4=66, G4=67, Gs4=68, A4=69, As4=70, B4=71
,  C5=72, Cs5=73, D5=74, Ds5=75, E5=76, F5=77, Fs5=78, G5=79, Gs5=80, A5=81, As5=82, B5=83
,  C6=84, Cs6=85, D6=86, Ds6=87, E6=88, F6=89, Fs6=90, G6=91, Gs6=92, A6=93, As6=94, B6=95
,  C7=96, Cs7=97, D7=98, Ds7=99, E7=100, F7=101, Fs7=102, G7=103, Gs7=104, A7=105, As7=106, B7=107
,  C8=108
};

enum class EasyMidiLibSysCommonMsg : uint8_t
{ SysExStart=0xF0, TimeCodeQuarter=0xF1, SongPosition=0xF2, SongSelect=0xF3, Undefined1=0xF4, Undefined2=0xF5, TuneRequest=0xF6, SysExEnd=0xF7 };

enum class EasyMidiLibSysRealtimeMsg : uint8_t
{ TimingClock=0xF8, Undefined3=0xF9, Start=0xFA, Continue=0xFB, Stop=0xFC, Undefined4=0xFD, ActiveSensing=0xFE, Reset=0xFF };

enum class EasyMidiLibMsg : uint8_t
{ NoteOff=0x80, NoteOn=0x90, PolyPressure=0xA0, ControlChange=0xB0, ProgramChange=0xC0, ChannelPressure=0xD0, PitchBend=0xE0 };

enum class EasyMidiLibCC : uint8_t
{ BankSelectMSB=0, ModulationWheel=1, BreathController=2, FootController=4, PortamentoTime=5, DataEntryMSB=6, Volume=7, Balance=8, Pan=10, ExpressionController=11
, EffectControl1=12, EffectControl2=13, GeneralPurpose1=16, GeneralPurpose2=17, GeneralPurpose3=18, GeneralPurpose4=19, BankSelectLSB=32, ModulationWheelLSB=33
, BreathControllerLSB=34, FootControllerLSB=36, PortamentoTimeLSB=37, DataEntryLSB=38, VolumeLSB=39, BalanceLSB=40, PanLSB=42, ExpressionControllerLSB=43, SustainPedal=64, Portamento=65
, Sostenuto=66, SoftPedal=67, LegatoFootswitch=68, Hold2=69, SoundController1=70, SoundController2=71, SoundController3=72, SoundController4=73, SoundController5=74
, SoundController6=75, SoundController7=76, SoundController8=77, SoundController9=78, SoundController10=79, AllSoundOff=120, ResetAllControllers=121
, LocalControl=122, AllNotesOff=123, OmniOff=124, OmniOn=125, MonoModeOn=126, PolyModeOn=127
};

//--------------------------------------------------------------------------------------------------------------------------
// EasyMidiLibListener
//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiLibListener
{
    public:

        EasyMidiLibListener ( )                                                        { setVerbose(false);   }
        EasyMidiLibListener ( bool verbose )                                           { setVerbose(verbose); }
        virtual ~EasyMidiLibListener()                                                 { }


        void            setVerbose         ( bool verbose )                            { m_verbose = verbose; }
        bool            getVerbose         ( bool verbose ) const                      { return m_verbose;    }


        // Library

        virtual void    libInit            ( )                                         { printf("listener -> libInit()\n"); }
        virtual void    libDone            ( )                                         { printf("listener -> libDone()\n"); }


        // Device

        virtual void    deviceConnected    ( const EasyMidiLibDevice* d )              { printf("listener -> %s deviceConnected %s (%s)\n"   , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
        virtual void    deviceReconnected  ( const EasyMidiLibDevice* d )              { printf("listener -> %s deviceReconnected %s (%s)\n" , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
        virtual void    deviceDisconnected ( const EasyMidiLibDevice* d )              { printf("listener -> %s deviceDisconnected %s (%s)\n", d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
        virtual void    deviceOpen         ( const EasyMidiLibDevice* d )              { printf("listener -> %s deviceOpen %s (%s)\n"        , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }
        virtual void    deviceClose        ( const EasyMidiLibDevice* d )              { printf("listener -> %s deviceClosed %s (%s)\n"      , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() ); }

        virtual void deviceOutData ( const EasyMidiLibDevice* d, const uint8_t* data, size_t dataSize )
        { 
            printf("listener -> %s deviceOutData %s (%s)\n    "      , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() );
            for ( size_t i=0; i!=dataSize; i++  )
                 printf ( "%02X ", data[i] );
            printf ("\n");
        }

        virtual size_t deviceInData ( const EasyMidiLibDevice* d, const uint8_t* data, size_t dataSize )
        { 
            printf("listener -> %s deviceInData %s (%s)\n    "      , d->isInput?"in":"out", d->name.c_str(), d->id.c_str() );
            for ( size_t i=0; i!=dataSize; i++  )
                 printf ( "%02X ", data[i] );
            printf ("\n");

            return processInData ( data, dataSize );
        }

        
        // Processing helper

        virtual size_t  processInData     ( const uint8_t* data, size_t dataSize );
        virtual void    noteOn            ( uint8_t channel, EasyMidiLibNote note, uint8_t velocity )       { printf("noteOn ch:%d note:%d vel:%d\n", channel, (int)note, velocity);         }
        virtual void    noteOff           ( uint8_t channel, EasyMidiLibNote note, uint8_t velocity )       { printf("noteOff ch:%d note:%d vel:%d\n", channel, (int)note, velocity);        }
        virtual void    programChange     ( uint8_t channel, uint8_t program )                              { printf("programChange ch:%d prog:%d\n", channel, program);                     }
        virtual void    controlChange     ( uint8_t channel, EasyMidiLibCC controller, uint8_t value )      { printf("controlChange ch:%d cc:%d val:%d\n", channel, (int)controller, value); }
        virtual void    pitchBend         ( uint8_t channel, uint16_t value )                               { printf("pitchBend ch:%d val:%d\n", channel, value);                            }
        virtual void    channelPressure   ( uint8_t channel, uint8_t pressure )                             { printf("channelPressure ch:%d press:%d\n", channel, pressure);                 }
        virtual void    polyPressure      ( uint8_t channel, EasyMidiLibNote note, uint8_t pressure )       { printf("polyPressure ch:%d note:%d press:%d\n", channel, (int)note, pressure); }
        virtual void    systemExclusive   ( const uint8_t* data, size_t size )                              { printf("systemExclusive size:%zu\n", size);                                    }
        virtual void    systemCommon      ( EasyMidiLibSysCommonMsg msg, const uint8_t* data, size_t size ) { printf("systemCommon msg:0x%02X\n", (int)msg);                                 }
        virtual void    systemRealtime    ( EasyMidiLibSysRealtimeMsg msg )                                 { printf("systemRealtime msg:0x%02X\n", (int)msg);                               }

    private:

        uint8_t         m_status  = 0;
        bool            m_verbose = true;
};

//------------------------------------------------------------------------------------------------------------------------

#endif //_EASYMIDILIB_H
