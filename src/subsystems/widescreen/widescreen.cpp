#include "widescreen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#if defined( _MSC_VER )
#include <intrin.h>
#define NFS5_RETURN_ADDRESS() reinterpret_cast< std::uintptr_t >( _ReturnAddress() )
#else
#define NFS5_RETURN_ADDRESS() reinterpret_cast< std::uintptr_t >( __builtin_return_address( 0 ) )
#endif

#include "../../core/log.h"
#include "../../game/addresses.h"

namespace nfs5
{
    namespace
    {
        constexpr float BASE_ASPECT = 4.0f / 3.0f;
        constexpr float DEG_TO_RAD = std::numbers::pi_v< float > / 180.0f;
        constexpr float RAD_TO_DEG = 180.0f / std::numbers::pi_v<float>;
        constexpr float MAX_FOV_DEGREES = 170.0f;
        constexpr float MAX_CULL_HALF_ANGLE = 1.53f;

        // Operand target of fmul in the flare builder, starts as the original '1 / 640'.
        float s_FlareWidthScale = 1.0f / 640.0f;

        // Replaces the 640.0 the car LOD size is normalised with, larger keeps detail further out.
        float s_LodSizeScale = 640.0f;

        // The screen size is saved as the mode itself instead of its list position, which moves
        // whenever the list changes. Bit 30 marks it, bit 28 is 32 bit, width and height take
        // 14 bits each. Plain positions from older saves are still read as positions.
        constexpr std::uint32_t SAVED_MODE_MARK = 0x40000000;
        constexpr std::uint32_t SAVED_MODE_MASK = 0xE0000000;
        constexpr std::uint32_t SAVED_MODE_32 = 0x10000000;
        constexpr std::uint32_t SAVED_MODE_SIZE = 0x3FFF;

        [[nodiscard]] bool SameDepth(
            int first,
            int second
        )
        {
            return (first > 16) == (second > 16);
        }


        // The registry mode is the real desktop, the current mode may be one emulated by
        // DDrawCompat for the 640x480 front end.
        bool GetDesktopSize(
            int & width,
            int & height
        )
        {
            DEVMODEW mode = {};
            mode.dmSize = sizeof( mode );

            if (::EnumDisplaySettingsW( nullptr, ENUM_REGISTRY_SETTINGS, &mode ) != FALSE && mode.dmPelsWidth >= 640)
            {
                width = static_cast<int>(mode.dmPelsWidth);
                height = static_cast<int>(mode.dmPelsHeight);
            }
            else
            {
                width = ::GetSystemMetrics( SM_CXSCREEN );
                height = ::GetSystemMetrics( SM_CYSCREEN );
            }

            return width >= 640 && height >= 480;
        }

        // 'THRASH_about' returns the driver info block, +3Ch mode count, +40h mode records.
        // Records are 40 bytes and 1 based, width height bpp first.
        // 'dx7z.dll' keeps only about 30 modes, every mode the display reports beyond
        // that is dropped, so 1920x1080 and up can be missing entirely.
        bool FindThrashMode(
            int width,
            int height,
            int bpp
        )
        {
            using AboutFn = std::uint8_t * (__stdcall *)();
            const auto about = hook::Ref< AboutFn >( game::var::THRASH_ABOUT );

            if (about == nullptr)
            {
                return false;
            }

            const std::uint8_t * info = about();

            if (info == nullptr)
            {
                return false;
            }

            const int count = *reinterpret_cast<const int *>(info + 0x3C);
            const auto * records = *reinterpret_cast<const std::uint8_t * const *>(info + 0x40);

            if (records == nullptr || count <= 0 || count > 256)
            {
                return false;
            }

            for (int i = 1; i <= count; ++i)
            {
                const auto * record = reinterpret_cast<const int *>(records + 40 * i);

                if (record[0] == width && record[1] == height && (bpp == 0 || record[2] == bpp))
                {
                    return true;
                }
            }

            return false;
        }
    }

    Widescreen & Widescreen::GetInstance()
    {
        static Widescreen instance;
        return instance;
    }

    float Widescreen::GetAspectCorrection()
    {
        const int width = hook::Ref< int >( game::var::SCREEN_WIDTH );
        const int height = hook::Ref< int >( game::var::SCREEN_HEIGHT );

        if (width <= 0 || height <= 0)
        {
            return 1.0f;
        }

        return (static_cast<float>(width) / static_cast<float>(height)) / BASE_ASPECT;
    }


    void Widescreen::UpdateSpriteScale(
        int width,
        int height
    )
    {
        if (!m_FlareScalePatch.IsApplied() || width <= 0 || height <= 0)
            return;

        // Flare width was 'screen_width / 640' and height 'screen_height / 480',
        // use the height based scale for both so flares stay round.
        s_FlareWidthScale = static_cast<float>(height) / (480.0f * static_cast<float>(width));
    }


    int __fastcall Widescreen::SetPerspectiveHook(
        game::EaglCamera * camera,
        void *,
        float fov_degrees,
        float aspect,
        float near_z,
        float far_z
    )
    {
        Widescreen & self = GetInstance();

        const int width = hook::Ref< int >( game::var::SCREEN_WIDTH );
        const int height = hook::Ref< int >( game::var::SCREEN_HEIGHT );
        const float correction = GetAspectCorrection();

        float new_fov = fov_degrees;
        float new_aspect = aspect;

        if (self.m_Settings.m_HorPlus && aspect > 0.0f && fov_degrees > 0.0f)
        {
            const float tan_half = std::tan( fov_degrees * 0.5f * DEG_TO_RAD ) * self.m_Settings.m_FovMultiplier;

            if (correction >= 1.0f)
            {
                // Wider than 4:3, keep the vertical extent and open up the horizontal one
                new_fov = 2.0f * std::atan( tan_half * correction ) * RAD_TO_DEG;
            }
            else
            {
                // Taller than 4:3, keep the horizontal extent and open up the vertical one
                new_fov = 2.0f * std::atan( tan_half ) * RAD_TO_DEG;
            }

            new_fov = std::min( new_fov, MAX_FOV_DEGREES );
            new_aspect = aspect * correction;
        }

        const int result = self.m_PerspectiveHook->thiscall< int >( camera, new_fov, new_aspect, near_z, far_z );

        if (new_fov != fov_degrees || new_aspect != aspect)
        {
            // 'SetActive' re-applies the stored inputs every time a camera is selected,
            // keeping the caller values makes that path idempotent.
            camera->m_FovDegrees = fov_degrees;
            camera->m_Aspect = aspect;

            // The original vertical cull angle is 'half_fov' times aspect, a generous bound.
            // Keep it, but never smaller than the true vertical half angle of the new projection.
            const float original_vertical = fov_degrees * 0.5f * DEG_TO_RAD * aspect;
            const float true_vertical = std::atan( std::tan( new_fov * 0.5f * DEG_TO_RAD ) / new_aspect );
            const float cull_vertical = std::clamp( std::max( original_vertical, true_vertical * 1.1f ), 0.0f, MAX_CULL_HALF_ANGLE );

            camera->m_TanHalfVertical = std::tan( cull_vertical );
            camera->m_CosHalfVertical = std::cos( cull_vertical );
        }

        self.UpdateSpriteScale( width, height );

        if (self.m_LodSizePatch.IsApplied())
        {
            // Undo the size loss of the wider horizontal fov, then apply the extra distance.
            const float fov_compensation = self.m_Settings.m_HorPlus ? std::max( correction, 1.0f ) * self.m_Settings.m_FovMultiplier : 1.0f;
            s_LodSizeScale = 640.0f * fov_compensation * self.m_Settings.m_LodDistanceScale;
        }

        if (width != self.m_LoggedWidth.load( std::memory_order_relaxed ) || height != self.m_LoggedHeight.load( std::memory_order_relaxed ))
        {
            self.m_LoggedWidth.store( width, std::memory_order_relaxed );
            self.m_LoggedHeight.store( height, std::memory_order_relaxed );

            LOG_INFO( "Resolution {}x{} aspect correction {:.4f}, sample camera fov {:.2f} -> {:.2f} aspect {:.4f} -> {:.4f}, car LOD scale {:.1f}",
                width, height, correction, fov_degrees, new_fov, aspect, new_aspect, s_LodSizeScale / 640.0f );
        }

        return result;
    }


    float * Widescreen::WidenViewHalfFov(
        float & original
    ) const
    {
        // Culling in the engine uses the 4:3 half FOV the view keeps at +130h, Hor+ only widens
        // the projection in 'SetPerspective'. The field itself feeds 'SetPerspective' again, so it
        // is only swapped for the duration of a cull call.
        const auto context = hook::Ref< std::uintptr_t >( game::var::RENDER_CONTEXT );
        const auto view = context != 0 ? *reinterpret_cast<std::uintptr_t *>(context + 0x10) : 0;
        const float correction = GetAspectCorrection();

        if (view == 0 || !m_Settings.m_HorPlus || correction <= 1.0f)
            return nullptr;

        float * half_fov = reinterpret_cast<float *>(view + 0x130);
        original = *half_fov;
        const float tan_half = std::tan( original * DEG_TO_RAD ) * correction * m_Settings.m_FovMultiplier;

        // A little margin, the sky test only samples the corners and edge midpoints of a patch.
        *half_fov = std::min( std::atan( tan_half ) * RAD_TO_DEG * 1.05f, MAX_FOV_DEGREES * 0.5f );
        return half_fov;
    }


    void __fastcall Widescreen::SkyCullPlanesHook(
        void * layer,
        void *
    )
    {
        Widescreen & self = GetInstance();
        float original = 0.0f;
        float * half_fov = self.WidenViewHalfFov( original );

        self.m_SkyCullHook->thiscall< void >( layer );

        if (half_fov != nullptr)
            *half_fov = original;
    }


    void __fastcall Widescreen::WorldCullVolumeHook(
        void * volume,
        void *,
        float * orientation,
        float * position,
        float distance
    )
    {
        Widescreen & self = GetInstance();
        float original = 0.0f;
        float * half_fov = self.WidenViewHalfFov( original );

        self.m_WorldCullHook->thiscall< void >( volume, orientation, position, distance );

        if (half_fov != nullptr)
            *half_fov = original;
    }


    int __cdecl Widescreen::ResolutionListCountHook()
    {
        Widescreen & self = GetInstance();

        using CountFn = int( __cdecl * )();
        const int count = reinterpret_cast<CountFn>(self.m_ResolutionCountPatch.GetOriginalTarget())();
        const auto context = hook::Ref< std::uintptr_t >( game::var::RENDER_CONTEXT );

        // In the software path the list is the single 640x480 entry, nothing to extend.
        if (count <= 0 || context == 0 || hook::Ref< std::uint8_t >( context + 0x60 ) != 0)
            return count;

        // Indices run '0' to count, record '0' is empty and the mode filter rejects it.
        return count + 1;
    }


    bool __cdecl Widescreen::IsVideoModeUsableHook(
        int mode_index
    )
    {
        Widescreen & self = GetInstance();

        if (self.m_ModeFilterHook->ccall< bool >( mode_index ))
            return true;

        const auto context = hook::Ref< std::uintptr_t >( game::var::RENDER_CONTEXT );

        if (context == 0 || hook::Ref< std::uint8_t >( context + 0x60 ) != 0)
            return false;

        const auto mode_object = hook::Ref< std::uintptr_t >( context + 0x70 );

        if (mode_object == 0)
            return false;

        // The original also rejected modes by a VRAM estimate and by card class,
        // both are meaningless on current hardware behind DDrawCompat.
        const auto * modes = hook::Ref< const game::DisplayMode * >( mode_object + 8 );
        const game::DisplayMode & mode = modes[mode_index];

        return mode.m_Width >= 640 && mode.m_Height >= 480 && mode.m_Capability >= 2 && mode.m_Bpp >= 15;
    }


    int __fastcall Widescreen::SetVideoModeHook(
        void * context,
        void *,
        int width,
        int height,
        int bpp,
        int flags
    )
    {
        Widescreen & self = GetInstance();

        const std::uintptr_t caller = NFS5_RETURN_ADDRESS();
        const bool race_switch = caller == game::fn::SET_VIDEO_MODE_RACE_START_RETURN.Get() || caller == game::fn::SET_VIDEO_MODE_RACE_OPTIONS_RETURN.Get();

        if (race_switch)
        {
            int new_width = width;
            int new_height = height;

            if (!self.GetRaceResolutionOverride( new_width, new_height ))
            {
                new_width = width;
                new_height = height;
            }

            const int new_bpp = self.m_Settings.m_RaceColorDepth > 0 ? self.m_Settings.m_RaceColorDepth : bpp;
            const bool changed = new_width != width || new_height != height || new_bpp != bpp;

            if (changed && !FindThrashMode( new_width, new_height, new_bpp ))
            {
                // The driver would fall back to its closest entry and the display mode change fails,
                // which crashed 'dx7z.dll' in the window thread, keep the game choice instead.
                LOG_WARN( "Race video mode {}x{}x{} is not in the driver mode list, keeping {}x{}x{}. Limit 'SupportedResolutions' in 'DDrawCompat.ini'",
                    new_width, new_height, new_bpp, width, height, bpp );
            }
            else if (new_width >= 640 && new_height >= 480 && changed)
            {
                LOG_INFO( "Race video mode {}x{}x{} overridden to {}x{}x{}", width, height, bpp, new_width, new_height, new_bpp );

                width = new_width;
                height = new_height;
                bpp = new_bpp;

                // Keep the option globals in sync, later code compares them with the active mode.
                hook::Ref< int >( game::var::RACE_WIDTH ) = width;
                hook::Ref< int >( game::var::RACE_HEIGHT ) = height;
                hook::Ref< int >( game::var::RACE_BPP ) = bpp;
            }
        }

        return self.m_SetVideoModeHook->thiscall< int >( context, width, height, bpp, flags );
    }


    bool Widescreen::GetRaceResolutionOverride(
        int & width,
        int & height
    ) const
    {
        if (!m_Installed || m_Settings.m_RaceWidth == 0)
            return false;

        if (m_Settings.m_RaceWidth > 0)
        {
            width = m_Settings.m_RaceWidth;
            height = m_Settings.m_RaceHeight;
            return true;
        }

        return GetDesktopSize( width, height );
    }


    bool Widescreen::IsResolutionAccessor(
        const void * accessor
    )
    {
        return accessor != nullptr && *static_cast<const std::uintptr_t *>(accessor) == game::fn::RESOLUTION_ACCESSOR_VTABLE.Get();
    }


    int Widescreen::FindListedMode(
        const void * accessor,
        int width,
        int height,
        int bpp
    )
    {
        namespace fn = game::fn;
        const auto base = reinterpret_cast<std::uintptr_t>(accessor);
        const int count = hook::Ref<int>( base + fn::ACCESSOR_COUNT );
        const auto * widths = hook::Ref<const int *>( base + fn::RESOLUTION_WIDTHS );
        const auto * heights = hook::Ref<const int *>( base + fn::RESOLUTION_HEIGHTS );
        const auto * depths = hook::Ref<const int *>( base + fn::RESOLUTION_DEPTHS );

        if (count <= 0 || widths == nullptr || heights == nullptr || depths == nullptr)
            return -1;

        for (int i = 0; i < count; ++i)
        {
            if (widths[i] == width && heights[i] == height && SameDepth( depths[i], bpp ))
                return i;
        }

        return -1;
    }


    int Widescreen::FindDefaultMode(
        const void * accessor
    )
    {
        int width = 0;
        int height = 0;

        if (GetDesktopSize( width, height ))
        {
            for (int bpp : { 32, 16 })
            {
                const int index = FindListedMode( accessor, width, height, bpp );

                if (index >= 0)
                    return index;
            }
        }

        namespace fn = game::fn;
        const auto base = reinterpret_cast<std::uintptr_t>(accessor);
        const int count = hook::Ref<int>( base + fn::ACCESSOR_COUNT );
        const auto * widths = hook::Ref<const int *>( base + fn::RESOLUTION_WIDTHS );
        const auto * heights = hook::Ref<const int *>( base + fn::RESOLUTION_HEIGHTS );
        const auto * depths = hook::Ref<const int *>( base + fn::RESOLUTION_DEPTHS );

        if (count <= 0 || widths == nullptr || heights == nullptr || depths == nullptr)
            return -1;

        int best = -1;
        std::int64_t best_area = 0;

        for (int i = 0; i < count; ++i)
        {
            const std::int64_t area = static_cast<std::int64_t>(widths[i]) * heights[i];

            if (depths[i] > 16 && area > best_area)
            {
                best = i;
                best_area = area;
            }
        }

        return best;
    }


    void * __fastcall Widescreen::ResolutionAccessorCtorHook(
        void * accessor,
        void *,
        const char * name
    )
    {
        Widescreen & self = GetInstance();
        void * result = self.m_ResolutionCtorHook->thiscall<void *>( accessor, name );

        // Only the first start without saved options keeps this, a saved screen size is loaded
        // over it afterwards.
        const int index = FindDefaultMode( accessor );

        if (index >= 0)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(accessor);
            hook::Ref<int>( base + game::fn::ACCESSOR_POSITION ) = index;

            LOG_INFO( "Default screen size {}x{}x{}", hook::Ref<const int *>( base + game::fn::RESOLUTION_WIDTHS )[index],
                hook::Ref<const int *>( base + game::fn::RESOLUTION_HEIGHTS )[index],
                hook::Ref<const int *>( base + game::fn::RESOLUTION_DEPTHS )[index] );
        }

        return result;
    }


    void * __fastcall Widescreen::PositionSaveHook(
        void * accessor,
        void *,
        std::uint8_t ** cursor
    )
    {
        Widescreen & self = GetInstance();
        void * result = self.m_PositionSaveHook->thiscall<void *>( accessor, cursor );

        if (!IsResolutionAccessor( accessor ) || cursor == nullptr || *cursor == nullptr)
            return result;

        namespace fn = game::fn;
        const auto base = reinterpret_cast<std::uintptr_t>(accessor);
        const int index = hook::Ref<int>( base + fn::ACCESSOR_POSITION );
        const int count = hook::Ref<int>( base + fn::ACCESSOR_COUNT );

        if (index < 0 || index >= count)
            return result;

        const int width = hook::Ref<const int *>( base + fn::RESOLUTION_WIDTHS )[index];
        const int height = hook::Ref<const int *>( base + fn::RESOLUTION_HEIGHTS )[index];
        const int bpp = hook::Ref<const int *>( base + fn::RESOLUTION_DEPTHS )[index];

        if (width <= 0 || height <= 0 || static_cast<std::uint32_t>(width) > SAVED_MODE_SIZE || static_cast<std::uint32_t>(height) > SAVED_MODE_SIZE)
            return result;

        const std::uint32_t saved = SAVED_MODE_MARK | (bpp > 16 ? SAVED_MODE_32 : 0) |
            (static_cast<std::uint32_t>(width) << 14) | static_cast<std::uint32_t>(height);

        // The original wrote the position and moved the cursor past it.
        std::memcpy( *cursor - sizeof( saved ), &saved, sizeof( saved ) );
        return result;
    }


    int __fastcall Widescreen::PositionLoadHook(
        void * accessor,
        void *,
        std::uint8_t ** cursor
    )
    {
        Widescreen & self = GetInstance();

        if (IsResolutionAccessor( accessor ) && cursor != nullptr && *cursor != nullptr)
        {
            std::uint32_t saved = 0;
            std::memcpy( &saved, *cursor, sizeof( saved ) );

            if ((saved & SAVED_MODE_MASK) == SAVED_MODE_MARK)
            {
                const int width = static_cast<int>((saved >> 14) & SAVED_MODE_SIZE);
                const int height = static_cast<int>(saved & SAVED_MODE_SIZE);
                const int bpp = (saved & SAVED_MODE_32) != 0 ? 32 : 16;
                int index = FindListedMode( accessor, width, height, bpp );

                if (index < 0)
                {
                    index = std::max( FindDefaultMode( accessor ), 0 );
                    LOG_WARN( "Saved screen size {}x{}x{} is no longer offered, using the default", width, height, bpp );
                }

                // The game reads the position from the stream, it gets the one of the saved mode.
                const auto position = static_cast<std::uint32_t>(index);
                std::memcpy( *cursor, &position, sizeof( position ) );
            }
        }

        return self.m_PositionLoadHook->thiscall<int>( accessor, cursor );
    }


    bool Widescreen::Install( const WidescreenSettings & settings )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "Widescreen disabled by config" );
            return true;
        }

        using namespace game;
        bool ok = true;

        if (settings.m_FixSpriteAspect)
        {
            ok &= m_FlareScalePatch.Apply(
                "FlareWidthScale",
                site::FLARE_WIDTH_SCALE.m_Address,
                site::FLARE_WIDTH_SCALE.m_Signature,
                site::FLARE_WIDTH_SCALE_OPERAND,
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&s_FlareWidthScale)) );
        }

        if (settings.m_LodDistanceScale > 0.0f)
        {
            if (hook::MatchBytes( site::CAR_LOD_SIZE_BLOCK.m_Address, site::CAR_LOD_SIZE_BLOCK.m_Signature ))
            {
                s_LodSizeScale = 640.0f * settings.m_LodDistanceScale;
                ok &= m_LodSizePatch.Apply( "CarLodSize", site::CAR_LOD_SIZE_DIVIDE.m_Address, site::CAR_LOD_SIZE_DIVIDE.m_Signature,
                    site::CAR_LOD_SIZE_DIVIDE_OPERAND, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&s_LodSizeScale)) );
            }
            else
            {
                LOG_ERROR( "Car LOD size block does not match, car detail distance unchanged" );
                ok = false;
            }
        }

        if (hook::MatchBytes( fn::RESOLUTION_LIST_COUNT_CALL.m_Address, fn::RESOLUTION_LIST_COUNT_CALL.m_Signature ))
        {
            ok &= m_ResolutionCountPatch.Apply( "ResolutionListCount", fn::RESOLUTION_LIST_COUNT_CALL.m_Address,
                reinterpret_cast<void *>(&ResolutionListCountHook) );
        }
        else
        {
            LOG_ERROR( "Resolution list count call does not match, the largest mode stays hidden" );
            ok = false;
        }

        // Screen size default and saving, the game kept a list position and started at 640x480x16.
        m_ResolutionCtorHook = hook::CreateInline( "ResolutionAccessorCtor", fn::RESOLUTION_ACCESSOR_CTOR.m_Address,
            fn::RESOLUTION_ACCESSOR_CTOR.m_Signature, reinterpret_cast<void *>(&ResolutionAccessorCtorHook) );
        m_PositionSaveHook = hook::CreateInline( "AccessorPositionSave", fn::POSITION_SAVE.m_Address, fn::POSITION_SAVE.m_Signature,
            reinterpret_cast<void *>(&PositionSaveHook) );
        m_PositionLoadHook = hook::CreateInline( "AccessorPositionLoad", fn::POSITION_LOAD.m_Address, fn::POSITION_LOAD.m_Signature,
            reinterpret_cast<void *>(&PositionLoadHook) );

        // Saving the mode without reading it back would leave the stock position reader with a
        // value it clamps to the last mode.
        if (!m_PositionSaveHook || !m_PositionLoadHook)
        {
            m_PositionSaveHook.reset();
            m_PositionLoadHook.reset();
            ok = false;
        }

        ok &= m_ResolutionCtorHook != nullptr;

        m_PerspectiveHook = hook::CreateInline( "CameraSetPerspective", fn::CAMERA_SET_PERSPECTIVE.m_Address, fn::CAMERA_SET_PERSPECTIVE.m_Signature,
            reinterpret_cast<void *>(&SetPerspectiveHook) );

        ok &= m_PerspectiveHook != nullptr;

        if (settings.m_HorPlus)
        {
            m_SkyCullHook = hook::CreateInline( "SkyCullPlanes", fn::SKY_CULL_PLANES.m_Address, fn::SKY_CULL_PLANES.m_Signature,
                reinterpret_cast<void *>(&SkyCullPlanesHook) );
            ok &= m_SkyCullHook != nullptr;

            m_WorldCullHook = hook::CreateInline( "WorldCullVolume", fn::WORLD_CULL_VOLUME.m_Address, fn::WORLD_CULL_VOLUME.m_Signature,
                reinterpret_cast<void *>(&WorldCullVolumeHook) );
            ok &= m_WorldCullHook != nullptr;
        }

        if (settings.m_UnlockResolutions)
        {
            m_ModeFilterHook = hook::CreateInline( "IsVideoModeUsable", fn::IS_VIDEO_MODE_USABLE.m_Address, fn::IS_VIDEO_MODE_USABLE.m_Signature,
                reinterpret_cast<void *>(&IsVideoModeUsableHook) );
            ok &= m_ModeFilterHook != nullptr;
        }

        if (settings.m_RaceWidth != 0 || settings.m_RaceColorDepth != 0)
        {
            m_SetVideoModeHook = hook::CreateInline( "SetVideoMode", fn::SET_VIDEO_MODE.m_Address, fn::SET_VIDEO_MODE.m_Signature,
                reinterpret_cast<void *>(&SetVideoModeHook) );
            ok &= m_SetVideoModeHook != nullptr;
        }

        m_Installed = true;
        LOG_INFO( "Widescreen installed, all hooks ok {}", ok );
        return ok;
    }


    void Widescreen::Uninstall()
    {
        m_SetVideoModeHook.reset();
        m_PositionLoadHook.reset();
        m_PositionSaveHook.reset();
        m_ResolutionCtorHook.reset();
        m_ModeFilterHook.reset();
        m_WorldCullHook.reset();
        m_SkyCullHook.reset();
        m_PerspectiveHook.reset();
        m_ResolutionCountPatch.Restore();
        m_FlareScalePatch.Restore();
        m_LodSizePatch.Restore();
        m_Installed = false;
    }
}