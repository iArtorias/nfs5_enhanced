#pragma once

// Port of the NFS Porsche specific parts of 'SilentPatchNFS90s' by Silent.
// Byte patches from the original were replaced with hooks on the verified
// routines that contained them.

#include <atomic>
#include <cstdint>
#include <memory>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/hook_util.h"

namespace nfs5
{
    class SilentCompat
    {
    private:

        std::unique_ptr<safetyhook::InlineHook> m_KeyDownHook;
        std::unique_ptr<safetyhook::InlineHook> m_LockKeyHook;
        std::unique_ptr<safetyhook::InlineHook> m_WndProcHook;
        std::unique_ptr<safetyhook::MidHook> m_EnumDevicesHook;
        std::unique_ptr<safetyhook::MidHook> m_CardActionHook;
        std::atomic_bool m_CardSpaceConsumed{ false };
        static void CardActionHook( safetyhook::Context & context );

        hook::CallSitePatch m_ThrashUnlockPatch;
        hook::IatPatch m_GetProcAddressPatch;
        std::unique_ptr<safetyhook::InlineHook> m_GimmeShapeHook;
        void * m_GimmeConvertFile = nullptr;

        using ReadRectFn = int( __stdcall * )(
            int x,
            int y,
            int width,
            int height,
            void * buffer
            );

        ReadRectFn m_OriginalReadRect = nullptr;
        CompatSettings m_Settings;
        bool m_Installed = false;

        SilentCompat() = default;

        static int __stdcall KeyDownHandlerHook(
            int * context,
            HWND window,
            UINT message,
            WPARAM w_param,
            LPARAM l_param,
            LRESULT * result
        );

        static void __cdecl SetLockKeyStateHook(
            int which,
            int state
        );

        static LRESULT __stdcall WndProcHook(
            HWND window,
            UINT message,
            WPARAM w_param,
            LPARAM l_param
        );

        static void EnumDevicesMidHook(
            safetyhook::Context & context
        );

        static void __fastcall ThrashUnlockWindowHook(
            void * self,
            void * unused_edx
        );

        void OnThrashDriverLoaded(
            HMODULE driver
        );

        void WrapReadRect();

        static int __stdcall ReadRectHook(
            int x,
            int y,
            int width,
            int height,
            void * buffer
        );

        static FARPROC WINAPI GetProcAddressHook(
            HMODULE module,
            LPCSTR name
        );

        static int __cdecl ConvertFileWrapper(
            const char * source,
            const char * destination,
            int index
        );

        static int __cdecl GimmeShapeHook(
            const std::uint8_t * shape,
            void ** out_data,
            int * out_size,
            int flags
        );

    public:

        SilentCompat( const SilentCompat & ) = delete;
        SilentCompat & operator=( const SilentCompat & ) = delete;

        static SilentCompat & GetInstance();

        bool Install(
            const CompatSettings & settings
        );

        void Uninstall();

        [[nodiscard]] bool IsInstalled() const
        {
            return m_Installed;
        }
    };
}