#include "EasyMidiLib.h"
#include <windows.h>
#include <conio.h>


//--------------------------------------------------------------------------------------------------------------------------

class EasyMidiLibListener 
{
};

class EasyMidiTestLibListener : public EasyMidiLibListener
{
};

//--------------------------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    bool ok = true;

    // Init library
    if (ok)
    {
        if (!EasyMidiLib_init())
        {
            printf ( "EasyMidiLib_init error:%s\n", EasyMidiLib_getLastError() );
            ok = false;
        }
    }

    // Loop
    if (ok)
    {
        printf ( "Press ESC to exit\n");
        bool running = true;
        while(running)
        {
          if (_kbhit()) {
              int ch = _getch();
              if (ch == 27)
                  running=false;
          }
          Sleep(10);
        }
    }

    // Done library
    EasyMidiLib_done();

    return ok?0:-1;
}

//--------------------------------------------------------------------------------------------------------------------------
