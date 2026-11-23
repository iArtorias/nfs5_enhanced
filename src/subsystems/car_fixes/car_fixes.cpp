#include "car_fixes.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>
#include <cstring>
#include <tuple>
#include <utility>

#include "../../core/log.h"
#include "../../core/win_handle.h"

namespace nfs5
{
    namespace
    {
        // Vector header pointed to by '[0x006919E8]' holding pointers to 1648 byte car records.
        struct CarRecordVector
        {
            std::uint8_t ** m_Begin;
            std::uint8_t ** m_End;
            std::uint8_t ** m_Capacity;
        };

        template <typename T>
        [[nodiscard]] T ReadPod(
            const std::uint8_t * ptr
        ) noexcept
        {
            T value {};
            std::memcpy( &value, ptr, sizeof( value ) );
            return value;
        }

        template <typename T>
        void WritePod(
            std::uint8_t * ptr,
            const T & value
        ) noexcept
        {
            std::memcpy( ptr, &value, sizeof( value ) );
        }

        [[nodiscard]] bool NearlyEqual(
            float a,
            float b,
            float epsilon = 1e-3f
        ) noexcept
        {
            return std::fabs( a - b ) <= epsilon;
        }

        // Known Car IDs from 'nfs5.car'.
        constexpr std::uint16_t CAR_ID_993_CARRERA_4S = 180;
        constexpr std::uint16_t CAR_ID_928_GTS = 214;

        // Known Part IDs from 'nfs5.prt'.
        constexpr std::uint32_t PART_ID_356A_1300_ENGINE = 18;
        constexpr std::uint32_t PART_ID_356_STREET_SPRINGS = 43;
        constexpr std::uint32_t PART_ID_930_MED_EXHAUST = 203;
        constexpr std::uint32_t PART_ID_930_HIGH_EXHAUST = 204;
        constexpr std::uint32_t PART_ID_964_HD_SHOCKS = 372;
        constexpr std::uint32_t PART_ID_993_HD_SHOCKS = 453;
        constexpr std::uint32_t PART_ID_993_XSTIFF_SWAY = 459;
        constexpr std::uint32_t PART_ID_996_HD_SHOCKS = 538;
        constexpr std::uint32_t PART_ID_996_STAGE4_SC = 565;
        constexpr std::uint32_t PART_ID_BOXSTER_HARDTOP = 598;
        constexpr std::uint32_t PART_ID_993_SLICKS_BASE = 679;
        constexpr std::uint32_t PART_ID_993_SLICKS_TURBO = 682;
        constexpr std::uint32_t PART_ID_993_SLICKS_S = 689;
        constexpr std::uint32_t PART_ID_944_BUMPERS_2 = 723;

        // Pre-GTS-pack and fixed GTS-pack dash/camera coordinates at record +0x2E8 for '92 928 GTS.
        constexpr std::array<float, 6> OLD_928_GTS_DASH =
        {
            0.0f, 0.0f, 0.1f, 0.1f, 0.1f, 0.1f
        };

        constexpr std::array<std::uint8_t, 24> FIXED_928_GTS_DASH_BYTES =
        {
            0xCD, 0xCC, 0x4C, 0xBF, // -0.8f
            0x9A, 0x99, 0x99, 0x3E, //  0.3f
            0x27, 0xA0, 0xA9, 0xBE, // -0.3313f
            0x78, 0x0B, 0xA4, 0x3D, //  0.0801f
            0x5B, 0xD3, 0xBC, 0xBE, // -0.3688f
            0x8A, 0xB0, 0x61, 0x3D  //  0.0551f
        };

        // Stock 930 High Performance Exhaust (ID 204) has an all-zero torque curve. When vanilla
        // Medium Performance Exhaust (ID 203, peak 30 lb-ft) is present, scale ~1.3x above Medium.
        constexpr std::array<float, game::car_fixes::PART_TORQUE_COUNT> FIXED_930_HIGH_EXHAUST_TORQUE =
        {
            13.0f, 13.0f, 13.0f, 17.0f, 21.0f, 25.0f, 31.0f, 38.0f,
            38.0f, 39.0f, 39.0f, 36.0f, 34.0f, 34.0f, 32.0f, 0.0f,
            0.0f,  0.0f,  0.0f,  0.0f
        };

        // Replacement literal for '"Supercharged"' -> '"Supercharger"' at 0x005D0D04.
        constexpr std::array<std::uint8_t, 13> SUPERCHARGER_REPLACEMENT =
        {
            'S', 'u', 'p', 'e', 'r', 'c', 'h', 'a', 'r', 'g', 'e', 'r', '\0'
        };

        // Stock wet weather surface grip table at 0x005D0E4C: 12 surfaces x [Stock, Rain, Slicks].
        // The original table copied the dry table (0x005D0D50) and swapped Stock/Rain across all
        // rows while leaving Slicks unchanged, leaving Slicks at 1.10 on wet asphalt and inverting
        // Stock vs Rain on wet dirt, grass, and snow.
        constexpr std::array<float, game::car_fixes::WET_TIRE_TABLE_FLOATS> STOCK_WET_TIRE_TABLE =
        {
            0.500f, 0.500f, 0.500f, // Row  0: ice / deep off-track
            0.900f, 1.000f, 1.100f, // Row  1: asphalt (Slicks unpenalized!)
            0.900f, 0.850f, 0.850f, // Row  2: cobblestone (Stock > Rain inverted)
            0.775f, 0.700f, 0.625f, // Row  3: dirt / gravel (Stock > Rain inverted)
            0.900f, 0.950f, 1.050f, // Row  4: paved 2
            0.875f, 0.850f, 0.800f, // Row  5: rough paved / cobble 2 (inverted)
            0.800f, 0.700f, 0.600f, // Row  6: grass / mud (Stock > Rain inverted)
            0.900f, 0.900f, 1.000f, // Row  7: paved 3
            0.750f, 0.600f, 0.500f, // Row  8: snow (Stock > Rain inverted)
            0.750f, 0.675f, 0.600f, // Row  9: mixed snow / dirt (inverted)
            0.800f, 0.850f, 0.950f, // Row 10: paved 4
            0.850f, 0.900f, 0.900f  // Row 11: paved 5
        };

        // Rebalanced wet surface grip table: Rain >= Stock on every wet surface, Slicks lose
        // significant traction in wet weather while remaining controllable.
        constexpr std::array<float, game::car_fixes::WET_TIRE_TABLE_FLOATS> FIXED_WET_TIRE_TABLE =
        {
            0.500f, 0.500f, 0.500f, // Row  0: ice / deep off-track
            0.900f, 1.000f, 0.780f, // Row  1: asphalt
            0.800f, 0.900f, 0.650f, // Row  2: cobblestone
            0.650f, 0.775f, 0.500f, // Row  3: dirt / gravel
            0.900f, 0.950f, 0.750f, // Row  4: paved 2
            0.800f, 0.875f, 0.625f, // Row  5: rough paved / cobble 2
            0.650f, 0.800f, 0.475f, // Row  6: grass / mud
            0.850f, 0.900f, 0.720f, // Row  7: paved 3
            0.600f, 0.750f, 0.425f, // Row  8: snow
            0.650f, 0.750f, 0.475f, // Row  9: mixed snow / dirt
            0.800f, 0.850f, 0.680f, // Row 10: paved 4
            0.850f, 0.900f, 0.700f  // Row 11: paved 5
        };

        [[nodiscard]] bool IsTorqueCurveAllZero(
            const std::uint8_t * part
        ) noexcept
        {
            namespace cf = game::car_fixes;

            for (std::size_t i = 0; i < cf::PART_TORQUE_COUNT; ++i)
            {
                if (ReadPod<float>( part + cf::PART_TORQUE_OFFSET + i * sizeof( float ) ) != 0.0f)
                    return false;
            }

            return true;
        }
    }

    CarFixes & CarFixes::GetInstance()
    {
        static CarFixes instance;
        return instance;
    }

    void __cdecl CarFixes::LoadPartsHook()
    {
        CarFixes & self = GetInstance();
        self.m_LoadPartsHook->ccall<void>();
        self.PatchLoadedParts();
    }

    void __cdecl CarFixes::CarListPostLoadHook()
    {
        CarFixes & self = GetInstance();
        self.PatchLoadedCars();

        const auto original = reinterpret_cast<void(__cdecl *)()>(self.m_CarListPostLoadCall.GetOriginalTarget());
        if (original != nullptr)
        {
            original();
        }
    }

    void CarFixes::PartsLoopStartHook(
        safetyhook::Context & context
    )
    {
        namespace cf = game::car_fixes;
        CarFixes & self = GetInstance();
        self.m_SwappedCar = 0;
        self.m_SwappedSlot = -1;

        auto ** slots = reinterpret_cast<std::uint8_t **>(context.esi + cf::CAR_PARTS_ARRAY_OFFSET);

        for (std::size_t i = 0; i < cf::CAR_PARTS_SLOT_COUNT; ++i)
        {
            std::uint8_t * part = slots[i];
            if (part == nullptr)
                continue;

            const std::uint16_t main_cat = ReadPod<std::uint16_t>( part + cf::PART_MAIN_CAT_OFFSET );
            if (main_cat != 1)
                continue;

            const char * subcat = reinterpret_cast<const char *>(part + cf::PART_SUBCAT_OFFSET);
            if (std::strcmp( subcat, "Engine" ) == 0)
            {
                // Run the engine swap first so its full torque-curve overwrite never wipes out
                // additive engine bolt-ons (carbs, exhaust, intake, ECU, etc.) in earlier slots.
                if (i > 0)
                {
                    std::swap( slots[0], slots[i] );
                    self.m_SwappedCar = context.esi;
                    self.m_SwappedSlot = static_cast<int>(i);
                }

                break;
            }
        }
    }

    void CarFixes::EngineSwapStartHook(
        safetyhook::Context & context
    )
    {
        namespace cf = game::car_fixes;
        const auto * part = *reinterpret_cast<const std::uint8_t * const *>(context.edi);
        auto * sim = *reinterpret_cast<std::uint8_t **>(context.esi + cf::CAR_SIM_PTR_OFFSET);

        if (part == nullptr || sim == nullptr)
            return;

        const float swap_redline = ReadPod<float>( part + cf::PART_STAT_C0_OFFSET );

        if (swap_redline > 1000.0f && swap_redline < 15000.0f)
        {
            WritePod<float>( sim + cf::SIM_REDLINE_OFFSET, swap_redline );
            WritePod<float>( sim + cf::SIM_INV_REDLINE_OFFSET, 1.0f / swap_redline );
        }
    }

    void CarFixes::PartsLoopEndHook(
        safetyhook::Context & context
    )
    {
        namespace cf = game::car_fixes;
        CarFixes & self = GetInstance();

        if (self.m_SwappedCar == context.esi && self.m_SwappedSlot > 0)
        {
            auto ** slots = reinterpret_cast<std::uint8_t **>(context.esi + cf::CAR_PARTS_ARRAY_OFFSET);
            std::swap( slots[0], slots[static_cast<std::size_t>(self.m_SwappedSlot)] );
        }

        self.m_SwappedCar = 0;
        self.m_SwappedSlot = -1;
    }

    void CarFixes::SetupRideHeightPreHook(
        safetyhook::Context & context
    )
    {
        namespace cf = game::car_fixes;
        CarFixes & self = GetInstance();
        const auto * sim = *reinterpret_cast<const std::uint8_t * const *>(context.esi + cf::CAR_SIM_PTR_OFFSET);

        self.m_SavedSpringCar = context.esi;
        self.m_SavedSpringLowering = (sim != nullptr) ? ReadPod<float>( sim + cf::SIM_RIDE_HEIGHT_OFFSET ) : 0.0f;
    }

    void CarFixes::SetupRideHeightPostHook(
        safetyhook::Context & context
    )
    {
        namespace cf = game::car_fixes;
        CarFixes & self = GetInstance();

        if (self.m_SavedSpringCar == context.esi && self.m_SavedSpringLowering != 0.0f)
        {
            auto * sim = *reinterpret_cast<std::uint8_t **>(context.esi + cf::CAR_SIM_PTR_OFFSET);

            if (sim != nullptr)
            {
                const float setup_lowering = ReadPod<float>( sim + cf::SIM_RIDE_HEIGHT_OFFSET );
                WritePod<float>( sim + cf::SIM_RIDE_HEIGHT_OFFSET, setup_lowering + self.m_SavedSpringLowering );
            }
        }

        self.m_SavedSpringCar = 0;
        self.m_SavedSpringLowering = 0.0f;
    }

    void CarFixes::PatchLoadedCars()
    {
        namespace cf = game::car_fixes;

        if (!m_Settings.m_FixCarDatabase)
            return;

        const auto * vec = hook::Ref<CarRecordVector *>( cf::CAR_LIST_VECTOR );

        if (vec == nullptr || vec->m_Begin == nullptr || vec->m_End <= vec->m_Begin)
            return;

        int patched = 0;

        for (std::uint8_t ** it = vec->m_Begin; it < vec->m_End; ++it)
        {
            std::uint8_t * rec = *it;
            if (rec == nullptr)
                continue;

            const std::uint16_t car_id = ReadPod<std::uint16_t>( rec + cf::CAR_ID_OFFSET );

            // BUG-01: '96 911 Carrera 4S (993) sim filename typo ('993coupeS436' -> '993coupe4S36').
            if (car_id == CAR_ID_993_CARRERA_4S)
            {
                auto * sim_name = reinterpret_cast<char *>(rec + cf::CAR_SIM_NAME_OFFSET);

                if (std::strcmp( sim_name, "993coupeS436" ) == 0)
                {
                    std::memcpy( sim_name, "993coupe4S36", 13 );
                    ++patched;
                    LOG_INFO( "CarFixes: fixed Car ID 180 (993 Carrera 4S) sim filename 993coupeS436 -> 993coupe4S36" );
                }
            }

            // QUIRK-07: '92 928 GTS dash/camera offsets clobbered when 959/GT2/GT3 DLC installs after GTS.
            if (car_id == CAR_ID_928_GTS)
            {
                bool matches_old = true;

                for (std::size_t i = 0; i < OLD_928_GTS_DASH.size(); ++i)
                {
                    const float current = ReadPod<float>( rec + cf::CAR_DASH_CAMERA_OFFSET + i * sizeof( float ) );
                    if (!NearlyEqual( current, OLD_928_GTS_DASH[i], 1e-4f ))
                    {
                        matches_old = false;
                        break;
                    }
                }

                if (matches_old)
                {
                    std::memcpy( rec + cf::CAR_DASH_CAMERA_OFFSET, FIXED_928_GTS_DASH_BYTES.data(), FIXED_928_GTS_DASH_BYTES.size() );
                    ++patched;
                    LOG_INFO( "CarFixes: restored Car ID 214 (928 GTS) dash/camera offsets" );
                }
            }
        }

        if (patched > 0)
        {
            LOG_INFO( "CarFixes: patched {} car database entries in memory", patched );
        }
    }

    void CarFixes::PatchLoadedParts()
    {
        namespace cf = game::car_fixes;

        const std::int32_t count = hook::Ref<std::int32_t>( cf::PARTS_COUNT );
        std::uint8_t * base = hook::Ref<std::uint8_t *>( cf::PARTS_DATA );

        if (base == nullptr || count <= 0 || count > 4096)
            return;

        int patched_bugs = 0;
        int patched_quirks = 0;
        std::uint8_t * part_930_med = nullptr;
        std::uint8_t * part_930_high = nullptr;

        for (std::int32_t i = 0; i < count; ++i)
        {
            std::uint8_t * rec = base + static_cast<std::size_t>(i) * cf::PART_RECORD_SIZE;
            const std::uint32_t pid = ReadPod<std::uint32_t>( rec + cf::PART_ID_OFFSET );

            if (pid == PART_ID_930_MED_EXHAUST)
                part_930_med = rec;
            else if (pid == PART_ID_930_HIGH_EXHAUST)
                part_930_high = rec;

            if (m_Settings.m_FixPartBugs)
            {
                // BUG-02: 996 Stage 4 Supercharger 1110.0f typo at 7500 RPM (index 15, +0x14C).
                if (pid == PART_ID_996_STAGE4_SC)
                {
                    const std::size_t rpm7500_off = cf::PART_TORQUE_OFFSET + 15 * sizeof( float );
                    const float current = ReadPod<float>( rec + rpm7500_off );

                    if (current > 500.0f)
                    {
                        WritePod<float>( rec + rpm7500_off, 110.0f );
                        ++patched_bugs;
                        LOG_INFO( "CarFixes: fixed Part ID 565 (996 Stage 4 Supercharger) 7500 RPM torque {:.1f} -> 110.0", current );
                    }
                }

                // BUG-04: Heavy Duty Shocks rated 0.80 (worse than Standard 1.00) -> 1.25.
                if (pid == PART_ID_964_HD_SHOCKS || pid == PART_ID_993_HD_SHOCKS || pid == PART_ID_996_HD_SHOCKS)
                {
                    const float current = ReadPod<float>( rec + cf::PART_STAT_C0_OFFSET );

                    if (NearlyEqual( current, 0.80f ))
                    {
                        WritePod<float>( rec + cf::PART_STAT_C0_OFFSET, 1.25f );
                        ++patched_bugs;
                        LOG_INFO( "CarFixes: fixed Part ID {} (Heavy Duty Shocks) rating {:.2f} -> 1.25", pid, current );
                    }
                }
            }

            if (m_Settings.m_FixPartQuirks)
            {
                // QUIRK-01 & QUIRK-02: stray drag = 1.0 on 356 Street Perf Springs and 356A 1300 Engine Upgrade.
                if (pid == PART_ID_356_STREET_SPRINGS || pid == PART_ID_356A_1300_ENGINE)
                {
                    const float drag = ReadPod<float>( rec + cf::PART_DRAG_OFFSET );

                    if (NearlyEqual( drag, 1.0f ))
                    {
                        WritePod<float>( rec + cf::PART_DRAG_OFFSET, 0.0f );
                        ++patched_quirks;
                        LOG_INFO( "CarFixes: cleared stray drag on Part ID {} ({:.1f} -> 0.0)", pid, drag );
                    }
                }

                // QUIRK-03: Boxster Hardtop drag = -20.0 (+25% top speed!) -> -2.0 (+2% coupe aero).
                if (pid == PART_ID_BOXSTER_HARDTOP)
                {
                    const float drag = ReadPod<float>( rec + cf::PART_DRAG_OFFSET );

                    if (NearlyEqual( drag, -20.0f ))
                    {
                        WritePod<float>( rec + cf::PART_DRAG_OFFSET, -2.0f );
                        ++patched_quirks;
                        LOG_INFO( "CarFixes: rebalanced Part ID 598 (Boxster Hardtop) drag {:.1f} -> -2.0", drag );
                    }
                }

                // QUIRK-04: 944 Bumpers Upgrade #2 ($800) had 0/0 downforce while cheaper #1 had 5/5.
                // Skipped automatically when GameplayFixes already set non-zero downforce on #2.
                if (pid == PART_ID_944_BUMPERS_2)
                {
                    const float fdf = ReadPod<float>( rec + cf::PART_STAT_C0_OFFSET );
                    const float rdf = ReadPod<float>( rec + cf::PART_STAT_C4_OFFSET );

                    if (NearlyEqual( fdf, 0.0f ) && NearlyEqual( rdf, 0.0f ))
                    {
                        WritePod<float>( rec + cf::PART_STAT_C0_OFFSET, 10.0f );
                        WritePod<float>( rec + cf::PART_STAT_C4_OFFSET, 10.0f );
                        ++patched_quirks;
                        LOG_INFO( "CarFixes: fixed Part ID 723 (944 Bumpers #2) downforce 0/0 -> 10/10" );
                    }
                }

                // QUIRK-05: 993 Extra Stiff Sway Bar weight = 4.0 kg (lighter than Stiff 6.0 kg) -> 8.0 kg.
                if (pid == PART_ID_993_XSTIFF_SWAY)
                {
                    const float weight = ReadPod<float>( rec + cf::PART_WEIGHT_OFFSET );

                    if (NearlyEqual( weight, 4.0f ))
                    {
                        WritePod<float>( rec + cf::PART_WEIGHT_OFFSET, 8.0f );
                        ++patched_quirks;
                        LOG_INFO( "CarFixes: fixed Part ID 459 (993 Extra Stiff Sway Bar) weight {:.1f} -> 8.0 kg", weight );
                    }
                }

                // QUIRK-06: 993 Slicks grip rating = 1.30 (lower than 993 4S / 996 Slicks 1.50) -> 1.50.
                if (pid == PART_ID_993_SLICKS_BASE || pid == PART_ID_993_SLICKS_TURBO || pid == PART_ID_993_SLICKS_S)
                {
                    const float front_grip = ReadPod<float>( rec + cf::PART_STAT_C0_OFFSET );
                    const float rear_grip = ReadPod<float>( rec + cf::PART_STAT_C4_OFFSET );

                    if (NearlyEqual( front_grip, 1.30f ) && NearlyEqual( rear_grip, 1.30f ))
                    {
                        WritePod<float>( rec + cf::PART_STAT_C0_OFFSET, 1.50f );
                        WritePod<float>( rec + cf::PART_STAT_C4_OFFSET, 1.50f );
                        ++patched_quirks;
                        LOG_INFO( "CarFixes: fixed Part ID {} (993 Slicks) grip rating 1.30 -> 1.50", pid );
                    }
                }
            }
        }

        // BUG-03: 930 Turbo High Performance Exhaust (ID 204) has an all-zero torque curve in vanilla.
        // Skipped automatically when GameplayFixes has already populated it.
        if (m_Settings.m_FixPartBugs && part_930_high != nullptr && IsTorqueCurveAllZero( part_930_high ))
        {
            if (part_930_med == nullptr || !IsTorqueCurveAllZero( part_930_med ))
            {
                std::memcpy(
                    part_930_high + cf::PART_TORQUE_OFFSET,
                    FIXED_930_HIGH_EXHAUST_TORQUE.data(),
                    FIXED_930_HIGH_EXHAUST_TORQUE.size() * sizeof( float )
                );
                ++patched_bugs;
                LOG_INFO( "CarFixes: populated missing torque curve on Part ID 204 (930 High Perf Exhaust)" );
            }
        }

        LOG_INFO( "CarFixes: scanned {} parts, applied {} bug fixes and {} quirk fixes", count, patched_bugs, patched_quirks );
    }

    bool CarFixes::PatchWetTireTable()
    {
        namespace cf = game::car_fixes;
        auto * table = hook::Ptr<float>( cf::WET_TIRE_GRIP_TABLE );

        for (std::size_t i = 0; i < STOCK_WET_TIRE_TABLE.size(); ++i)
        {
            if (!NearlyEqual( table[i], STOCK_WET_TIRE_TABLE[i], 1e-4f ))
            {
                LOG_ERROR( "CarFixes: wet tire grip table at {:08X} does not match stock values, left unchanged", cf::WET_TIRE_GRIP_TABLE.Get() );
                return false;
            }
        }

        ScopedPageProtection protection( table, sizeof( STOCK_WET_TIRE_TABLE ) );

        if (!protection.IsChanged())
        {
            LOG_ERROR( "CarFixes: wet tire grip table could not be unprotected" );
            return false;
        }

        std::memcpy( m_OriginalWetTireTable.data(), table, sizeof( STOCK_WET_TIRE_TABLE ) );
        std::memcpy( table, FIXED_WET_TIRE_TABLE.data(), sizeof( FIXED_WET_TIRE_TABLE ) );
        m_WetTireTablePatched = true;
        LOG_INFO( "CarFixes: rebalanced wet-weather tire grip table at {:08X}", cf::WET_TIRE_GRIP_TABLE.Get() );
        return true;
    }

    void CarFixes::RestoreWetTireTable()
    {
        if (!m_WetTireTablePatched)
            return;

        auto * table = hook::Ptr<float>( game::car_fixes::WET_TIRE_GRIP_TABLE );
        ScopedPageProtection protection( table, sizeof( m_OriginalWetTireTable ) );

        if (protection.IsChanged())
        {
            std::memcpy( table, m_OriginalWetTireTable.data(), sizeof( m_OriginalWetTireTable ) );
        }

        m_WetTireTablePatched = false;
    }

    void CarFixes::UsedCarDisplayHook( safetyhook::Context & context )
    {
        // EAX holds the dealer's filtered view. Buying a row uses its offer pointer.
        auto * view = reinterpret_cast<CarRecordVector *>( context.eax );
        const auto * catalog = hook::Ref<CarRecordVector *>( game::car_fixes::CAR_LIST_VECTOR );
        if (!view || !view->m_Begin || view->m_End == view->m_Begin ||
            !catalog || !catalog->m_Begin)
            return;

        // The offer embeds its model record at +4. Look up the original database
        // entry so existing saves and tied model years use the editor's ordering.
        const auto rank = [catalog]( const std::uint8_t * offer )
        {
            const auto id = ReadPod<std::uint16_t>( offer + 4 );
            for (auto it = catalog->m_Begin; it != catalog->m_End; ++it)
            {
                const auto * model = *it;
                if (model && ReadPod<std::uint16_t>( model ) == id)
                    return std::tuple{ false, ReadPod<std::uint32_t>( model + 0x1B0 ),
                        ReadPod<std::uint32_t>( model + 0x1AC ),
                        ReadPod<std::uint16_t>( model + 0x1F8 ), it - catalog->m_Begin };
            }
            return std::tuple{ true, std::uint32_t{}, std::uint32_t{},
                std::uint16_t{}, std::ptrdiff_t{} };
        };
        std::stable_sort( view->m_Begin, view->m_End, [&rank]( auto * left, auto * right )
        {
            return rank( left ) < rank( right );
        } );
    }

    void CarFixes::UsedCarCandidateHook( safetyhook::Context & context )
    {
        // EBX selects the model for both the eligibility check and offer creation.
        // Shuffle indices only. Other dealer screens share the actual catalog.
        static thread_local std::vector<std::uint32_t> order;
        if (context.ebx == 0)
        {
            static thread_local std::mt19937 rng( ::GetTickCount() );
            order.resize( context.ebp );
            std::iota( order.begin(), order.end(), 0u );
            std::shuffle( order.begin(), order.end(), rng );
        }
        context.ebx = order[context.ebx];
    }

    void CarFixes::UsedCarAdvanceHook( safetyhook::Context & context )
    {
        // The game's stack local still holds the sequential index. All paths,
        // including a failed spawn or a vector reallocation, join here.
        context.ebx = *reinterpret_cast<const std::uint32_t *>(context.esp + 0x10);
    }

    bool CarFixes::Install(
        const CarFixesSettings & settings
    )
    {
        namespace cf = game::car_fixes;
        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "CarFixes disabled" );
            return false;
        }

        if (m_Installed)
            return true;

        if (settings.m_RemoveGaragePurchaseLimit)
        {
            constexpr std::uint8_t jump[] = { 0xEB };
            const auto & newDealer = cf::NEW_CAR_GARAGE_LIMIT;
            const auto & usedDealer = cf::USED_CAR_GARAGE_LIMIT;
            if (m_NewCarGarageLimit.Apply( "NewCarGarageLimit", newDealer.m_Address, newDealer.m_Signature, jump ) &&
                !m_UsedCarGarageLimit.Apply( "UsedCarGarageLimit", usedDealer.m_Address, usedDealer.m_Signature, jump ))
                m_NewCarGarageLimit.Restore();
        }

        if (settings.m_FixUsedCarAgeGap)
        {
            constexpr std::uint8_t thresholds[] = { 54,0,0,0, 54,0,0,0, 54,0,0,0, 54,0,0,0 };
            const auto & site = cf::USED_CAR_AGE_7_TO_10;
            m_UsedCarAgePatch.Apply( "FixUsedCarAgeGap", site.m_Address, site.m_Signature, thresholds );
        }

        if (settings.m_ShuffleUsedCarCandidates)
        {
            const auto & advance = cf::USED_CAR_ADVANCE;
            const auto & candidate = cf::USED_CAR_CANDIDATE;
            const auto & display = cf::USED_CAR_DISPLAY_READY;
            m_UsedCarDisplayHook = hook::CreateMid( "UsedCarDisplay", display.m_Address, display.m_Signature, &UsedCarDisplayHook );
            if (m_UsedCarDisplayHook)
                m_UsedCarAdvanceHook = hook::CreateMid( "UsedCarAdvance", advance.m_Address, advance.m_Signature, &UsedCarAdvanceHook );
            if (m_UsedCarAdvanceHook)
                m_UsedCarCandidateHook = hook::CreateMid( "UsedCarCandidate", candidate.m_Address, candidate.m_Signature, &UsedCarCandidateHook );
            if (!m_UsedCarCandidateHook)
            {
                m_UsedCarAdvanceHook.reset();
                m_UsedCarDisplayHook.reset();
            }
        }

        if (settings.m_FixPartBugs || settings.m_FixPartQuirks)
        {
            m_LoadPartsHook = hook::CreateInline(
                "CarFixesLoadParts",
                cf::PARTS_LOAD.m_Address,
                cf::PARTS_LOAD.m_Signature,
                reinterpret_cast<void *>(&LoadPartsHook)
            );
        }

        if (settings.m_FixCarDatabase)
        {
            if (hook::MatchBytes( cf::CAR_LIST_POST_LOAD_CALL.m_Address, cf::CAR_LIST_POST_LOAD_CALL.m_Signature ))
            {
                m_CarListPostLoadCall.Apply(
                    "CarFixesCarListPostLoad",
                    cf::CAR_LIST_POST_LOAD_CALL.m_Address,
                    reinterpret_cast<void *>(&CarListPostLoadHook)
                );
            }
            else
            {
                LOG_ERROR( "CarFixes: car list post-load site does not match, FixCarDatabase skipped" );
            }
        }

        if (settings.m_FixSuperchargerFlag)
        {
            m_SuperchargerStrPatch.Apply(
                "CarFixesSuperchargerStr",
                cf::SUPERCHARGER_STR.m_Address,
                cf::SUPERCHARGER_STR.m_Signature,
                SUPERCHARGER_REPLACEMENT
            );
        }

        if (settings.m_FixEngineSwaps)
        {
            m_PartsLoopStartHook = hook::CreateMid(
                "CarFixesPartsLoopStart",
                cf::PARTS_LOOP_START.m_Address,
                cf::PARTS_LOOP_START.m_Signature,
                &PartsLoopStartHook
            );

            m_EngineSwapStartHook = hook::CreateMid(
                "CarFixesEngineSwapStart",
                cf::ENGINE_SWAP_START.m_Address,
                cf::ENGINE_SWAP_START.m_Signature,
                &EngineSwapStartHook
            );

            m_PartsLoopEndHook = hook::CreateMid(
                "CarFixesPartsLoopEnd",
                cf::PARTS_LOOP_END.m_Address,
                cf::PARTS_LOOP_END.m_Signature,
                &PartsLoopEndHook
            );

            // If start succeeded without end (unexpected), release start so slot ordering cannot leak.
            if (m_PartsLoopStartHook && !m_PartsLoopEndHook)
            {
                m_PartsLoopStartHook.reset();
            }
        }

        if (settings.m_FixSpringLowering)
        {
            m_SetupRideHeightPreHook = hook::CreateMid(
                "CarFixesSetupRideHeightPre",
                cf::SETUP_RIDE_HEIGHT_PRE.m_Address,
                cf::SETUP_RIDE_HEIGHT_PRE.m_Signature,
                &SetupRideHeightPreHook
            );

            m_SetupRideHeightPostHook = hook::CreateMid(
                "CarFixesSetupRideHeightPost",
                cf::SETUP_RIDE_HEIGHT_POST.m_Address,
                cf::SETUP_RIDE_HEIGHT_POST.m_Signature,
                &SetupRideHeightPostHook
            );

            if (!m_SetupRideHeightPreHook || !m_SetupRideHeightPostHook)
            {
                m_SetupRideHeightPreHook.reset();
                m_SetupRideHeightPostHook.reset();
            }
        }

        if (settings.m_FixWetTireTable)
        {
            PatchWetTireTable();
        }

        m_Installed = true;
        return true;
    }

    void CarFixes::Uninstall()
    {
        if (!m_Installed)
            return;

        m_UsedCarGarageLimit.Restore();
        m_NewCarGarageLimit.Restore();
        m_UsedCarCandidateHook.reset();
        m_UsedCarAdvanceHook.reset();
        m_UsedCarDisplayHook.reset();
        m_UsedCarAgePatch.Restore();
        RestoreWetTireTable();
        m_SetupRideHeightPostHook.reset();
        m_SetupRideHeightPreHook.reset();
        m_PartsLoopEndHook.reset();
        m_EngineSwapStartHook.reset();
        m_PartsLoopStartHook.reset();
        m_SuperchargerStrPatch.Restore();
        m_CarListPostLoadCall.Restore();
        m_LoadPartsHook.reset();
        m_SwappedCar = 0;
        m_SwappedSlot = -1;
        m_SavedSpringCar = 0;
        m_SavedSpringLowering = 0.0f;
        m_Installed = false;
    }
}
