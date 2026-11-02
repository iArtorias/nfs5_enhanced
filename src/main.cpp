// Modernization layer for Need for Speed Porsche Unleashed v3.5.

#include <atomic>
#include <cwctype>
#include <filesystem>
#include <string>

#include "core/config.h"
#include "core/crash_handler.h"
#include "core/hook_util.h"
#include "core/log.h"
#include "core/win_handle.h"

#include <tlhelp32.h>

#include "game/addresses.h"
#include "game/thrash.h"

#include "subsystems/framerate/framerate.h"
#include "subsystems/gamepad/gamepad.h"
#include "subsystems/io_hooks/io_hooks.h"
#include "subsystems/framerate/interpolation.h"
#include "subsystems/hud_scaling/hud_scaling.h"
#include "subsystems/patch_silent_compat/affinity.h"
#include "subsystems/patch_silent_compat/qpc_timer.h"
#include "subsystems/patch_silent_compat/silent_compat.h"
#include "subsystems/widescreen/widescreen.h"
#include "subsystems/detail/detail.h"
#include "subsystems/text/text_quality.h"
#include "subsystems/menu/menu_scroll.h"
#include "subsystems/movie/movie_upscale.h"
#include "subsystems/camera/gt1_camera.h"

namespace nfs5
{
    namespace
    {
        constexpr std::string_view VERSION = "1.3.0";

        HMODULE s_SelfModule = nullptr;
        hook::IatPatch s_CommandLinePatch;
        std::atomic< bool > s_Initialized = false;
        // Set once the executable is verified and the game addresses are rebased, only then
        // the subsystems may touch game memory, also when they uninstall.
        std::atomic< bool > s_Hooked = false;
        hook::BytePatch s_CdCheckPatch;

        // Turns the 'je' after the 'fe.txt' probe in the CD search into a 'jmp'.
        void SkipCdCheck()
        {
            namespace fn = game::fn;
            const std::uintptr_t branch = (fn::CD_CHECK.m_Address + fn::CD_CHECK_BRANCH).Get();

            if (!hook::MatchBytes( fn::CD_CHECK.m_Address, fn::CD_CHECK.m_Signature ))
            {
                LOG_ERROR( "CD check site does not match, the game may ask for its CD" );
                return;
            }

            const std::uint8_t current = hook::Ref<std::uint8_t>( branch );

            if (current == 0xEB)
            {
                LOG_INFO( "CD check already removed in the executable" );
                return;
            }

            if (current != 0x74)
            {
                LOG_ERROR( "CD check branch {:02X} is unknown, left alone", current );
                return;
            }

            constexpr std::uint8_t JMP_SHORT[] = { 0xEB };
            s_CdCheckPatch.Apply( "SkipCdCheck", branch, "74 BB", JMP_SHORT );
        }


        std::filesystem::path GetSelfDirectory()
        {
            std::wstring buffer( MAX_PATH, L'\0' );

            for (;; )
            {
                const DWORD length = ::GetModuleFileNameW( s_SelfModule, buffer.data(), static_cast<DWORD>(buffer.size()) );

                if (length == 0)
                    return {};

                if (length < buffer.size())
                {
                    buffer.resize( length );
                    break;
                }

                buffer.resize( buffer.size() * 2 );
            }

            return std::filesystem::path( buffer ).parent_path();
        }


        bool VerifyExecutable()
        {
            const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW( nullptr ));
            const auto * dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
            const auto * nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);

            LOG_INFO( "Executable base {:08X} timestamp {:08X} image size {:08X}", base, nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage );

            // The image may be loaded elsewhere than its link time base (Mandatory ASLR), game
            // addresses are rebased with this delta and signatures ignore relocated operands.
            const bool same_build = nt->FileHeader.TimeDateStamp == game::EXPECTED_TIMESTAMP
                && nt->OptionalHeader.SizeOfImage == game::EXPECTED_IMAGE_SIZE;

            if (!same_build)
            {
                LOG_ERROR( "Unsupported 'Porsche.exe' build, expected the v3.5 executable, nothing was hooked" );
                return false;
            }

            game::g_ImageDelta = static_cast<std::intptr_t>(base) - static_cast<std::intptr_t>(game::EXPECTED_IMAGE_BASE);

            if (game::g_ImageDelta != 0)
            {
                LOG_WARN( "'Porsche.exe' was loaded at {:08X} instead of {:08X}, game addresses are rebased by {}{:X}",
                    base, game::EXPECTED_IMAGE_BASE, game::g_ImageDelta < 0 ? "-" : "+", game::g_ImageDelta < 0 ? -game::g_ImageDelta : game::g_ImageDelta );
            }

            return true;
        }


        // Finds any other loaded module whose file name starts with 'SilentPatchNFS90s',
        // renamed copies like 'SilentPatchNFS90s_.asi' are still picked up by the ASI loader.
        HMODULE FindSilentPatch()
        {
            UniqueHandle snapshot( ::CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, 0 ) );

            if (!snapshot)
            {
                return nullptr;
            }

            constexpr std::wstring_view PREFIX = L"silentpatchnfs90s";
            MODULEENTRY32W entry = {};
            entry.dwSize = sizeof( entry );

            for (BOOL ok = ::Module32FirstW( snapshot.Get(), &entry ); ok != FALSE; ok = ::Module32NextW( snapshot.Get(), &entry ))
            {
                if (entry.hModule == s_SelfModule)
                    continue;

                std::wstring name( entry.szModule );

                for (wchar_t & ch : name)
                {
                    ch = static_cast<wchar_t>(std::towlower( ch ));
                }

                if (name.starts_with( PREFIX ))
                    return entry.hModule;
            }

            return nullptr;
        }


        void Initialize()
        {
            if (s_Initialized.exchange( true ))
                return;

            const std::filesystem::path directory = GetSelfDirectory();

            Config & config = Config::GetInstance();
            config.Load( (directory / L"nfs5_enhanced.ini").wstring() );

            const Settings & settings = config.Get();

            Log::GetInstance().Open( (directory / L"nfs5_enhanced.log").wstring(), settings.m_General.m_LogLevel );
            LOG_INFO( "NFS5 Enhanced {} starting", VERSION );

            config.Dump();
            CrashHandler::GetInstance().Install( (directory / L"nfs5_enhanced_crashdump.dmp").wstring() );

            if (!VerifyExecutable())
                return;

            s_Hooked = true;

            if (settings.m_Compat.m_SkipCdCheck)
                SkipCdCheck();

            const bool silent_loaded = FindSilentPatch() != nullptr;
            const bool skip_compat = silent_loaded && settings.m_General.m_SkipOnSilentPatch;

            if (silent_loaded)
            {
                LOG_WARN( "Another 'SilentPatchNFS90s' ASI is loaded, it patches the same code as the compat subsystem. Rename it to .asi.disabled, compat and affinity are skipped until then" );
            }

            // Order matters, affinity first so threads created by later hooks are pinned,
            // the Thrash tracker before the subsystems that register driver listeners.
            if (!skip_compat)
            {
                AffinityManager::GetInstance().Install( settings.m_Affinity );
            }

            QpcTimer::GetInstance().Install( settings.m_Timing );
            game::ThrashDriver::GetInstance().Install( settings.m_Compat.m_FallbackDriver, settings.m_Compat.m_ThrashDriver );

            if (!skip_compat)
            {
                SilentCompat::GetInstance().Install( settings.m_Compat );
            }

            Widescreen::GetInstance().Install( settings.m_Widescreen );
            Detail::GetInstance().Install( settings.m_Detail );
            Framerate::GetInstance().Install( settings.m_Framerate );
            Interpolation::GetInstance().Install( settings.m_Framerate );
            HudScaling::GetInstance().Install( settings.m_Hud );
            TextQuality::GetInstance().Install( settings.m_Text );
            IoHooks::GetInstance().Install( settings.m_Io );
            Gamepad::GetInstance().Install( settings.m_Controller, directory );
            MenuScroll::GetInstance().Install( settings.m_Menu );
            MovieUpscale::GetInstance().Install( settings.m_Movie );
            Gt1Camera::GetInstance().Install();

            LOG_INFO( "Initialization finished" );
        }

        LPSTR WINAPI GetCommandLineHook()
        {
            Initialize();
            return s_CommandLinePatch.GetOriginal< decltype(&::GetCommandLineA) >()();
        }
    }
}


extern "C" BOOL WINAPI DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID reserved
)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        nfs5::s_SelfModule = module;
        ::DisableThreadLibraryCalls( module );

        if (!nfs5::s_CommandLinePatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "GetCommandLineA",
            reinterpret_cast<void *>(&nfs5::GetCommandLineHook) ))
        {
            return FALSE;
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (reserved == nullptr && nfs5::s_Hooked.load())
        {
            nfs5::Gt1Camera::GetInstance().Uninstall();
            nfs5::MovieUpscale::GetInstance().Uninstall();
            nfs5::MenuScroll::GetInstance().Uninstall();
            nfs5::Gamepad::GetInstance().Uninstall();
            nfs5::IoHooks::GetInstance().Uninstall();
            nfs5::TextQuality::GetInstance().Uninstall();
            nfs5::HudScaling::GetInstance().Uninstall();
            nfs5::Interpolation::GetInstance().Uninstall();
            nfs5::Framerate::GetInstance().Uninstall();
            nfs5::Detail::GetInstance().Uninstall();
            nfs5::Widescreen::GetInstance().Uninstall();
            nfs5::SilentCompat::GetInstance().Uninstall();
            nfs5::game::ThrashDriver::GetInstance().Uninstall();
            nfs5::QpcTimer::GetInstance().Uninstall();
            nfs5::AffinityManager::GetInstance().Uninstall();
        }

        nfs5::Log::GetInstance().Close();
    }

    return TRUE;
}