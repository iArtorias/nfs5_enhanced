#include "silent_compat.h"

#define DIRECTINPUT_VERSION 0x0500

#include <algorithm>
#include <dinput.h>

#include <array>
#include <cstring>
#include <string>
#include <utility>

#include "../../core/log.h"
#include "../../game/addresses.h"
#include "../../game/thrash.h"

namespace nfs5
{
    namespace
    {
        // Replacement for 'IDirectInputA::EnumDevices' as called by the controller enumerator
        // Only keyboards and joysticks that are attached are reported, the game ignores mice.
        HRESULT __stdcall EnumAttachedDevices(
            IDirectInputA * direct_input,
            DWORD,
            LPDIENUMDEVICESCALLBACKA callback,
            LPVOID reference,
            DWORD
        )
        {
            HRESULT hr = direct_input->EnumDevices( DIDEVTYPE_KEYBOARD, callback, reference, DIEDFL_ATTACHEDONLY );

            if (FAILED( hr ))
                return hr;

            hr = direct_input->EnumDevices( DIDEVTYPE_JOYSTICK, callback, reference, DIEDFL_ATTACHEDONLY );

            if (FAILED( hr ))
                return hr;

            return S_OK;
        }


        // The game calls through [ecx+10h], slot 4 of this table.
        std::array< void *, 5 > s_EnumDevicesTable = {
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            reinterpret_cast<void *>(&EnumAttachedDevices)
        };


        std::string ReadClipboardText( HWND window )
        {
            std::string result;
            if (::IsClipboardFormatAvailable( CF_TEXT ) == FALSE || ::OpenClipboard( window ) == FALSE)
            {
                return result;
            }

            HANDLE data = ::GetClipboardData( CF_TEXT );
            if (data != nullptr)
            {
                const auto * text = static_cast<const char *>(::GlobalLock( data ));
                if (text != nullptr)
                {
                    result.assign( text );
                    ::GlobalUnlock( data );
                }
            }
            ::CloseClipboard();
            return result;
        }


        // Verok OpenGL thrash driver leak fix state.
        using FreeFn = void( __cdecl * )(void *);
        FreeFn * s_DriverFreeImport = nullptr;
        FreeFn * s_DriverFreeOverride = nullptr;
        void * s_LastUnlockedRect = nullptr;

        void DriverFree( void * data )
        {
            if (s_DriverFreeOverride != nullptr && *s_DriverFreeOverride != nullptr)
            {
                (*s_DriverFreeOverride)(data);
            }
            else if (s_DriverFreeImport != nullptr && *s_DriverFreeImport != nullptr)
            {
                (*s_DriverFreeImport)(data);
            }
        }
    }

    SilentCompat & SilentCompat::GetInstance()
    {
        static SilentCompat instance;
        return instance;
    }

    int __stdcall SilentCompat::KeyDownHandlerHook(
        int * context,
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param,
        LRESULT * result
    )
    {
        SilentCompat & self = GetInstance();
        const int handled = self.m_KeyDownHook->stdcall< int >( context, window, message, w_param, l_param, result );

        // The original handler swallows every key so 'DefWindowProc' never sees Alt+F4
        // Only that combination is released, plain Alt keeps being eaten so the
        // system menu loop cannot freeze the game.
        if (self.m_Settings.m_AltF4 && message == WM_SYSKEYDOWN && w_param == VK_F4)
            return 0;

        return handled;
    }


    void __cdecl SilentCompat::SetLockKeyStateHook(
        int,
        int
    )
    {
        // Intentionally empty, the game used 'keybd_event' to force Caps Num and Scroll Lock off.
    }

    LRESULT __stdcall SilentCompat::WndProcHook(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param
    )
    {
        SilentCompat & self = GetInstance();

        if (self.m_Settings.m_ClipboardPaste && message == WM_CHAR && w_param == 0x16)
        {
            // 'Ctrl+V', the text boxes need a delimiter between repeated characters
            char last_char = '\0';
            for (const char ch : ReadClipboardText( window ))
            {
                if (std::exchange( last_char, ch ) == ch)
                {
                    ::SendMessageA( window, WM_CHAR, '\x2', 1 );
                }

                ::SendMessageA( window, WM_CHAR, static_cast<unsigned char>(ch), 1 );
            }

            return 0;
        }

        if (self.m_Settings.m_ClearKeysOnFocusLoss && message == WM_KILLFOCUS)
        {
            // Keys held while alt tabbing would otherwise stay pressed.
            std::memset( hook::Ptr<void>( game::var::KEY_STATE ), 0, game::var::KEY_STATE_SIZE );
        }

        return self.m_WndProcHook->stdcall< LRESULT >( window, message, w_param, l_param );
    }


    void SilentCompat::EnumDevicesMidHook(
        safetyhook::Context & context
    )
    {
        context.ecx = reinterpret_cast<std::uintptr_t>(s_EnumDevicesTable.data());
    }


    void __fastcall SilentCompat::ThrashUnlockWindowHook(
        void * self_rect,
        void *
    )
    {
        SilentCompat & self = GetInstance();

        using UnlockFn = void( __thiscall * )(void *);
        reinterpret_cast<UnlockFn>(self.m_ThrashUnlockPatch.GetOriginalTarget())(self_rect);

        // The driver never released the previous lock rectangle and its buffer.
        void * last_rect = std::exchange( s_LastUnlockedRect, self_rect );

        if (last_rect != nullptr)
        {
            DriverFree( *static_cast<void **>(last_rect) );
            DriverFree( last_rect );
        }
    }


    int __stdcall SilentCompat::ReadRectHook(
        int x,
        int y,
        int width,
        int height,
        void * buffer
    )
    {
        SilentCompat & self = GetInstance();

        const std::int32_t screen_width = hook::Ref<std::int32_t>( game::var::SCREEN_WIDTH );
        const std::int32_t screen_height = hook::Ref<std::int32_t>( game::var::SCREEN_HEIGHT );

        // The sun glare test asks for '( x - 2, y - 2, 4, 4 )' around the projected sun, which
        // leaves the frame within two pixels of an edge and reads outside the surface.
        if (screen_width > 0 && screen_height > 0 && width > 0 && height > 0 && width <= screen_width && height <= screen_height)
        {
            x = std::clamp( x, 0, screen_width - width );
            y = std::clamp( y, 0, screen_height - height );
        }

        return self.m_OriginalReadRect( x, y, width, height, buffer );
    }

    void SilentCompat::WrapReadRect()
    {
        auto & slot = hook::Ref<ReadRectFn>( game::var::THRASH_READRECT );

        if (slot == nullptr || slot == &ReadRectHook)
            return;

        m_OriginalReadRect = slot;
        slot = &ReadRectHook;

        LOG_INFO( "THRASH_readrect wrapped with a bounds clamp, original {:08X}", reinterpret_cast<std::uintptr_t>(m_OriginalReadRect) );
    }


    void SilentCompat::OnThrashDriverLoaded(
        HMODULE driver
    )
    {
        if (!m_Settings.m_ThrashDriverLeakFix || driver == nullptr)
            return;

        // A reloaded driver is a new module, drop the patch of the previous one first. Restore
        // skips the write when that module is already unmapped.
        m_ThrashUnlockPatch.Restore();
        s_DriverFreeImport = nullptr;
        s_DriverFreeOverride = nullptr;

        // These patterns only exist in Verok OpenGL thrash drivers, stock 'dx7z.dll' does not match.
        const std::uintptr_t free_call = hook::FindPattern( driver, "FF 15 ? ? ? ? 83 C4 04 FF 74 24", false );
        const std::uintptr_t free_override = hook::FindPattern( driver, "A1 ? ? ? ? 56 85 C0 74 04 FF D0", false );
        const std::uintptr_t unlock_site = hook::FindPattern( driver, "8B 4D 08 E8 ? ? ? ? A1", false );

        if (free_call == 0 || free_override == 0 || unlock_site == 0)
        {
            LOG_INFO( "Thrash driver leak fix not applicable to this driver" );
            return;
        }

        s_DriverFreeImport = *reinterpret_cast<FreeFn **>(free_call + 2);
        s_DriverFreeOverride = *reinterpret_cast<FreeFn **>(free_override + 1);
        s_LastUnlockedRect = nullptr;
        m_ThrashUnlockPatch.Apply( "ThrashUnlockWindowInternal", unlock_site + 3, reinterpret_cast<void *>(&ThrashUnlockWindowHook) );
    }

    // 'ConvertFile' from 'gimme.dll' turns the selected driver picture of 'AllDrivr.fsh' into
    // SaveData 'pic16.fsh' when the front end comes back from a race. Its shape
    // parser at RVA 1054A0 trusts a palette count at +2898h of a block that is
    // shorter than the 2AA4h it assumes, so it reads heap garbage as the count
    // and walks off the end of the heap. Counts that cannot fit are rejected.
    namespace
    {
        constexpr std::uintptr_t GIMME_SHAPE_PARSER_RVA = 0x001054A0;
        constexpr std::string_view GIMME_SHAPE_PARSER_SIG = "55 8B EC 83 EC 10 56 57 C7 45 FC 00 00 00 00 C7";
        constexpr std::size_t GIMME_SHAPE_SIZE = 0x2AA4;
        constexpr std::size_t GIMME_COUNT_OFFSET = 0x2898;
        constexpr std::size_t GIMME_TABLE_OFFSET = 0x289C;
        constexpr std::int32_t GIMME_MAX_COUNT = static_cast<std::int32_t>((GIMME_SHAPE_SIZE - GIMME_TABLE_OFFSET) / 16);

        bool IsReadableRange(
            const void * address,
            std::size_t size
        )
        {
            MEMORY_BASIC_INFORMATION info = {};
            if (::VirtualQuery( address, &info, sizeof( info ) ) == 0 || info.State != MEM_COMMIT
                || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            {
                return false;
            }

            const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            return reinterpret_cast<std::uintptr_t>(address) + size <= end;
        }
    }


    int __cdecl SilentCompat::GimmeShapeHook(
        const std::uint8_t * shape,
        void ** out_data,
        int * out_size,
        int flags
    )
    {
        SilentCompat & self = GetInstance();

        if (shape != nullptr && IsReadableRange( shape, GIMME_COUNT_OFFSET + 4 ))
        {
            std::int32_t count = 0;
            std::memcpy( &count, shape + GIMME_COUNT_OFFSET, sizeof( count ) );

            if (count < 0 || count > GIMME_MAX_COUNT)
            {
                LOG_INFO( "'gimme.dll' shape palette count {} is not valid, palette ignored", count );

                if (out_data != nullptr)
                    *out_data = nullptr;

                if (out_size != nullptr)
                    *out_size = 0;

                return 0;
            }
        }

        return self.m_GimmeShapeHook->ccall< int >( shape, out_data, out_size, flags );
    }


    int __cdecl SilentCompat::ConvertFileWrapper(
        const char * source,
        const char * destination,
        int index
    )
    {
        SilentCompat & self = GetInstance();

        using ConvertFileFn = int( __cdecl * )(const char *, const char *, int);
        const auto original = reinterpret_cast<ConvertFileFn>(self.m_GimmeConvertFile);

        // 'gimme.dll' is loaded and freed around every call, so the parser hook lives
        // exactly as long as this call and is removed before FreeLibrary
        HMODULE gimme = nullptr;
        ::GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(self.m_GimmeConvertFile), &gimme );

        if (gimme != nullptr)
        {
            self.m_GimmeShapeHook = hook::CreateInline( "GimmeShapeParser", reinterpret_cast<std::uintptr_t>(gimme) + GIMME_SHAPE_PARSER_RVA,
                GIMME_SHAPE_PARSER_SIG, reinterpret_cast<void *>(&GimmeShapeHook) );
        }

        const int result = original( source, destination, index );
        self.m_GimmeShapeHook.reset();
        return result;
    }


    FARPROC WINAPI SilentCompat::GetProcAddressHook(
        HMODULE module,
        LPCSTR name
    )
    {
        SilentCompat & self = GetInstance();

        auto original = self.m_GetProcAddressPatch.GetOriginal< decltype(&::GetProcAddress) >();
        FARPROC result = original( module, name );

        // Ordinal lookups pass a value below 64K instead of a string.
        if (result != nullptr && reinterpret_cast<std::uintptr_t>(name) > 0xFFFF && std::strcmp( name, "ConvertFile" ) == 0)
        {
            self.m_GimmeConvertFile = reinterpret_cast<void *>(result);
            return reinterpret_cast<FARPROC>(&ConvertFileWrapper);
        }

        return result;
    }


    bool SilentCompat::Install(
        const CompatSettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "SilentCompat disabled by config" );
            return true;
        }

        using namespace game;
        bool ok = true;

        if (settings.m_AltF4)
        {
            m_KeyDownHook = hook::CreateInline( "KeyDownHandler", fn::KEY_DOWN_HANDLER.m_Address, fn::KEY_DOWN_HANDLER.m_Signature,
                reinterpret_cast<void *>(&KeyDownHandlerHook) );
            ok &= m_KeyDownHook != nullptr;
        }

        if (settings.m_KeepLockKeys)
        {
            m_LockKeyHook = hook::CreateInline( "SetLockKeyState", fn::SET_LOCK_KEY_STATE.m_Address, fn::SET_LOCK_KEY_STATE.m_Signature,
                reinterpret_cast<void *>(&SetLockKeyStateHook) );
            ok &= m_LockKeyHook != nullptr;
        }

        if (settings.m_ClipboardPaste || settings.m_ClearKeysOnFocusLoss)
        {
            m_WndProcHook = hook::CreateInline( "GameWndProc", fn::GAME_WND_PROC.m_Address, fn::GAME_WND_PROC.m_Signature,
                reinterpret_cast<void *>(&WndProcHook) );
            ok &= m_WndProcHook != nullptr;
        }

        if (settings.m_FixDeviceEnumeration)
        {
            if (hook::MatchBytes( site::ENUM_DEVICES_BLOCK, site::ENUM_DEVICES_BLOCK_SIG ))
            {
                m_EnumDevicesHook = hook::CreateMid( "EnumDevices", site::ENUM_DEVICES_HOOK, site::ENUM_DEVICES_HOOK_SIG, &EnumDevicesMidHook );
            }
            else
            {
                LOG_ERROR( "'EnumDevices' call block signature mismatch, fix skipped" );
            }

            ok &= m_EnumDevicesHook != nullptr;
        }

        if (settings.m_FixPictureConversion)
        {
            ok &= m_GetProcAddressPatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "GetProcAddress",
                reinterpret_cast<void *>(&GetProcAddressHook) );
        }

        if (settings.m_ThrashDriverLeakFix)
        {
            game::ThrashDriver::GetInstance().AddLoadListener( [this] ( HMODULE driver )
            {
                OnThrashDriverLoaded( driver );
            } );
        }

        if (settings.m_FixScreenReadback)
        {
            game::ThrashDriver::GetInstance().AddLoadListener( [this] ( HMODULE )
            {
                WrapReadRect();
            } );
        }

        m_Installed = true;
        LOG_INFO( "SilentCompat installed, all hooks ok {}", ok );
        return ok;
    }


    void SilentCompat::Uninstall()
    {
        auto & read_rect = hook::Ref<ReadRectFn>( game::var::THRASH_READRECT );

        if (read_rect == &ReadRectHook && m_OriginalReadRect != nullptr)
            read_rect = m_OriginalReadRect;

        m_GetProcAddressPatch.Restore();
        m_ThrashUnlockPatch.Restore();
        m_EnumDevicesHook.reset();
        m_WndProcHook.reset();
        m_LockKeyHook.reset();
        m_KeyDownHook.reset();
        m_Installed = false;
    }
}