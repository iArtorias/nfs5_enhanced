#pragma once

// Front end lists scrolled by the mouse wheel and the arrow keys.
// Lists only move by dragging the scroll bar in the original. The game reads the wheel, but no
// control ever receives it, and both list classes leave keys unhandled. The list under the mouse
// is remembered from its mouse move slot, and its scroll accessor is moved the way the arrow
// buttons of the scroll bar do it ['sub_4DB040': move the accessor, then update the knob].
// Wheel over a closed drop down list steps its selection like a click on the next entry.

#include <cstdint>
#include <memory>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../game/game_addr.h"

namespace nfs5
{
    class MenuScroll
    {
    private:

        // One verified vtable slot, the original pointer is put back by 'Restore'.
        class SlotPatch
        {
        private:

            std::uintptr_t * m_Slot = nullptr;
            std::uintptr_t m_Original = 0;

        public:

            bool Apply(
                const char * name,
                game::GameAddr slot,
                game::GameAddr expected,
                void * destination
            );

            void Restore();

            [[nodiscard]] std::uintptr_t GetOriginal() const
            {
                return m_Original;
            }

            [[nodiscard]] bool IsApplied() const
            {
                return m_Slot != nullptr;
            }
        };

        MenuSettings m_Settings;
        SlotPatch m_ComboDtorPatch;
        SlotPatch m_ComboMovePatch;
        SlotPatch m_ListDtorPatch;
        SlotPatch m_ListMovePatch;
        SlotPatch m_PollPatch;
        SlotPatch m_DispatchPatch;
        std::unique_ptr<safetyhook::InlineHook> m_WheelHook;

        // Last list under the mouse and last list seen open, cleared by the destructor hook.
        void * m_ListHover = nullptr;
        void * m_ListOpen = nullptr;

        // 'WHEEL_DELTA' units since the last poll, written on the window thread.
        volatile long m_WheelRaw = 0;
        int m_WheelRest = 0;
        bool m_SliderWarned = false;
        bool m_Installed = false;

        MenuScroll() = default;

        bool ScrollList(
            void * list,
            int rows,
            bool page
        );

        void StepSelection(
            void * list,
            int steps
        );

        void * WheelTarget() const;

        void * ClosedTarget() const;

        void * KeyTarget() const;

        static void * __fastcall ListDtorHook(
            void * list,
            void * unused_edx,
            int flags
        );

        static int __fastcall ListMoveHook(
            void * list,
            void * unused_edx,
            void * event
        );

        static int __stdcall WheelMessageHook(
            int context,
            HWND window,
            UINT message,
            WPARAM wparam,
            LPARAM lparam,
            LRESULT * result
        );

        static void __fastcall PumpPollHook(
            void * pump
        );

        static void __fastcall PumpDispatchHook(
            void * pump,
            void * unused_edx,
            void * event
        );

    public:

        MenuScroll( const MenuScroll & ) = delete;
        MenuScroll & operator=( const MenuScroll & ) = delete;

        static MenuScroll & GetInstance();

        bool Install(
            const MenuSettings & settings
        );

        void Uninstall();
    };
}
