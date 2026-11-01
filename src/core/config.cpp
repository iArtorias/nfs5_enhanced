#include "config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cwchar>
#include <cwctype>

namespace nfs5
{
    namespace
    {
        constexpr DWORD VALUE_BUFFER_SIZE = 128;

        std::wstring Trim(
            std::wstring value
        )
        {
            const auto is_space = [] ( wchar_t ch )
            {
                return std::iswspace( ch ) != 0;
            };

            value.erase( value.begin(), std::find_if_not( value.begin(), value.end(), is_space ) );
            value.erase( std::find_if_not( value.rbegin(), value.rend(), is_space ).base(), value.end() );

            // Inline comments are allowed after the value.
            const std::size_t comment_pos = value.find_first_of( L";#" );
            if (comment_pos != std::wstring::npos)
            {
                value.erase( comment_pos );
                value.erase( std::find_if_not( value.rbegin(), value.rend(), is_space ).base(), value.end() );
            }

            return value;
        }

        std::string Narrow(
            const std::wstring & value
        )
        {
            std::string result;
            result.reserve( value.size() );

            for (const wchar_t ch : value)
            {
                result.push_back( ch < 0x80 ? static_cast<char>( ch ) : '?' );
            }

            return result;
        }
    }

    Config & Config::GetInstance()
    {
        static Config instance;
        return instance;
    }

    bool Config::ReadBool(
        const wchar_t * section,
        const wchar_t * key,
        bool fallback
    ) const
    {
        std::array< wchar_t, VALUE_BUFFER_SIZE > buffer = {};
        ::GetPrivateProfileStringW( section, key, L"", buffer.data(), VALUE_BUFFER_SIZE, m_Path.c_str() );

        std::wstring value = Trim( buffer.data() );

        if (value.empty())
            return fallback;

        std::transform( value.begin(), value.end(), value.begin(), [] ( wchar_t ch )
        {
            return static_cast<wchar_t>(std::towlower( ch ));
        } );

        if (value == L"1" || value == L"true" || value == L"on" || value == L"yes")
        {
            return true;
        }

        if (value == L"0" || value == L"false" || value == L"off" || value == L"no")
        {
            return false;
        }

        return fallback;
    }


    int Config::ReadInt(
        const wchar_t * section,
        const wchar_t * key,
        int fallback
    ) const
    {
        std::array< wchar_t, VALUE_BUFFER_SIZE > buffer = {};
        ::GetPrivateProfileStringW( section, key, L"", buffer.data(), VALUE_BUFFER_SIZE, m_Path.c_str() );

        const std::string value = Narrow( Trim( buffer.data() ) );
        int result = fallback;
        const auto [ptr, error] = std::from_chars( value.data(), value.data() + value.size(), result );

        if (error != std::errc() || ptr != value.data() + value.size())
            return fallback;

        return result;
    }


    std::wstring Config::ReadString(
        const wchar_t * section,
        const wchar_t * key,
        const wchar_t * fallback
    ) const
    {
        std::array< wchar_t, VALUE_BUFFER_SIZE > buffer = {};
        ::GetPrivateProfileStringW( section, key, L"", buffer.data(), VALUE_BUFFER_SIZE, m_Path.c_str() );
        std::wstring value = Trim( buffer.data() );

        if (value.empty())
        {
            return fallback;
        }

        std::transform( value.begin(), value.end(), value.begin(), [] ( wchar_t ch )
        {
            return static_cast<wchar_t>(std::towlower( ch ));
        } );

        return value;
    }


    float Config::ReadFloat(
        const wchar_t * section,
        const wchar_t * key,
        float fallback
    ) const
    {
        std::array< wchar_t, VALUE_BUFFER_SIZE > buffer = {};
        ::GetPrivateProfileStringW( section, key, L"", buffer.data(), VALUE_BUFFER_SIZE, m_Path.c_str() );

        const std::wstring value = Trim( buffer.data() );

        if (value.empty())
            return fallback;

        wchar_t * end = nullptr;
        const float result = std::wcstof( value.c_str(), &end );

        if (end == value.c_str() || *end != L'\0')
            return fallback;

        return result;
    }


    void Config::Load(
        const std::wstring & path
    )
    {
        m_Path = path;

        Settings & s = m_Settings;

        s.m_General.m_LogLevel = static_cast<LogLevel>(std::clamp( ReadInt( L"General", L"LogLevel", 1 ), 0, 4 ));
        s.m_General.m_SkipOnSilentPatch = ReadBool( L"General", L"SkipCompatIfSilentPatchLoaded", true );

        s.m_Compat.m_Enabled = ReadBool( L"Compat", L"Enabled", true );
        s.m_Compat.m_AltF4 = ReadBool( L"Compat", L"AltF4", true );
        s.m_Compat.m_KeepLockKeys = ReadBool( L"Compat", L"KeepLockKeys", true );
        s.m_Compat.m_FixDeviceEnumeration = ReadBool( L"Compat", L"FixDeviceEnumeration", true );
        s.m_Compat.m_ClearKeysOnFocusLoss = ReadBool( L"Compat", L"ClearKeysOnFocusLoss", true );
        s.m_Compat.m_ClipboardPaste = ReadBool( L"Compat", L"ClipboardPaste", true );
        s.m_Compat.m_ThrashDriverLeakFix = ReadBool( L"Compat", L"ThrashDriverLeakFix", true );
        s.m_Compat.m_FixPictureConversion = ReadBool( L"Compat", L"FixPictureConversion", true );
        s.m_Compat.m_FixScreenReadback = ReadBool( L"Compat", L"FixScreenReadback", true );
        s.m_Compat.m_SkipCdCheck = ReadBool( L"Compat", L"SkipCdCheck", true );
        s.m_Compat.m_FallbackDriver = Narrow( ReadString( L"Compat", L"FallbackDriver", L"dx7z" ) );
        s.m_Compat.m_ThrashDriver = Narrow( ReadString( L"Compat", L"ThrashDriver", L"dx11" ) );

        s.m_Affinity.m_Mode = static_cast<AffinityMode>(std::clamp( ReadInt( L"Affinity", L"Mode", 1 ), 0, 2 ));
        s.m_Affinity.m_GameCore = ReadInt( L"Affinity", L"GameCore", -1 );
        s.m_Affinity.m_PreferPerformanceCores = ReadBool( L"Affinity", L"PreferPerformanceCores", true );

        s.m_Timing.m_QpcTimer = ReadBool( L"Timing", L"QpcTimer", true );
        s.m_Timing.m_HighResolutionWaitableTimer = ReadBool( L"Timing", L"HighResolutionWaitableTimer", true );
        s.m_Timing.m_MaxCatchUpTicks = std::clamp( ReadInt( L"Timing", L"MaxCatchUpTicks", 128 ), 1, 1024 );

        s.m_Widescreen.m_Enabled = ReadBool( L"Widescreen", L"Enabled", true );
        s.m_Widescreen.m_HorPlus = ReadBool( L"Widescreen", L"HorPlus", true );
        s.m_Widescreen.m_FovMultiplier = std::clamp( ReadFloat( L"Widescreen", L"FovMultiplier", 1.0f ), 0.5f, 2.0f );
        s.m_Widescreen.m_FixSpriteAspect = ReadBool( L"Widescreen", L"FixSpriteAspect", true );
        s.m_Widescreen.m_UnlockResolutions = ReadBool( L"Widescreen", L"UnlockResolutions", true );

        // 'RaceResolution' accepts 'desktop', 'game' or WIDTHxHEIGHT.
        const std::wstring race_resolution = ReadString( L"Widescreen", L"RaceResolution", L"game" );
        if (race_resolution == L"desktop")
        {
            s.m_Widescreen.m_RaceWidth = -1;
            s.m_Widescreen.m_RaceHeight = -1;
        }
        else
        {
            // WIDTHxHEIGHT, text after the height is ignored.
            const wchar_t * text = race_resolution.c_str();
            wchar_t * width_end = nullptr;
            const long width = std::wcstol( text, &width_end, 10 );
            wchar_t * height_end = nullptr;
            const long height = (width_end != text && *width_end == L'x') ? std::wcstol( width_end + 1, &height_end, 10 ) : 0;

            if (height_end != nullptr && height_end != width_end + 1 && width >= 640 && height >= 480)
            {
                s.m_Widescreen.m_RaceWidth = static_cast<int>(width);
                s.m_Widescreen.m_RaceHeight = static_cast<int>(height);
            }
            else
            {
                s.m_Widescreen.m_RaceWidth = 0;
                s.m_Widescreen.m_RaceHeight = 0;
            }
        }

        const int depth = ReadInt( L"Widescreen", L"RaceColorDepth", 0 );
        s.m_Widescreen.m_RaceColorDepth = (depth == 16 || depth == 32) ? depth : 0;

        s.m_Widescreen.m_LodDistanceScale = std::clamp( ReadFloat( L"Widescreen", L"LodDistanceScale", 4.0f ), 0.0f, 16.0f );
        s.m_Framerate.m_Enabled = ReadBool( L"Framerate", L"Enabled", true );
        s.m_Framerate.m_FpsLimit = std::clamp( ReadInt( L"Framerate", L"FpsLimit", 0 ), 0, 1000 );
        s.m_Framerate.m_FrontendFpsLimit = std::clamp( ReadInt( L"Framerate", L"FrontendFpsLimit", 0 ), 0, 1000 );
        s.m_Framerate.m_Interpolation = ReadBool( L"Framerate", L"Interpolation", true );
        s.m_Framerate.m_InterpolateCamera = ReadBool( L"Framerate", L"InterpolateCamera", true );
        s.m_Framerate.m_SnapDistance = std::clamp( ReadFloat( L"Framerate", L"SnapDistance", 25.0f ), 1.0f, 1000.0f );

        s.m_Io.m_Enabled = ReadBool( L"IO", L"Enabled", true );
        s.m_Io.m_ReadChunkKB = std::clamp( ReadInt( L"IO", L"ReadChunkKB", 1024 ), 16, 65536 );
        s.m_Io.m_CacheDiskFreeSpace = ReadBool( L"IO", L"CacheDiskFreeSpace", true );
        s.m_Io.m_FastCpuSpeedCheck = ReadBool( L"Loading", L"FastCpuSpeedCheck", true );
        s.m_Io.m_GameplayRedirect = ReadBool( L"IO", L"GameplayRedirect", false );
        s.m_Io.m_GameplayRedirectTrace = ReadBool( L"IO", L"GameplayRedirectTrace", false );
        s.m_Hud.m_Enabled = ReadBool( L"HudScaling", L"Enabled", true );
        s.m_Hud.m_ReferenceHeight = std::clamp( ReadInt( L"HudScaling", L"ReferenceHeight", 480 ), 240, 4320 );
        s.m_Hud.m_ScaleMultiplier = std::clamp( ReadFloat( L"HudScaling", L"ScaleMultiplier", 1.0f ), 0.25f, 4.0f );
        s.m_Hud.m_MinimumScale = std::clamp( ReadFloat( L"HudScaling", L"MinimumScale", 1.0f ), 0.25f, 8.0f );
        s.m_Hud.m_ScaleText = ReadBool( L"HudScaling", L"ScaleText", true );
        s.m_Hud.m_ExclusiveGaugeStyle = ReadBool( L"HudScaling", L"ExclusiveGaugeStyle", true );
        s.m_Hud.m_ScaleLoadingScreen = ReadBool( L"HudScaling", L"ScaleLoadingScreen", true );
        s.m_Hud.m_SmoothLoadingScreen = ReadBool( L"HudScaling", L"SmoothLoadingScreen", true );

        s.m_Text.m_Enabled = ReadBool( L"Text", L"Enabled", true );
        {
            const int upscale = ReadInt( L"Text", L"Upscale", 0 );
            s.m_Text.m_Upscale = (upscale == 2 || upscale == 4) ? upscale : 0;
        }

        s.m_Text.m_Sharpness = std::clamp( ReadFloat( L"Text", L"Sharpness", 1.0f ), 0.25f, 4.0f );
        s.m_Detail.m_Enabled = ReadBool( L"Detail", L"Enabled", true );
        s.m_Detail.m_DrawDistanceScale = std::clamp( ReadFloat( L"Detail", L"DrawDistanceScale", 2.0f ), 1.0f, 32.0f );
        s.m_Detail.m_MinDrawDistance = std::clamp( ReadFloat( L"Detail", L"MinDrawDistance", 20000.0f ), 0.0f, 30000.0f );
        s.m_Detail.m_FarClipMultiplier = std::clamp( ReadFloat( L"Detail", L"FarClipMultiplier", 1.0f ), 1.0f, 32.0f );
        s.m_Detail.m_KeepDistantWheels = ReadBool( L"Detail", L"KeepDistantWheels", true );
        s.m_Detail.m_FullCarDetail = ReadBool( L"Detail", L"FullCarDetail", true );
        s.m_Detail.m_TextureDetail = std::clamp( ReadInt( L"Detail", L"TextureDetail", 3 ), -1, 3 );
        s.m_Detail.m_OcclusionRange = std::clamp( ReadFloat( L"Detail", L"OcclusionRange", 220.0f ), 0.0f, 20000.0f );

        s.m_Controller.m_Enabled = ReadBool( L"Controller", L"Enabled", true );
        s.m_Controller.m_HideHandledDevices = ReadBool( L"Controller", L"HideHandledDevices", true );
        s.m_Controller.m_Rumble = ReadBool( L"Controller", L"Rumble", true );
        s.m_Controller.m_RumbleStrength = std::clamp( ReadFloat( L"Controller", L"RumbleStrength", 1.0f ), 0.0f, 2.0f );
        s.m_Controller.m_StickDeadzone = std::clamp( ReadFloat( L"Controller", L"StickDeadzone", 0.10f ), 0.0f, 0.9f );
        s.m_Controller.m_TriggerDeadzone = std::clamp( ReadFloat( L"Controller", L"TriggerDeadzone", 0.05f ), 0.0f, 0.9f );
        s.m_Controller.m_BackgroundInput = ReadBool( L"Controller", L"BackgroundInput", false );
        s.m_Controller.m_TriggerBindings = ReadBool( L"Controller", L"TriggerBindings", true );

        s.m_Menu.m_Scroll = ReadBool( L"Menu", L"Scroll", true );
        s.m_Menu.m_WheelRows = ReadInt( L"Menu", L"WheelRows", 3 );
        if (s.m_Menu.m_WheelRows < 1 || s.m_Menu.m_WheelRows > 20)
            s.m_Menu.m_WheelRows = 3;
        s.m_Menu.m_WheelSelect = ReadBool( L"Menu", L"WheelSelect", true );

        s.m_Movie.m_Upscale = ReadBool( L"Movie", L"Upscale", true );
    }

    void Config::Dump() const
    {
        const Settings & s = m_Settings;

        LOG_INFO( "Config path {}", Narrow( m_Path ) );
        LOG_INFO( "  General LogLevel={} SkipCompatIfSilentPatchLoaded={}", static_cast<int>(s.m_General.m_LogLevel), s.m_General.m_SkipOnSilentPatch );
        LOG_INFO( "  Compat Enabled={} AltF4={} KeepLockKeys={} FixDeviceEnumeration={} ClearKeysOnFocusLoss={} ClipboardPaste={} ThrashDriverLeakFix={}",
            s.m_Compat.m_Enabled, s.m_Compat.m_AltF4, s.m_Compat.m_KeepLockKeys, s.m_Compat.m_FixDeviceEnumeration,
            s.m_Compat.m_ClearKeysOnFocusLoss, s.m_Compat.m_ClipboardPaste, s.m_Compat.m_ThrashDriverLeakFix );
        LOG_INFO( "  Compat FixPictureConversion={} FixScreenReadback={} SkipCdCheck={} FallbackDriver={} ThrashDriver={}",
            s.m_Compat.m_FixPictureConversion, s.m_Compat.m_FixScreenReadback, s.m_Compat.m_SkipCdCheck,
            s.m_Compat.m_FallbackDriver, s.m_Compat.m_ThrashDriver );
        LOG_INFO( "  Affinity Mode={} GameCore={} PreferPerformanceCores={}",
            static_cast<int>(s.m_Affinity.m_Mode), s.m_Affinity.m_GameCore, s.m_Affinity.m_PreferPerformanceCores );
        LOG_INFO( "  Timing QpcTimer={} HighResolutionWaitableTimer={} MaxCatchUpTicks={}",
            s.m_Timing.m_QpcTimer, s.m_Timing.m_HighResolutionWaitableTimer, s.m_Timing.m_MaxCatchUpTicks );
        LOG_INFO( "  Widescreen Enabled={} HorPlus={} FovMultiplier={:.3f} FixSpriteAspect={} UnlockResolutions={} RaceResolution={}x{} RaceColorDepth={}",
            s.m_Widescreen.m_Enabled, s.m_Widescreen.m_HorPlus, s.m_Widescreen.m_FovMultiplier,
            s.m_Widescreen.m_FixSpriteAspect, s.m_Widescreen.m_UnlockResolutions,
            s.m_Widescreen.m_RaceWidth, s.m_Widescreen.m_RaceHeight, s.m_Widescreen.m_RaceColorDepth );
        LOG_INFO( "  Framerate Enabled={} FpsLimit={} FrontendFpsLimit={} Interpolation={} InterpolateCamera={} SnapDistance={:.1f}",
            s.m_Framerate.m_Enabled, s.m_Framerate.m_FpsLimit, s.m_Framerate.m_FrontendFpsLimit,
            s.m_Framerate.m_Interpolation, s.m_Framerate.m_InterpolateCamera, s.m_Framerate.m_SnapDistance );
        LOG_INFO( "  HudScaling Enabled={} ReferenceHeight={} ScaleMultiplier={:.2f} MinimumScale={:.2f} ScaleText={} ExclusiveGaugeStyle={} ScaleLoadingScreen={} SmoothLoadingScreen={}",
            s.m_Hud.m_Enabled, s.m_Hud.m_ReferenceHeight, s.m_Hud.m_ScaleMultiplier, s.m_Hud.m_MinimumScale, s.m_Hud.m_ScaleText,
            s.m_Hud.m_ExclusiveGaugeStyle, s.m_Hud.m_ScaleLoadingScreen, s.m_Hud.m_SmoothLoadingScreen );
        LOG_INFO( "  IO Enabled={} ReadChunkKB={} CacheDiskFreeSpace={} FastCpuSpeedCheck={} GameplayRedirect={} GameplayRedirectTrace={}",
            s.m_Io.m_Enabled, s.m_Io.m_ReadChunkKB, s.m_Io.m_CacheDiskFreeSpace,
            s.m_Io.m_FastCpuSpeedCheck, s.m_Io.m_GameplayRedirect, s.m_Io.m_GameplayRedirectTrace );
        LOG_INFO( "  Detail Enabled={} DrawDistanceScale={:.2f} MinDrawDistance={:.0f} FarClipMultiplier={:.2f} KeepDistantWheels={} FullCarDetail={} TextureDetail={} OcclusionRange={:.0f} LodDistanceScale={:.2f}",
            s.m_Detail.m_Enabled, s.m_Detail.m_DrawDistanceScale, s.m_Detail.m_MinDrawDistance, s.m_Detail.m_FarClipMultiplier, s.m_Detail.m_KeepDistantWheels,
            s.m_Detail.m_FullCarDetail, s.m_Detail.m_TextureDetail, s.m_Detail.m_OcclusionRange,
            s.m_Widescreen.m_LodDistanceScale );
        LOG_INFO( "  Text Enabled={} Upscale={} Sharpness={:.2f}", s.m_Text.m_Enabled, s.m_Text.m_Upscale, s.m_Text.m_Sharpness );
        LOG_INFO( "  Controller Enabled={} HideHandledDevices={} Rumble={} RumbleStrength={:.2f} StickDeadzone={:.2f} TriggerDeadzone={:.2f} BackgroundInput={} TriggerBindings={}",
            s.m_Controller.m_Enabled, s.m_Controller.m_HideHandledDevices, s.m_Controller.m_Rumble, s.m_Controller.m_RumbleStrength,
            s.m_Controller.m_StickDeadzone, s.m_Controller.m_TriggerDeadzone, s.m_Controller.m_BackgroundInput,
            s.m_Controller.m_TriggerBindings );
        LOG_INFO( "  Menu Scroll={} WheelRows={} WheelSelect={}", s.m_Menu.m_Scroll, s.m_Menu.m_WheelRows, s.m_Menu.m_WheelSelect );
        LOG_INFO( "  Movie Upscale={}", s.m_Movie.m_Upscale );
    }
}