#pragma once

// Resolution independent race HUD.

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/hook_util.h"

namespace nfs5
{
    class HudScaling
    {
    private:

        struct FontScale
        {
            float m_X = 1.0f;
            float m_Y = 1.0f;
        };

        HudSettings m_Settings;
        std::vector<std::unique_ptr< hook::OperandPatch>> m_SizePatches;
        std::unique_ptr<safetyhook::MidHook> m_ModeWriteHook;
        std::unique_ptr<safetyhook::MidHook> m_ModeWriteAltHook;
        std::unique_ptr<safetyhook::MidHook> m_TextBeginHook;
        std::unique_ptr<safetyhook::MidHook> m_TextEndHook;
        std::unique_ptr<safetyhook::InlineHook> m_LayoutInitHook;
        hook::CallSitePatch m_PreviewWidthPatch;
        hook::CallSitePatch m_PreviewHeightPatch;
        hook::CallSitePatch m_DigitalSpeedPatch;
        hook::CallSitePatch m_DigitalRpmPatch;

        // Scale the game itself last gave each font, the HUD only sets it when it is not 1.
        std::unordered_map<void *, FontScale> m_GameFontScale;
        void * m_ScaledFont = nullptr;
        FontScale m_ScaledFontGameScale;
        float m_FactorX = 1.0f;
        float m_FactorY = 1.0f;
        std::int32_t m_RealWidth = 0;
        std::int32_t m_RealHeight = 0;
        bool m_Installed = false;

        std::unique_ptr<safetyhook::MidHook> m_LapTextBeginHook;
        std::unique_ptr<safetyhook::MidHook> m_LapTextEndHook;
        void * m_LapFont = nullptr;
        FontScale m_LapFontScale;
        static void LapTextBegin( safetyhook::Context & context );
        static void LapTextEnd( safetyhook::Context & context );

        HudScaling() = default;

        static void ModeWriteMidHook(
            safetyhook::Context & context
        );

        static void TextBeginMidHook(
            safetyhook::Context & context
        );

        static void TextEndMidHook(
            safetyhook::Context & context
        );

        static int __cdecl LayoutInitHook(
            const char * name,
            int layout_set,
            int width,
            int height
        );

        // Virtual layout size for a race screen size, the 'RaceResolution' override wins.
        void VirtualizeRaceSize(
            std::int32_t width,
            std::int32_t height,
            std::int32_t & virtual_width,
            std::int32_t & virtual_height
        ) const;

        static int __fastcall PreviewWidthHook(
            void * accessor,
            void * unused_edx
        );

        static int __fastcall PreviewHeightHook(
            void * accessor,
            void * unused_edx
        );

        // Virtual HUD screen for a real resolution.
        void Virtualize(
            std::int32_t width,
            std::int32_t height,
            std::int32_t & virtual_width,
            std::int32_t & virtual_height
        ) const;

        static bool __cdecl IsDigitalSpeedShown();

        static bool __cdecl IsDigitalRpmShown();

        // Recomputes the virtual screen from the real one, called after every mode change.
        void Update();

        void SetFontScale(
            void * font,
            float x,
            float y
        ) const;

    public:

        HudScaling( const HudScaling & ) = delete;
        HudScaling & operator=( const HudScaling & ) = delete;

        static HudScaling & GetInstance();

        bool Install(
            const HudSettings & settings
        );

        void Uninstall();
    };
}