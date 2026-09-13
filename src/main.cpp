#include "App.h"

#include <windows.h>

#include <cstdio>
#include <stdexcept>

static int real_main(HINSTANCE hInst, int nCmdShow)
{
    App app;
    return app.Run(hInst, nCmdShow);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    try
    {
        return real_main(hInst, nCmdShow);
    }
    catch (const std::exception& e)
    {
        std::string msg = "Fatal: ";
        msg += e.what();
        msg += '\n';
        std::fputs(msg.c_str(), stderr);
        MessageBoxA(nullptr, msg.c_str(), "webview2_dcomp1", MB_OK | MB_ICONERROR);
        return 1;
    }
}
