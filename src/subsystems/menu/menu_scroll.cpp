#include "menu_scroll.h"

#include <algorithm>
#include <initializer_list>

#include "../../core/hook_util.h"
#include "../../core/log.h"
#include "../../core/win_handle.h"
#include "../../game/addresses.h"

namespace nfs5
{
    namespace
    {
        namespace menu = game::menu;

        using ListDtorFn = void * (__thiscall *)(void * list, int flags);
        using ListEventFn = int( __thiscall * )(void * list, void * event);
        using PumpFn = void( __thiscall * )(void * pump);
        using PumpEventFn = void( __thiscall * )(void * pump, void * event);
        using ControlRectFn = int * (__thiscall *)(void * control, int * out);
        using ControlTestFn = int( __fastcall * )(void * control);
        using PartHitFn = int( __thiscall * )(void * part, const int * rect, int x, int y);
        using ThisIntFn = int( __thiscall * )(void * object);
        using ThisSetFn = void( __thiscall * )(void * object, int value);
        using ThisSetIntFn = int( __thiscall * )(void * object, int value);
        using ThisVoidFn = void( __thiscall * )(void * object);
        using DynamicCastFn = void * (__cdecl *)(void * object, int vf_delta, std::uintptr_t from, std::uintptr_t to, int is_reference);

        template <typename T>
        T & At(
            void * base,
            std::size_t offset
        )
        {
            return *reinterpret_cast<T *>(static_cast<std::uint8_t *>(base) + offset);
        }

        template <typename F>
        F Virtual(
            void * object,
            std::size_t offset
        )
        {
            return *reinterpret_cast<F *>(*static_cast<std::uint8_t **>(object) + offset);
        }

        bool IsList(
            void * pointer
        )
        {
            if (pointer == nullptr)
                return false;

            const auto vtable = *static_cast<std::uintptr_t *>(pointer);
            return vtable == menu::COMBOBOX_VTABLE.Get() || vtable == menu::LISTBOX_VTABLE.Get();
        }

        bool IsOpen(
            void * list
        )
        {
            return (At<std::uint32_t>( list, menu::CONTROL_FLAGS ) & menu::CONTROL_OPEN) != 0;
        }

        bool IsHidden(
            void * control
        )
        {
            return (reinterpret_cast<ControlTestFn>(menu::CONTROL_HIDDEN.Get())(control) & 0xFF) != 0;
        }

        // Visible and open, an 'FEListBox' always is.
        bool ListShown(
            void * list
        )
        {
            return IsList( list ) && !IsHidden( list ) && (At<char>( list, menu::LIST_ALWAYS_OPEN ) != 0 || IsOpen( list ));
        }

        // The hover test at the end of 'sub_4C6F60': selection box, closed box or scroll bar.
        bool ListUnder(
            void * list,
            int x,
            int y
        )
        {
            int rect[4] = {};
            reinterpret_cast<ControlRectFn>(menu::CONTROL_RECT.Get())(list, rect);

            for (std::size_t part : { menu::LIST_BOX_PART, menu::LIST_LABEL_PART })
            {
                void * p = At<void *>( list, part );

                if (p != nullptr && (reinterpret_cast<PartHitFn>(menu::PART_HIT.Get())(p, rect, x, y) & 0xFF) != 0)
                    return true;
            }

            void * slider = At<void *>( list, menu::LIST_SLIDER );
            return At<char>( list, menu::LIST_HAS_SLIDER ) != 0 && slider != nullptr &&
                (reinterpret_cast<ControlTestFn>(menu::SLIDER_HOVER.Get())(slider) & 0xFF) != 0;
        }

        // Only the closed box [part +17Ch].
        bool LabelUnder(
            void * list,
            int x,
            int y
        )
        {
            int rect[4] = {};
            reinterpret_cast<ControlRectFn>(menu::CONTROL_RECT.Get())(list, rect);
            void * label = At<void *>( list, menu::LIST_LABEL_PART );
            return label != nullptr && (reinterpret_cast<PartHitFn>(menu::PART_HIT.Get())(label, rect, x, y) & 0xFF) != 0;
        }

        int MouseX()
        {
            return hook::Ref<int>( menu::MOUSE_X );
        }

        int MouseY()
        {
            return hook::Ref<int>( menu::MOUSE_Y );
        }

        bool ListUnderMouse(
            void * list
        )
        {
            return ListUnder( list, MouseX(), MouseY() );
        }
    }


    bool MenuScroll::SlotPatch::Apply(
        const char * name,
        game::GameAddr slot_address,
        game::GameAddr expected_address,
        void * destination
    )
    {
        const std::uintptr_t slot = slot_address.Get();
        const std::uintptr_t expected = expected_address.Get();
        auto * entry = reinterpret_cast<std::uintptr_t *>(slot);

        if (*entry != expected)
        {
            LOG_ERROR( "{} vtable slot {:08X} holds {:08X} instead of {:08X}, skipped", name, slot, *entry, expected );
            return false;
        }

        {
            ScopedPageProtection protection( entry, sizeof( *entry ), PAGE_READWRITE );

            if (!protection.IsChanged())
            {
                LOG_ERROR( "{} vtable slot {:08X} cannot be unprotected", name, slot );
                return false;
            }

            *entry = reinterpret_cast<std::uintptr_t>(destination);
        }

        m_Slot = entry;
        m_Original = expected;
        LOG_INFO( "{} vtable slot {:08X} hooked", name, slot );
        return true;
    }


    void MenuScroll::SlotPatch::Restore()
    {
        if (m_Slot == nullptr)
            return;

        {
            ScopedPageProtection protection( m_Slot, sizeof( *m_Slot ), PAGE_READWRITE );

            if (protection.IsChanged())
            {
                *m_Slot = m_Original;
            }
        }

        m_Slot = nullptr;
    }


    MenuScroll & MenuScroll::GetInstance()
    {
        static MenuScroll instance;
        return instance;
    }


    // Moves the top row by 'rows' [pages with 'page'], false when the list has no scroll bar.
    bool MenuScroll::ScrollList(
        void * list,
        int rows,
        bool page
    )
    {
        void * scroll = At<void *>( list, menu::LIST_SCROLL );
        void * slider = At<void *>( list, menu::LIST_SLIDER );
        void * data = At<void *>( list, menu::LIST_DATA );

        if (At<char>( list, menu::LIST_HAS_SLIDER ) == 0 || scroll == nullptr || slider == nullptr || data == nullptr)
            return false;

        if (*static_cast<std::uintptr_t *>(slider) != menu::LIST_SLIDER_VTABLE.Get())
        {
            if (!m_SliderWarned)
            {
                LOG_WARN( "Menu list {} has an unknown scroll bar [vtable {:08X}], not scrolled", list, *static_cast<std::uintptr_t *>(slider) );
                m_SliderWarned = true;
            }

            return false;
        }

        const int count = Virtual<ThisIntFn>( scroll, 0x24 )(scroll);
        const int top = Virtual<ThisIntFn>( scroll, 0x28 )(scroll);

        // The scroll accessor only counts the possible top rows [entries - last page + 1], dragging the
        // bar ends at 'count - 1' with the last entry at the bottom.
        const int max_top = std::max( 0, count - 1 );

        // Visible rows for page up and down, from the height of each entry [data +68h].
        const int items = Virtual<ThisIntFn>( data, 0x24 )(data);
        const int box_height = At<int>( list, menu::LIST_MAX_HEIGHT );
        int visible = 0;

        for (int i = top, sum = 0; i < items; ++i, ++visible)
        {
            sum += std::max( 0, Virtual<ThisSetIntFn>( data, 0x68 )(data, i) );

            if (sum > box_height)
                break;
        }

        visible = std::max( 1, visible );

        if (page)
        {
            rows *= visible;
        }

        const int want = std::min( max_top, std::max( 0, top + rows ) );

        if (want != top)
        {
            Virtual<ThisSetFn>( scroll, 0x2C )(scroll, want);

            // Moves the knob along ['sub_4DB220'].
            Virtual<ThisVoidFn>( slider, 0x2C )(slider);

            // The highlight under the mouse is picked again.
            if (void * front_end = hook::Ref<void *>( menu::FRONT_END ); front_end != nullptr)
            {
                reinterpret_cast<ControlTestFn>(menu::REFRESH_MOUSE.Get())(front_end);
            }
        }

        LOG_TRACE( "Menu list {}: top {} -> {} [positions {}, entries {}, visible {}, box {}]", list, top, want, count, items, visible, box_height );
        return true;
    }


    // Moves the selection by 'steps' entries like a click on the entry ['sub_4C6AF0': data +2Ch].
    // Colour lists [+1DEh] skip the 'own colour' entry, a click on it opens the colour picker.
    void MenuScroll::StepSelection(
        void * list,
        int steps
    )
    {
        void * data = At<void *>( list, menu::LIST_DATA );

        if (data == nullptr || steps == 0)
            return;

        const int items = Virtual<ThisIntFn>( data, 0x24 )(data);
        const int current = Virtual<ThisIntFn>( data, 0x28 )(data);
        void * colours = At<char>( list, menu::LIST_COLOUR_SPECIAL ) != 0
            ? reinterpret_cast<DynamicCastFn>(menu::DYNAMIC_CAST.Get())(data, 0, menu::TD_POSITIONABLE_ACCESSOR.Get(), menu::TD_COLOUR_ACCESSOR.Get(), 0)
            : nullptr;

        const int direction = steps > 0 ? 1 : -1;
        int want = current;

        for (int left = steps * direction, i = current + direction; left > 0 && i >= 0 && i < items; i += direction)
        {
            if (colours != nullptr && (reinterpret_cast<ThisSetIntFn>(menu::COLOUR_IS_SPECIAL.Get())(colours, i) & 0xFF) != 0)
                continue;

            want = i;
            --left;
        }

        if (want != current)
        {
            Virtual<ThisSetFn>( data, 0x2C )(data, want);
        }

        LOG_TRACE( "Menu list {} closed: selection {} -> {} [entries {}]", list, current, want, items );
    }


    // Wheel: the list under the mouse, else the open one.
    void * MenuScroll::WheelTarget() const
    {
        for (void * list : { m_ListHover, m_ListOpen })
        {
            if (ListShown( list ) && ListUnderMouse( list ))
                return list;
        }

        return ListShown( m_ListOpen ) ? m_ListOpen : nullptr;
    }


    // Wheel over a closed drop down list: the closed box is under the mouse and the list can be
    // opened [+110h bit 40h, the condition of 'sub_4C7D70'].
    void * MenuScroll::ClosedTarget() const
    {
        void * list = m_ListHover;

        if (!IsList( list ) || At<char>( list, menu::LIST_ALWAYS_OPEN ) != 0 || IsOpen( list ) ||
            (At<std::uint32_t>( list, menu::CONTROL_CAN_OPEN ) & menu::CONTROL_OPEN) == 0 || IsHidden( list ))
        {
            return nullptr;
        }

        return LabelUnder( list, MouseX(), MouseY() ) ? list : nullptr;
    }


    // Keys: the open drop down list, else the always open list under the mouse.
    void * MenuScroll::KeyTarget() const
    {
        if (ListShown( m_ListOpen ) && IsOpen( m_ListOpen ))
            return m_ListOpen;

        return ListShown( m_ListHover ) && ListUnderMouse( m_ListHover ) ? m_ListHover : nullptr;
    }


    void * __fastcall MenuScroll::ListDtorHook(
        void * list,
        void *,
        int flags
    )
    {
        MenuScroll & self = GetInstance();

        if (self.m_ListHover == list)
        {
            self.m_ListHover = nullptr;
        }

        if (self.m_ListOpen == list)
        {
            self.m_ListOpen = nullptr;
        }

        return reinterpret_cast<ListDtorFn>(menu::LIST_DTOR.Get())(list, flags);
    }


    int __fastcall MenuScroll::ListMoveHook(
        void * list,
        void *,
        void * event
    )
    {
        MenuScroll & self = GetInstance();
        const int result = reinterpret_cast<ListEventFn>(menu::LIST_MOVE.Get())(list, event);

        const int x = At<int>( event, 8 );
        const int y = At<int>( event, 0xC );
        const bool open = At<char>( list, menu::LIST_ALWAYS_OPEN ) != 0 || IsOpen( list );

        if (open ? ListUnder( list, x, y ) : LabelUnder( list, x, y ))
        {
            self.m_ListHover = list;
        }

        if (IsOpen( list ))
        {
            self.m_ListOpen = list;
        }

        return result;
    }


    int __stdcall MenuScroll::WheelMessageHook(
        int context,
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        LRESULT * result
    )
    {
        MenuScroll & self = GetInstance();

        if (context != 0 && window == hook::Ref<HWND>( menu::GAME_WINDOW ))
        {
            ::InterlockedExchangeAdd( &self.m_WheelRaw, static_cast<short>(HIWORD( wparam )) );
        }

        return self.m_WheelHook->stdcall<int>( context, window, message, wparam, lparam, result );
    }


    // Input poll of the front end, every frame on the menu thread.
    void __fastcall MenuScroll::PumpPollHook(
        void * pump
    )
    {
        MenuScroll & self = GetInstance();
        reinterpret_cast<PumpFn>(menu::EVENT_PUMP_POLL.Get())(pump);

        self.m_WheelRest += ::InterlockedExchange( &self.m_WheelRaw, 0 );
        const int notches = self.m_WheelRest / WHEEL_DELTA;

        if (notches == 0)
            return;

        self.m_WheelRest -= notches * WHEEL_DELTA;

        // Wheel forward scrolls up and picks the previous entry.
        if (void * list = self.WheelTarget(); list != nullptr)
        {
            self.ScrollList( list, -notches * self.m_Settings.m_WheelRows, false );
        }
        else if (self.m_Settings.m_WheelSelect)
        {
            if (void * closed = self.ClosedTarget(); closed != nullptr)
            {
                self.StepSelection( closed, -notches );
            }
        }
    }


    // A key used by a list never reaches the menu.
    void __fastcall MenuScroll::PumpDispatchHook(
        void * pump,
        void *,
        void * event
    )
    {
        MenuScroll & self = GetInstance();

        if (event != nullptr && At<int>( event, 4 ) == menu::EVENT_KEY)
        {
            int rows = 0;
            bool page = false;

            switch (At<int>( event, 8 ) & 0xFFFF)
            {
            case menu::KEY_UP: rows = -1; break;
            case menu::KEY_DOWN: rows = 1; break;
            case menu::KEY_PAGE_UP: page = true; rows = -1; break;
            case menu::KEY_PAGE_DOWN: page = true; rows = 1; break;
            case menu::KEY_HOME: rows = -100000; break;
            case menu::KEY_END: rows = 100000; break;
            default: break;
            }

            void * list = rows != 0 ? self.KeyTarget() : nullptr;

            if (list != nullptr && self.ScrollList( list, rows, page ))
                return;
        }

        reinterpret_cast<PumpEventFn>(menu::EVENT_PUMP_DISPATCH.Get())(pump, event);
    }


    bool MenuScroll::Install(
        const MenuSettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Scroll)
        {
            LOG_INFO( "Menu scrolling disabled" );
            return true;
        }

        // Destructor and mouse move first, without them no list is remembered.
        bool ok = m_ComboDtorPatch.Apply( "ComboBoxDtor", menu::COMBOBOX_VTABLE + menu::LIST_DTOR_SLOT, menu::LIST_DTOR,
            reinterpret_cast<void *>(&ListDtorHook) );
        ok = m_ComboMovePatch.Apply( "ComboBoxMouseMove", menu::COMBOBOX_VTABLE + menu::LIST_MOVE_SLOT, menu::LIST_MOVE,
            reinterpret_cast<void *>(&ListMoveHook) ) && ok;
        ok = m_ListDtorPatch.Apply( "ListBoxDtor", menu::LISTBOX_VTABLE + menu::LIST_DTOR_SLOT, menu::LIST_DTOR,
            reinterpret_cast<void *>(&ListDtorHook) ) && ok;
        ok = m_ListMovePatch.Apply( "ListBoxMouseMove", menu::LISTBOX_VTABLE + menu::LIST_MOVE_SLOT, menu::LIST_MOVE,
            reinterpret_cast<void *>(&ListMoveHook) ) && ok;

        if (!ok)
        {
            LOG_ERROR( "Menu scrolling not installed, the list classes do not match" );
            Uninstall();
            return false;
        }

        m_WheelHook = hook::CreateInline( "MenuWheelMessage", menu::WHEEL_MESSAGE.m_Address, menu::WHEEL_MESSAGE.m_Signature,
            reinterpret_cast<void *>(&WheelMessageHook) );

        if (m_WheelHook != nullptr)
        {
            ok = m_PollPatch.Apply( "EventPumpPoll", menu::EVENT_PUMP_VTABLE + menu::EVENT_PUMP_POLL_SLOT, menu::EVENT_PUMP_POLL,
                reinterpret_cast<void *>(&PumpPollHook) ) && ok;
        }
        else
        {
            ok = false;
        }

        ok = m_DispatchPatch.Apply( "EventPumpDispatch", menu::EVENT_PUMP_VTABLE + menu::EVENT_PUMP_DISPATCH_SLOT, menu::EVENT_PUMP_DISPATCH,
            reinterpret_cast<void *>(&PumpDispatchHook) ) && ok;

        m_Installed = true;

        LOG_INFO( "Menu scrolling installed, wheel rows {} wheel select {} all ok {}", settings.m_WheelRows, settings.m_WheelSelect, ok );
        return ok;
    }


    void MenuScroll::Uninstall()
    {
        m_DispatchPatch.Restore();
        m_PollPatch.Restore();
        m_WheelHook.reset();
        m_ListMovePatch.Restore();
        m_ListDtorPatch.Restore();
        m_ComboMovePatch.Restore();
        m_ComboDtorPatch.Restore();
        m_ListHover = nullptr;
        m_ListOpen = nullptr;
        m_Installed = false;
    }
}
