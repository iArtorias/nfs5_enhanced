#pragma once

// Intro and menu movies.
// The movie player decodes a frame at the size of the movie [320x240 or 320x176] and its software
// blitter ['sub_56A920'] converts and doubles it into the locked front buffer, so the graphics
// driver only sees a frame that is already enlarged. This hook sees the original sized frame and
// offers it to 'NFS5E_MovieBlit' of the DX11 driver, which enlarges it once at the final
// resolution. When the driver does not have that function or refuses a frame, the game draws it.

#include <atomic>
#include <memory>

#include "../../core/win_handle.h"

#include <safetyhook.hpp>

#include "../../core/config.h"

namespace nfs5
{
    class MovieUpscale
    {
    private:

        MovieSettings m_Settings;
        std::unique_ptr<safetyhook::InlineHook> m_BlitHook;

        // 'NFS5E_MovieBlit' of the graphics driver, null when the driver does not have it.
        using DriverBlitFn = int( __stdcall * )(const void * pixels, int pitch, int width, int height, int x, int y, int scale);
        std::atomic<DriverBlitFn> m_DriverBlit = nullptr;
        bool m_Installed = false;

        MovieUpscale() = default;

        void OnThrashDriverLoaded(
            HMODULE module
        );

        static int __cdecl BlitHook(
            int arg1,
            int arg2,
            int arg3,
            int arg4,
            int arg5,
            int arg6
        );

    public:

        MovieUpscale( const MovieUpscale & ) = delete;
        MovieUpscale & operator=( const MovieUpscale & ) = delete;

        static MovieUpscale & GetInstance();

        bool Install(
            const MovieSettings & settings
        );

        void Uninstall();
    };
}
