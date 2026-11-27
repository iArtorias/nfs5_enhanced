#include "damage_cull.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "game/addresses.h"

namespace nfs5
{
    namespace
    {
        // R3DCar: damage applied to the mesh per zone, written after each damage update.
        constexpr std::size_t APPLIED_DAMAGE = 0x6A8;
        constexpr std::size_t ZONE_COUNT = 10;

        // Cars already reported as damaged, for the log only.
        std::array< std::atomic< const void * >, 16 > s_Reported = {};

        // Called from the patched site with the R3DCar of the part (may be null). Nonzero unbinds
        // the plane stream of the part, zero continues with the game's own check.
        int __cdecl CarIsDamaged( const std::uint8_t * car )
        {
            if (car == nullptr)
                return 0;

            const auto * applied = reinterpret_cast<const float *>(car + APPLIED_DAMAGE);
            float worst = 0.0f;

            for (std::size_t i = 0; i < ZONE_COUNT; ++i)
            {
                if (applied[i] > worst)
                    worst = applied[i];
            }

            if (worst <= 0.0f)
                return 0;

            for (auto & slot : s_Reported)
            {
                const void * seen = slot.load( std::memory_order_relaxed );

                if (seen == car)
                    break;

                if (seen == nullptr)
                {
                    const void * expected = nullptr;

                    if (slot.compare_exchange_strong( expected, car ))
                    {
                        LOG_INFO( "DamageCullFix: car {} damaged (worst zone {:.1f}), dented parts drawn without plane culling",
                            static_cast<const void *>(car), worst );
                        break;
                    }

                    if (expected == car)
                        break;
                }
            }

            return 1;
        }
    }

    DamageCull & DamageCull::GetInstance()
    {
        static DamageCull instance;
        return instance;
    }

    void DamageCull::Install( int mode )
    {
        namespace fn = game::fn;

        if (mode == 1)
        {
            // Jump over the menu check straight to the unbind: 'jmp short +0x20', 3 nops.
            constexpr std::uint8_t JUMP[] = { 0xEB, 0x20, 0x90, 0x90, 0x90 };
            m_Patch.Apply( "DamageCullFix 1 (menu rule in races)", fn::DAMAGE_CULL.m_Address + fn::DAMAGE_CULL_MENU,
                "A1 ? ? ? ? 85 C0 75 19", JUMP );
        }
        else if (mode == 2)
        {
            // push [ebp - 0x28] (R3DCar), call CarIsDamaged, add esp 4, test eax eax,
            // jne unbind (+0x1B), 2 nops into the game's TINYCAR check right after. 17 bytes.
            const std::uintptr_t site = fn::DAMAGE_CULL.m_Address.Get();
            std::array< std::uint8_t, 17 > code = {
                0xFF, 0x75, 0xD8,
                0xE8, 0, 0, 0, 0,
                0x83, 0xC4, 0x04,
                0x85, 0xC0,
                0x75, 0x1B,
                0x90, 0x90
            };
            const auto call = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&CarIsDamaged) - (site + 8));
            std::memcpy( code.data() + 4, &call, sizeof( call ) );
            m_Patch.Apply( "DamageCullFix 2 (damaged cars)", site, fn::DAMAGE_CULL.m_Signature, code );
        }

        LOG_INFO( "DamageCullFix {}: {}", mode,
            mode == 0 ? "off" : (m_Patch.IsApplied() ? "active" : "not applied, site does not match") );
    }

    void DamageCull::Uninstall()
    {
        m_Patch.Restore();
    }
}
