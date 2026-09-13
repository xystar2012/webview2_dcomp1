#pragma once

#include "MainWnd.h"

#include <memory>
#include <string>

class App
{
public:
    App();
    ~App();

    int Run(HINSTANCE hInst, int nCmdShow);

private:
    std::unique_ptr<MainWnd> m_mainWnd;
};
