#pragma once
#include <windows.h>
namespace cm
{
    struct Settings
    {
        bool enabled = true;
        int  logLevel = 2;
        bool hookPointClimb = true;
        bool equipScan = true;
        int  maxScans = 8;
        int  nodeBudget = 600;
        bool animSwap = false;   // not implemented in this build; see README
        bool keepAxesDrawn = false; // not implemented in this build
    };
    void Start(HMODULE self);
    void Stop(bool processExit);
}
