#include "hud_scaling.h"

#include <algorithm>
#include <cmath>

#include "../../core/log.h"
#include "../../game/addresses.h"

#include "loading_screen.h"

#include "../widescreen/widescreen.h"

namespace nfs5
{
    namespace
    {
        // The HUD code reads these instead of 'SCREEN_WIDTH' and 'SCREEN_HEIGHT'.
        // They need fixed addresses, the patched instructions hold them as absolute operands.
        alignas(4) std::int32_t g_VirtualWidth = 0;
        alignas(4) std::int32_t g_VirtualHeight = 0;

        #if defined(_MSC_VER)
        #define THISCALL __thiscall
        #elif defined(__GNUC__) || defined(__clang__)
        #define THISCALL __attribute__((thiscall))
        #else
        #define THISCALL
        #endif

        using FontSetScaleFn = void(THISCALL *)(
            void * font,
            float x,
            float y
            );

        std::int32_t LayoutField(
            std::size_t offset
        )
        {
            const auto * table = hook::Ref<const std::uint8_t * const * const *>( game::hud::LAYOUT_TABLE );
            const std::int32_t index = hook::Ref<std::int32_t>( game::hud::LAYOUT_INDEX );

            if (table == nullptr || *table == nullptr || index < 0)
                return 0;

            const std::uint8_t * record = (*table)[index];
            return record != nullptr ? *reinterpret_cast<const std::int32_t *>( record + offset ) : 0;
        }


        bool IsOne(
            float value
        )
        {
            return std::abs( value - 1.0f ) < 1e-6f;
        }
    }

    HudScaling & HudScaling::GetInstance()
    {
        static HudScaling instance;
        return instance;
    }

    void HudScaling::Update()
    {
        const std::int32_t real_width = hook::Ref<std::int32_t>( game::var::SCREEN_WIDTH );
        const std::int32_t real_height = hook::Ref<std::int32_t>( game::var::SCREEN_HEIGHT );

        if (real_width == m_RealWidth && real_height == m_RealHeight && g_VirtualWidth != 0)
            return;

        m_RealWidth = real_width;
        m_RealHeight = real_height;

        if (real_width <= 0 || real_height <= 0)
        {
            g_VirtualWidth = real_width;
            g_VirtualHeight = real_height;
            m_FactorX = 1.0f;
            m_FactorY = 1.0f;
            return;
        }

        Virtualize( real_width, real_height, g_VirtualWidth, g_VirtualHeight );
        m_FactorX = static_cast<float>(real_width) / static_cast<float>(g_VirtualWidth);
        m_FactorY = static_cast<float>(real_height) / static_cast<float>(g_VirtualHeight);

        LOG_INFO( "HUD screen {}x{} drawn as {}x{}, scale {:.3f} x {:.3f}", real_width, real_height, g_VirtualWidth, g_VirtualHeight, m_FactorX,
            m_FactorY );
    }


    void HudScaling::Virtualize(
        std::int32_t width,
        std::int32_t height,
        std::int32_t & virtual_width,
        std::int32_t & virtual_height
    ) const
    {
        if (width <= 0 || height <= 0)
        {
            virtual_width = width;
            virtual_height = height;
            return;
        }

        // One scale for both axes keeps the HUD art in proportion, the extra width of a
        // wide screen becomes extra virtual width and the edge anchoring uses it.
        float scale = static_cast<float>(height) / static_cast<float>(m_Settings.m_ReferenceHeight) * m_Settings.m_ScaleMultiplier;
        scale = std::max( scale, m_Settings.m_MinimumScale );
        virtual_width = std::max( 1, static_cast<std::int32_t>(std::lround( static_cast<float>(width) / scale )) );
        virtual_height = std::max( 1, static_cast<std::int32_t>(std::lround( static_cast<float>(height) / scale )) );
    }


    void HudScaling::VirtualizeRaceSize(
        std::int32_t width,
        std::int32_t height,
        std::int32_t & virtual_width,
        std::int32_t & virtual_height
    ) const
    {
        int effective_width = width;
        int effective_height = height;

        if (!Widescreen::GetInstance().GetRaceResolutionOverride( effective_width, effective_height ))
        {
            effective_width = width;
            effective_height = height;
        }

        Virtualize( effective_width, effective_height, virtual_width, virtual_height );
    }

    namespace
    {
        using ResolutionFn = int( __fastcall * )(
            void * accessor,
            void * unused_edx
            );

        void ReadSelectedResolution(
            void * accessor,
            std::int32_t & width,
            std::int32_t & height
        )
        {
            width = reinterpret_cast<ResolutionFn>(game::hud::RESOLUTION_WIDTH.Get())(accessor, nullptr);
            height = reinterpret_cast<ResolutionFn>(game::hud::RESOLUTION_HEIGHT.Get())(accessor, nullptr);
        }
    }


    int __fastcall HudScaling::PreviewWidthHook(
        void * accessor,
        void *
    )
    {
        std::int32_t width = 0;
        std::int32_t height = 0;
        ReadSelectedResolution( accessor, width, height );

        std::int32_t virtual_width = width;
        std::int32_t virtual_height = height;
        GetInstance().VirtualizeRaceSize( width, height, virtual_width, virtual_height );
        return virtual_width;
    }


    int __fastcall HudScaling::PreviewHeightHook(
        void * accessor,
        void *
    )
    {
        std::int32_t width = 0;
        std::int32_t height = 0;
        ReadSelectedResolution( accessor, width, height );

        std::int32_t virtual_width = width;
        std::int32_t virtual_height = height;
        GetInstance().VirtualizeRaceSize( width, height, virtual_width, virtual_height );
        return virtual_height;
    }


    int __cdecl HudScaling::LayoutInitHook(
        const char * name,
        int layout_set,
        int width,
        int height
    )
    {
        HudScaling & self = GetInstance();

        // The race passes the virtual size it reads through the redirected globals, the
        // 'Options -> Display' page passes the selected race screen size. Both are mapped to the
        // virtual size of the resolution the race really runs at, so the page edits the
        // same layout record the race uses
        std::int32_t virtual_width = width;
        std::int32_t virtual_height = height;

        self.VirtualizeRaceSize( width, height, virtual_width, virtual_height );

        if (virtual_width != width || virtual_height != height)
        {
            LOG_INFO( "HUD layout {}x{} requested, using the {}x{} record", width, height, virtual_width, virtual_height );
        }

        return self.m_LayoutInitHook->ccall< int >( name, layout_set, virtual_width, virtual_height );
    }


    void HudScaling::ModeWriteMidHook(
        safetyhook::Context &
    )
    {
        GetInstance().Update();
    }


    void HudScaling::SetFontScale(
        void * font,
        float x,
        float y
    ) const
    {
        const auto set_scale = reinterpret_cast<FontSetScaleFn>(game::hud::FONT_SET_SCALE.Get());
        set_scale( font, x, y );
    }


    bool __cdecl HudScaling::IsDigitalSpeedShown()
    {
        return LayoutField( game::hud::LAYOUT_SPEEDOMETER ) == game::hud::GAUGE_DIGITAL;
    }


    bool __cdecl HudScaling::IsDigitalRpmShown()
    {
        return LayoutField( game::hud::LAYOUT_TACHOMETER ) == game::hud::GAUGE_DIGITAL;
    }


    void HudScaling::TextBeginMidHook(
        safetyhook::Context & context
    )
    {
        HudScaling & self = GetInstance();

        if (IsOne( self.m_FactorX ) && IsOne( self.m_FactorY ))
            return;

        const auto * entry = reinterpret_cast<const std::uint8_t *>(context.esi);
        const auto * holder = reinterpret_cast<const std::uint8_t *>(context.edi);
        void * font = *reinterpret_cast<void * const *>(holder + 4);

        if (font == nullptr)
            return;

        // The flush only calls set scale when the entry scale differs from 1, the font
        // then keeps whatever it had before, so the last known game value is used.
        const float entry_x = *reinterpret_cast<const float *>(entry + 0x18);
        const float entry_y = *reinterpret_cast<const float *>(entry + 0x1C);
        FontScale & game_scale = self.m_GameFontScale[font];

        if (!IsOne( entry_x ) || !IsOne( entry_y ))
        {
            game_scale = { entry_x, entry_y };
        }

        self.SetFontScale( font, game_scale.m_X * self.m_FactorX, game_scale.m_Y * self.m_FactorY );
        self.m_ScaledFont = font;
        self.m_ScaledFontGameScale = game_scale;
    }


    void HudScaling::TextEndMidHook(
        safetyhook::Context &
    )
    {
        HudScaling & self = GetInstance();

        if (self.m_ScaledFont == nullptr)
            return;

        // Fonts are shared with the front end, leave them as the game set them.
        self.SetFontScale( self.m_ScaledFont, self.m_ScaledFontGameScale.m_X, self.m_ScaledFontGameScale.m_Y );
        self.m_ScaledFont = nullptr;
    }


    bool HudScaling::Install(
        const HudSettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "HUD scaling disabled by config" );
            return true;
        }

        // Independent of the HUD redirects, it only touches the loading screen blit.
        LoadingScreen::GetInstance().Install( settings );

        if (settings.m_ExclusiveGaugeStyle)
        {
            namespace hud_sites = game::hud;
            if (hook::MatchBytes( hud_sites::DIGITAL_SPEED_CHECK.m_Address, hud_sites::DIGITAL_SPEED_CHECK.m_Signature ) &&
                hook::MatchBytes( hud_sites::DIGITAL_RPM_CHECK.m_Address, hud_sites::DIGITAL_RPM_CHECK.m_Signature ))
            {
                m_DigitalSpeedPatch.Apply( "DigitalSpeedCheck", hud_sites::DIGITAL_SPEED_CHECK.m_Address, reinterpret_cast<void *>(&IsDigitalSpeedShown) );
                m_DigitalRpmPatch.Apply( "DigitalRpmCheck", hud_sites::DIGITAL_RPM_CHECK.m_Address, reinterpret_cast<void *>(&IsDigitalRpmShown) );
            }
            else
            {
                LOG_ERROR( "Digital gauge checks do not match, readouts keep the game behaviour" );
            }
        }

        namespace hud = game::hud;

        // All reads are checked before any is changed, a partial redirect would mix two sizes.
        for (const hud::SizeRead & read : hud::SIZE_READS)
        {
            if (!hook::MatchBytes( read.m_Address, read.m_Signature ))
            {
                LOG_ERROR( "HUD size read at {:08X} does not match, HUD scaling stays off", read.m_Address.Get() );
                return false;
            }
        }

        if (!hook::MatchBytes( hud::FONT_SET_SCALE, hud::FONT_SET_SCALE_SIG ))
        {
            LOG_ERROR( "Font set scale does not match, HUD scaling stays off" );
            return false;
        }

        Update();

        m_LayoutInitHook = hook::CreateInline( "HudLayoutInit", hud::LAYOUT_INIT.m_Address, hud::LAYOUT_INIT.m_Signature,
            reinterpret_cast<void *>(&LayoutInitHook) );

        // The 'Options -> Display' preview must scale the same virtual layout the race uses.
        if (hook::MatchBytes( hud::PREVIEW_WIDTH_CALL.m_Address, hud::PREVIEW_WIDTH_CALL.m_Signature ) &&
            hook::MatchBytes( hud::PREVIEW_HEIGHT_CALL.m_Address, hud::PREVIEW_HEIGHT_CALL.m_Signature ))
        {
            m_PreviewWidthPatch.Apply( "HudPreviewWidth", hud::PREVIEW_WIDTH_CALL.m_Address, reinterpret_cast<void *>(&PreviewWidthHook) );
            m_PreviewHeightPatch.Apply( "HudPreviewHeight", hud::PREVIEW_HEIGHT_CALL.m_Address, reinterpret_cast<void *>(&PreviewHeightHook) );
        }
        else
        {
            LOG_WARN( "Options Display preview calls do not match, the preview keeps the game scaling" );
        }

        m_ModeWriteHook = hook::CreateMid( "HudModeWrite", hud::MODE_WRITE.m_Address, hud::MODE_WRITE.m_Signature, &ModeWriteMidHook );
        m_ModeWriteAltHook = hook::CreateMid( "HudModeWriteAlt", hud::MODE_WRITE_ALT.m_Address, hud::MODE_WRITE_ALT.m_Signature, &ModeWriteMidHook );

        if (m_ModeWriteHook == nullptr || m_ModeWriteAltHook == nullptr)
        {
            LOG_ERROR( "HUD scaling mode hooks failed, HUD scaling stays off" );
            Uninstall();
            return false;
        }

        m_SizePatches.reserve( std::size( hud::SIZE_READS ) );

        for (const hud::SizeRead & read : hud::SIZE_READS)
        {
            auto patch = std::make_unique< hook::OperandPatch >();
            const std::int32_t * target = read.m_Axis == hud::SizeAxis::WIDTH ? &g_VirtualWidth : &g_VirtualHeight;

            if (!patch->Apply( "HudSize", read.m_Address, read.m_Signature, read.m_OperandOffset,
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) ))
            {
                Uninstall();
                return false;
            }

            m_SizePatches.push_back( std::move( patch ) );
        }

        if (settings.m_ScaleText)
        {
            m_TextEndHook = hook::CreateMid( "HudTextEnd", hud::TEXT_END.m_Address, hud::TEXT_END.m_Signature, &TextEndMidHook );

            if (m_TextEndHook != nullptr)
            {
                m_TextBeginHook = hook::CreateMid( "HudTextBegin", hud::TEXT_BEGIN.m_Address, hud::TEXT_BEGIN.m_Signature, &TextBeginMidHook );
            }

            if (m_TextBeginHook == nullptr)
            {
                LOG_WARN( "HUD text scaling hooks failed, text keeps its native size" );
                m_TextEndHook.reset();
            }
        }

        m_Installed = true;

        LOG_INFO( "HUD scaling installed, {} size reads redirected, reference height {} multiplier {:.2f} text {}", m_SizePatches.size(),
            settings.m_ReferenceHeight, settings.m_ScaleMultiplier, m_TextBeginHook != nullptr );
        return true;
    }


    void HudScaling::Uninstall()
    {
        m_DigitalSpeedPatch.Restore();
        m_DigitalRpmPatch.Restore();
        LoadingScreen::GetInstance().Uninstall();
        m_TextBeginHook.reset();
        m_TextEndHook.reset();

        for (auto & patch : m_SizePatches)
        {
            patch->Restore();
        }

        m_SizePatches.clear();
        m_ModeWriteHook.reset();
        m_ModeWriteAltHook.reset();
        m_LayoutInitHook.reset();
        m_PreviewWidthPatch.Restore();
        m_PreviewHeightPatch.Restore();
        m_Installed = false;
    }
}