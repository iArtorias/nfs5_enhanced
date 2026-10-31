#pragma once

#include "core/hook_util.h"

namespace nfs5
{
    // Restores the original first-person camera for the GT1 only.
    // Every other car continues to use the unmodified v3.5 camera routine.
    class Gt1Camera
    {
    public:
        static Gt1Camera & GetInstance();
        void Install();
        void Uninstall();

    private:
        static void __cdecl Mode0Dispatch(int slot);
        hook::CallSitePatch m_Mode0Call;
    };
}
