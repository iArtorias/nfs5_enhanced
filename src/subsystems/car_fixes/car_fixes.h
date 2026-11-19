#pragma once

// Runtime fixes for car/part database bugs, authoring quirks, and upgrade physics defects.
//
// Data fixes run in memory immediately after 'nfs5.car' ['sub_4F3B60'] and 'nfs5.prt'
// ['sub_49C3A0'] are loaded, matching records by Car ID / Part ID and validating the stock
// buggy values before writing so external overrides [such as GameplayFixes] are left intact.
// Executable fixes patch 'Porsche.exe' v3.5 via verified hooks and restore cleanly on unload.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/hook_util.h"
#include "../../game/addresses.h"

namespace nfs5
{
    class CarFixes
    {
    private:

        CarFixesSettings m_Settings;
        std::unique_ptr<safetyhook::InlineHook> m_LoadPartsHook;
        hook::CallSitePatch m_CarListPostLoadCall;
        hook::BytePatch m_SuperchargerStrPatch;
        std::unique_ptr<safetyhook::MidHook> m_PartsLoopStartHook;
        std::unique_ptr<safetyhook::MidHook> m_EngineSwapStartHook;
        std::unique_ptr<safetyhook::MidHook> m_PartsLoopEndHook;
        std::unique_ptr<safetyhook::MidHook> m_SetupRideHeightPreHook;
        std::unique_ptr<safetyhook::MidHook> m_SetupRideHeightPostHook;
        std::array<float, game::car_fixes::WET_TIRE_TABLE_FLOATS> m_OriginalWetTireTable = {};
        bool m_WetTireTablePatched = false;
        std::uintptr_t m_SwappedCar = 0;
        int m_SwappedSlot = -1;
        std::uintptr_t m_SavedSpringCar = 0;
        float m_SavedSpringLowering = 0.0f;
        bool m_Installed = false;

        CarFixes() = default;

        static void __cdecl LoadPartsHook();
        static void __cdecl CarListPostLoadHook();

        static void PartsLoopStartHook(
            safetyhook::Context & context
        );

        static void EngineSwapStartHook(
            safetyhook::Context & context
        );

        static void PartsLoopEndHook(
            safetyhook::Context & context
        );

        static void SetupRideHeightPreHook(
            safetyhook::Context & context
        );

        static void SetupRideHeightPostHook(
            safetyhook::Context & context
        );

        void PatchLoadedParts();
        void PatchLoadedCars();
        bool PatchWetTireTable();
        void RestoreWetTireTable();

    public:

        CarFixes( const CarFixes & ) = delete;
        CarFixes & operator=( const CarFixes & ) = delete;

        static CarFixes & GetInstance();

        bool Install(
            const CarFixesSettings & settings
        );

        void Uninstall();
    };
}
