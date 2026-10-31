#include "detail.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../core/log.h"
#include "../../game/addresses.h"
#include "../../core/win_handle.h"

namespace nfs5
{
    namespace
    {
        // Wheel LOD per detail level and body LOD, 3 rows of 7, LOD 5 and 6 map to themselves.
        constexpr std::array< std::int32_t, 21 > STOCK_WHEEL_TABLE =
        {
            0, 0, 1, 1, 1, 5, 6,
            0, 0, 0, 0, 1, 5, 6,
            0, 0, 0, 0, 1, 5, 6
        };

        constexpr float STOCK_FAR_CLIP = 700.0f;

        // Depth precision with the 0.15 near plane gets poor beyond this.
        constexpr float MAX_FAR_CLIP = 20000.0f;

        // Objects are kept while their nearest point is within the draw distance, the far
        // plane leaves room for the rest of a large piece of scenery behind it.
        constexpr float FAR_CLIP_MARGIN = 1.5f;

        // Squared fixed object limits the culling reads instead of '360000' and '490000', updated
        // with every draw distance change.
        float s_ObjectLimit400000 = 360000.0f;
        float s_ObjectLimit20 = 490000.0f;
    }

    Detail & Detail::GetInstance()
    {
        static Detail instance;
        return instance;
    }

    double __cdecl Detail::DrawDistanceHook(
        float distance
    )
    {
        Detail & self = GetInstance();

        // The original multiplies by the view distance option and keeps at least 50.
        const float scale = self.m_Settings.m_DrawDistanceScale;
        const float scaled = std::max( distance * scale, self.m_Settings.m_MinDrawDistance );
        const double result = self.m_DrawDistanceHook->ccall< double >( scaled );

        // The two object classes with fixed limits scale along and never end before the rest,
        // otherwise ground and walls go missing under buildings that are still drawn.
        const float drawn = static_cast<float>(result);
        const float limit_400000 = std::max( game::detail::OBJECT_LIMIT_400000_STOCK * scale, drawn );
        const float limit_20 = std::max( game::detail::OBJECT_LIMIT_20_STOCK * scale, drawn );
        s_ObjectLimit400000 = limit_400000 * limit_400000;
        s_ObjectLimit20 = limit_20 * limit_20;

        return result;
    }


    int __cdecl Detail::TextureDetailHook()
    {
        Detail & self = GetInstance();
        const int detected = self.m_TextureDetailHook->ccall<int>();

        LOG_INFO( "Texture detail detected {} used {}", detected, self.m_Settings.m_TextureDetail );
        return self.m_Settings.m_TextureDetail;
    }


    void Detail::CarLodOffsetHook(
        safetyhook::Context & context
    )
    {
        // First usable LOD, the distance based choice still picks simpler models further away.
        context.ebx = 0;
    }


    void Detail::LampGlowDepthHook(
        safetyhook::Context & context
    )
    {
        // Keep the glow at least one step in front of the lamp lens at any distance.
        auto & bias = *reinterpret_cast<std::int32_t *>(context.ebp + game::detail::LAMP_GLOW_DEPTH_BIAS_EBP);
        bias = std::min( bias, -1 );
    }


    void Detail::OcclusionHook(
        safetyhook::Context & context
    )
    {
        // Nothing to change when the wall test let the object through.
        if ((context.eax & 0xFF) == 0)
            return;

        const auto * sphere = reinterpret_cast<const float *>(context.esp + game::detail::OCCLUSION_SPHERE);
        const auto * camera = *reinterpret_cast<const float * const *>(context.esp + game::detail::OCCLUSION_CAMERA);

        const float dx = sphere[0] - camera[0];
        const float dy = sphere[1] - camera[1];
        const float dz = sphere[2] - camera[2];
        const float gap = std::sqrt( dx * dx + dy * dy + dz * dz );

        // Close by the walls only hide what the depth buffer hides anyway. Far away they still
        // drop track pieces whose surrounding ground is not drawn, those would float in the air.
        if (gap < GetInstance().m_Settings.m_OcclusionRange)
        {
            context.eax &= ~0xFFu;
        }
    }


    bool Detail::PatchWheelTable()
    {
        auto * table = hook::Ptr<std::int32_t>( game::detail::WHEEL_LOD_TABLE );

        if (std::memcmp( table, STOCK_WHEEL_TABLE.data(), sizeof( STOCK_WHEEL_TABLE ) ) != 0)
        {
            LOG_ERROR( "Wheel LOD table does not match, distant cars keep the game wheels" );
            return false;
        }

        ScopedPageProtection protection( table, sizeof( STOCK_WHEEL_TABLE ) );
        std::memcpy( m_OriginalWheelTable.data(), table, sizeof( STOCK_WHEEL_TABLE ) );

        // The two farthest body LODs take the wheels of LOD 1, wheels are placed per wheel
        // transform and not by the body mesh.
        for (std::size_t i = 0; i < STOCK_WHEEL_TABLE.size(); ++i)
        {
            if (table[i] >= 5)
            {
                table[i] = 1;
            }
        }

        m_WheelTablePatched = true;
        return true;
    }


    void Detail::RestoreWheelTable()
    {
        if (!m_WheelTablePatched)
            return;

        auto * table = hook::Ptr<std::int32_t>( game::detail::WHEEL_LOD_TABLE );

        ScopedPageProtection protection( table, sizeof( STOCK_WHEEL_TABLE ) );
        std::memcpy( table, m_OriginalWheelTable.data(), sizeof( STOCK_WHEEL_TABLE ) );

        m_WheelTablePatched = false;
    }


    bool Detail::Install(
        const DetailSettings & settings
    )
    {
        if (m_Installed)
        {
            return true;
        }

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "Detail subsystem disabled by config" );
            return true;
        }

        namespace site = game::detail;
        bool ok = true;

        const bool extend = settings.m_DrawDistanceScale > 1.0f || settings.m_MinDrawDistance > 0.0f;

        if (extend)
        {
            m_DrawDistanceHook = hook::CreateInline( "DrawDistance", site::DRAW_DISTANCE.m_Address, site::DRAW_DISTANCE.m_Signature,
                reinterpret_cast<void *>(&DrawDistanceHook) );
            ok &= m_DrawDistanceHook != nullptr;
        }

        // Both fixed object limits read the values the draw distance hook keeps up to date.
        if (m_DrawDistanceHook)
        {
            const auto limit_400000 = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&s_ObjectLimit400000));

            ok &= m_ObjectLimitPatch400000.Apply( "ObjectLimit400000", site::OBJECT_LIMIT_400000.m_Address, site::OBJECT_LIMIT_400000.m_Signature,
                site::OBJECT_LIMIT_OPERAND, limit_400000 );
            // Preserve the stock distance for object flag 0x20; extending it exposes floating scenery.
        }

        // The far plane has to cover the longest distance anything is drawn at, with room for
        // large pieces that start inside it. Track data stays within the stock '700' plane.
        const float longest = std::max( STOCK_FAR_CLIP * std::max( 1.0f, settings.m_DrawDistanceScale ), settings.m_MinDrawDistance );
        const float far_clip = extend ? std::clamp( longest * FAR_CLIP_MARGIN * settings.m_FarClipMultiplier, STOCK_FAR_CLIP, MAX_FAR_CLIP ) :
            STOCK_FAR_CLIP;

        if (far_clip > STOCK_FAR_CLIP)
        {
            std::uint32_t bits = 0;
            std::memcpy( &bits, &far_clip, sizeof( bits ) );

            ok &= m_FarClipPatch.Apply( "FarClip", site::FAR_CLIP.m_Address, site::FAR_CLIP.m_Signature, site::FAR_CLIP_OPERAND, bits );
            ok &= m_FarClipResetPatch.Apply( "FarClipReset", site::FAR_CLIP_RESET.m_Address, site::FAR_CLIP_RESET.m_Signature,
                site::FAR_CLIP_RESET_OPERAND, bits );
        }

        if (settings.m_KeepDistantWheels)
        {
            ok &= PatchWheelTable();
        }

        if (settings.m_TextureDetail >= 0)
        {
            m_TextureDetailHook = hook::CreateInline( "TextureDetail", site::TEXTURE_DETAIL_DETECT.m_Address,
                site::TEXTURE_DETAIL_DETECT.m_Signature, reinterpret_cast<void *>(&TextureDetailHook) );
            ok &= m_TextureDetailHook != nullptr;
        }

        if (settings.m_OcclusionRange > 0.0f)
        {
            m_OcclusionHook = hook::CreateMid( "Occlusion", site::OCCLUSION_RESULT.m_Address, site::OCCLUSION_RESULT.m_Signature,
                &OcclusionHook );
            ok &= m_OcclusionHook != nullptr;
        }

        if (settings.m_FullCarDetail)
        {
            m_CarLodOffsetHook = hook::CreateMid( "CarLodOffset", site::CAR_LOD_OFFSET.m_Address, site::CAR_LOD_OFFSET.m_Signature,
                &CarLodOffsetHook );
            ok &= m_CarLodOffsetHook != nullptr;

            // LOD 0 never gets the suspension travel, the wheels would roll with the body into the ground.
            ok &= m_WheelSuspensionPatch.Apply( "WheelSuspensionLoop", site::WHEEL_SUSPENSION_LOOP.m_Address,
                site::WHEEL_SUSPENSION_LOOP.m_Signature, site::WHEEL_SUSPENSION_LOOP_JL );
        }

        // Lamp glows turn partly black from about 90 m on, also in the stock game.
        m_LampGlowDepthHook = hook::CreateMid( "LampGlowDepth", site::LAMP_GLOW_DEPTH_BIAS.m_Address, site::LAMP_GLOW_DEPTH_BIAS.m_Signature,
            &LampGlowDepthHook );
        ok &= m_LampGlowDepthHook != nullptr;

        m_Installed = true;

        LOG_INFO( "Detail installed, draw distance x{:.2f} at least {:.0f} far clip {:.0f} distant wheels {} occlusion from {:.0f} all ok {}",
            settings.m_DrawDistanceScale, settings.m_MinDrawDistance, far_clip, m_WheelTablePatched, settings.m_OcclusionRange, ok );
        return ok;
    }


    void Detail::Uninstall()
    {
        RestoreWheelTable();

        m_LampGlowDepthHook.reset();
        m_OcclusionHook.reset();
        m_WheelSuspensionPatch.Restore();
        m_CarLodOffsetHook.reset();
        m_TextureDetailHook.reset();
        m_ObjectLimitPatch20.Restore();
        m_ObjectLimitPatch400000.Restore();
        m_FarClipResetPatch.Restore();
        m_FarClipPatch.Restore();
        m_DrawDistanceHook.reset();
        m_Installed = false;
    }
}