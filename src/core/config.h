#pragma once

// Configuration for every subsystem.

#include <cstdint>
#include <string>

#include "log.h"

namespace nfs5
{
    enum class AffinityMode : std::uint8_t
    {
        OFF = 0,
        GAME_THREADS = 1,
        WHOLE_PROCESS = 2
    };

    struct GeneralSettings
    {
        LogLevel m_LogLevel = LogLevel::INFO;
        bool m_SkipOnSilentPatch = true;
    };

    struct CompatSettings
    {
        bool m_Enabled = true;
        bool m_AltF4 = true;
        bool m_KeepLockKeys = true;
        bool m_FixDeviceEnumeration = true;
        bool m_ClearKeysOnFocusLoss = true;
        bool m_ClipboardPaste = true;
        bool m_ThrashDriverLeakFix = true;
        bool m_FixPictureConversion = true;

        // 'THRASH_readrect' has no bounds check, the sun glare test reads 4x4 pixels around
        // the projected sun and runs off the frame when the sun is at the screen edge.
        bool m_FixScreenReadback = true;

        // Start without the game CD, applied even when 'SilentPatchNFS90s' is loaded.
        bool m_SkipCdCheck = true;

        // Stock THRASH driver loaded when the one named in the registry fails, empty off.
        std::string m_FallbackDriver = "dx7z";

        // Registry value Thrash Driver as the game sees it, auto replaces a driver that does
        // not load with dx, registry passes the stored value, anything else is used as is.
        std::string m_ThrashDriver = "dx11";
    };

    struct AffinitySettings
    {
        AffinityMode m_Mode = AffinityMode::GAME_THREADS;
        int m_GameCore = -1;
        bool m_PreferPerformanceCores = true;
    };

    struct TimingSettings
    {
        bool m_QpcTimer = true;
        bool m_HighResolutionWaitableTimer = true;
        int m_MaxCatchUpTicks = 128;
    };

    struct WidescreenSettings
    {
        bool m_Enabled = true;
        bool m_HorPlus = true;
        float m_FovMultiplier = 1.0f;
        bool m_FixSpriteAspect = true;
        bool m_UnlockResolutions = true;

        // '0' keeps the game setting, '-1 uses the desktop size.
        int m_RaceWidth = 0;
        int m_RaceHeight = 0;

        // '0' keeps the game setting.
        int m_RaceColorDepth = 0;

        // Car detail distance factor, the Hor+ fov change is compensated on top, '0' disables.
        float m_LodDistanceScale = 4.0f;
    };

    struct FramerateSettings
    {
        bool m_Enabled = true;
        int m_FpsLimit = 0;
        int m_FrontendFpsLimit = 0;
        bool m_Interpolation = true;
        bool m_InterpolateCamera = true;

        // Distance in metres above which a change is treated as a teleport and not blended.
        float m_SnapDistance = 25.0f;
    };

    struct IoSettings
    {
        bool m_Enabled = true;
        int m_ReadChunkKB = 1024;
        bool m_CacheDiskFreeSpace = true;
        bool m_FastCpuSpeedCheck = true;
        bool m_GameplayRedirect = false;
        bool m_GameplayRedirectTrace = false;
    };

    struct HudSettings
    {
        bool m_Enabled = true;

        // Layout height the HUD is authored for, the HUD is scaled by real height.
        int m_ReferenceHeight = 480;

        // Extra factor on top of the automatic scale, '1.0' keeps the 640x480 proportions.
        float m_ScaleMultiplier = 1.0f;

        // Never shrink below the original pixel size.
        float m_MinimumScale = 1.0f;

        // Scale text glyphs with the rest of the HUD.
        bool m_ScaleText = true;

        // Speedometer and tachometer set to analog show only the gauge, the game also
        // draws the digital value inside it.
        bool m_ExclusiveGaugeStyle = true;

        // Fit the race loading screen to the display height instead of a 640x480 box.
        bool m_ScaleLoadingScreen = true;

        // Smooth the enlarged loading screen, off keeps hard pixel edges.
        bool m_SmoothLoadingScreen = true;
    };

    struct TextSettings
    {
        bool m_Enabled = true;

        // Glyph sheet magnification, '0' picks '2' or '4' from the desktop height.
        int m_Upscale = 0;

        // Edge sharpness of the enlarged glyphs, '1' is about one screen pixel of smoothing.
        float m_Sharpness = 1.0f;
    };

    struct DetailSettings
    {
        bool m_Enabled = true;

        // Multiplier on the track draw distance, applied on top of the view distance option.
        float m_DrawDistanceScale = 2.0f;

        // Draw distance in metres every track position gets at least, '0' follows the track data.
        float m_MinDrawDistance = 20000.0f;

        // Extra factor on the far clip plane after it follows the draw distance.
        float m_FarClipMultiplier = 1.0f;

        // Distant cars use the LOD 1 wheels instead of none.
        bool m_KeepDistantWheels = true;

        // Cars close to the camera use the full detail model the garage shows.
        bool m_FullCarDetail = true;

        // Texture quality tier '0' to '3', '-1' keeps the game hardware detection.
        int m_TextureDetail = 3;

        // Objects closer than this [distance minus radius, metres] are drawn even when the
        // occlusion test puts them behind a wall, '0' keeps the test everywhere.
        float m_OcclusionRange = 220.0f;
    };

    struct ControllerSettings
    {
        // SDL3 gamepad shown to the game as one DirectInput controller that is always present.
        bool m_Enabled = true;

        // DirectInput copies of controllers SDL already reads are not listed, so a pad is not
        // seen twice and its bindings never point at a device that went away.
        bool m_HideHandledDevices = true;

        // Force feedback of the game played as rumble.
        bool m_Rumble = true;
        float m_RumbleStrength = 1.0f;

        // Radial dead zone of the sticks and dead zone of the triggers, '0' to '0.9'.
        float m_StickDeadzone = 0.10f;
        float m_TriggerDeadzone = 0.05f;

        // Keep reading the pad while another program is active.
        bool m_BackgroundInput = false;

        // Default modern bindings of the gamepad.
        bool m_TriggerBindings = true;
    };

    // Front end lists scrolled by the mouse wheel and the arrow keys.
    struct MenuSettings
    {
        bool m_Scroll = true;

        // Rows per wheel notch, '1' to '20'.
        int m_WheelRows = 3;

        // Wheel over a closed drop down list picks the next or previous entry.
        bool m_WheelSelect = true;
    };

    // Intro and menu movies.
    struct MovieSettings
    {
        // Hands the frames of the movie player to the graphics driver, which enlarges them
        // itself. The stock driver has no such function, the game then keeps scaling them.
        bool m_Upscale = true;
    };

    struct Settings
    {
        GeneralSettings m_General;
        CompatSettings m_Compat;
        AffinitySettings m_Affinity;
        TimingSettings m_Timing;
        WidescreenSettings m_Widescreen;
        FramerateSettings m_Framerate;
        IoSettings m_Io;
        HudSettings m_Hud;
        DetailSettings m_Detail;
        TextSettings m_Text;
        ControllerSettings m_Controller;
        MenuSettings m_Menu;
        MovieSettings m_Movie;
    };

    class Config
    {
    private:

        std::wstring m_Path;
        Settings m_Settings;

        Config() = default;

        [[nodiscard]] bool ReadBool(
            const wchar_t * section,
            const wchar_t * key,
            bool fallback
        ) const;

        [[nodiscard]] int ReadInt(
            const wchar_t * section,
            const wchar_t * key,
            int fallback
        ) const;

        [[nodiscard]] std::wstring ReadString(
            const wchar_t * section,
            const wchar_t * key,
            const wchar_t * fallback
        ) const;

        [[nodiscard]] float ReadFloat(
            const wchar_t * section,
            const wchar_t * key,
            float fallback
        ) const;

    public:

        Config( const Config & ) = delete;
        Config & operator=( const Config & ) = delete;

        static Config & GetInstance();

        void Load( const std::wstring & path );

        void Dump() const;

        [[nodiscard]] const Settings & Get() const
        {
            return m_Settings;
        }

        [[nodiscard]] const std::wstring & GetPath() const
        {
            return m_Path;
        }
    };
}