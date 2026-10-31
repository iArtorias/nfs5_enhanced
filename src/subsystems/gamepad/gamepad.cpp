#include "gamepad.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../core/hook_util.h"
#include "../../core/log.h"

#include "../../game/addresses.h"

namespace nfs5
{
    namespace
    {
        namespace gi = game::input;

        // SDL thread wakes at least this often to refresh the state, milliseconds.
        constexpr Sint32 POLL_INTERVAL_MS = 4;

        // How long 'INPUT_init' waits for SDL to list the pads already plugged in.
        constexpr DWORD READY_TIMEOUT_MS = 2000;

        // Rumble is sent for this long and refreshed before it runs out.
        constexpr Uint32 RUMBLE_DURATION_MS = 1000;
        constexpr std::int64_t RUMBLE_REFRESH_MS = 400;

        // A stick moved this far or any button change makes a pad the one in use.
        constexpr float ACTIVITY_THRESHOLD = 0.35f;

        // Binding of one action as the controller screen stores it.
        struct Binding
        {
            std::int32_t m_Type;
            std::int32_t m_Device;

            // Axis 0 X, 1 Y, 2 Z, 3 Rx, 4 Ry, 5 Rz, 6 and 7 sliders, or the button number.
            std::int32_t m_Index;

            // Axis half, '1' toward the maximum, '-1' toward the minimum.
            std::int32_t m_Direction;
            std::int32_t m_Pov;
            std::int32_t m_Key;
        };

        enum BindingType : std::int32_t
        {
            BINDING_NONE = 0,
            BINDING_AXIS = 1,
            BINDING_BUTTON = 2,
            BINDING_KEY = 3,
            BINDING_POV = 5
        };

        struct GamepadDefault
        {
            const char * m_Action;
            BindingType m_Type;
            std::int32_t m_Index;
            std::int32_t m_Direction;
        };

        // Gamepad layout of 'VirtualGamepad', triggers rest in the middle of Rx and Ry and
        // press toward the maximum. Actions not listed keep the keyboard defaults of the game.
        constexpr GamepadDefault GAMEPAD_DEFAULTS[] =
        {
            { "Controller.Accel", BINDING_AXIS, 4, 1 }, // Right trigger.
            { "Controller.Brake", BINDING_AXIS, 3, 1 }, // Left trigger.
            { "Controller.SteerLeft", BINDING_AXIS, 0, -1 }, // Left stick.
            { "Controller.SteerRight", BINDING_AXIS, 0, 1 },
            { "Controller.HandBrake", BINDING_BUTTON, 0, 0 }, // A, Cross.
            { "Controller.CameraView", BINDING_BUTTON, 1, 0 }, // B, Circle.
            { "Controller.LookBehind", BINDING_BUTTON, 2, 0 }, // X, Square.
            { "Controller.ResetCar", BINDING_BUTTON, 3, 0 }, // Y, Triangle.
            { "Controller.ShiftUp", BINDING_BUTTON, 7, 0 }, // Right shoulder.
            { "Controller.ShiftDown", BINDING_BUTTON, 8, 0 }, // Left shoulder.
            { "Controller.Pause", BINDING_BUTTON, 6, 0 }, // Start, Options.
        };

        using FindAccessorFn = void * (__cdecl *)(const char * name);

        using ClearBindingFn = char( __cdecl * )(
            std::int32_t type,
            std::int32_t device,
            std::int32_t index,
            std::int32_t direction,
            std::int32_t pov,
            std::int32_t key
            );

        std::int64_t QpcNow()
        {
            LARGE_INTEGER value = {};
            ::QueryPerformanceCounter( &value );
            return value.QuadPart;
        }


        std::int64_t QpcFromMs(
            std::int64_t milliseconds
        )
        {
            static const std::int64_t frequency = []
            {
                LARGE_INTEGER value = {};
                ::QueryPerformanceFrequency( &value );
                return value.QuadPart > 0 ? value.QuadPart : 1;
            }();

            return milliseconds * frequency / 1000;
        }


        constexpr std::uint32_t Bit(
            int button
        )
        {
            return 1u << button;
        }


        float NormalizeStick(
            Sint16 value
        )
        {
            return std::clamp( static_cast<float>(value) / 32767.0f, -1.0f, 1.0f );
        }


        float NormalizeTrigger(
            Sint16 value
        )
        {
            return std::clamp( static_cast<float>(value) / 32767.0f, 0.0f, 1.0f );
        }


        // Radial dead zone, the remaining travel is stretched back to the full range.
        void ApplyStickDeadzone(
            float & x,
            float & y,
            float deadzone
        )
        {
            const float magnitude = std::sqrt( x * x + y * y );

            if (magnitude <= deadzone || magnitude <= 0.0f)
            {
                x = 0.0f;
                y = 0.0f;
                return;
            }

            const float scale = std::min( 1.0f, (magnitude - deadzone) / (1.0f - deadzone) ) / magnitude;
            x = std::clamp( x * scale, -1.0f, 1.0f );
            y = std::clamp( y * scale, -1.0f, 1.0f );
        }


        float ApplyTriggerDeadzone(
            float value,
            float deadzone
        )
        {
            if (value <= deadzone)
                return 0.0f;

            return std::clamp( (value - deadzone) / (1.0f - deadzone), 0.0f, 1.0f );
        }


        // DirectInput HID product GUIDs carry 'PIDVID' and the ids in the first field.
        bool ProductIds(
            const GUID & product,
            std::uint16_t & vendor,
            std::uint16_t & id
        )
        {
            static constexpr unsigned char SIGNATURE[8] = { 0x00, 0x00, 'P', 'I', 'D', 'V', 'I', 'D' };

            if (product.Data2 != 0 || product.Data3 != 0 || std::memcmp( product.Data4, SIGNATURE, sizeof( SIGNATURE ) ) != 0)
                return false;

            vendor = LOWORD( product.Data1 );
            id = HIWORD( product.Data1 );
            return true;
        }
    }

    Gamepad & Gamepad::GetInstance()
    {
        static Gamepad instance;
        return instance;
    }

    // Game thread.

    void Gamepad::ReadPad(
        input::PadSnapshot & out
    )
    {
        std::scoped_lock lock( m_StateLock );
        out = m_Pad;
    }


    void Gamepad::InsertDevice(
        int & count
    )
    {
        if (count < 0 || count >= gi::MAX_DEVICES)
        {
            LOG_WARN( "Controller table full, the SDL gamepad is not listed" );
            return;
        }

        auto * entry = hook::Ptr<std::uint32_t>( gi::DEVICE_TABLE + gi::DEVICE_STRIDE * static_cast<std::size_t>( count ) );
        std::memset( entry, 0, gi::DEVICE_STRIDE );

        entry[0] = static_cast<std::uint32_t>( reinterpret_cast<std::uintptr_t>( static_cast<IDirectInputDevice2A *>( &m_Device ) ) );
        entry[2] = input::VirtualGamepad::DEVICE_TYPE;

        m_VirtualIndex = count;
        ++count;

        LOG_INFO( "SDL gamepad listed as controller {}", m_VirtualIndex );
    }


    bool Gamepad::IsHandledBySdl(
        const DIDEVICEINSTANCEA * instance
    ) const
    {
        std::uint16_t vendor = 0;
        std::uint16_t product = 0;

        if (!m_SdlReady.load() || !ProductIds( instance->guidProduct, vendor, product ))
            return false;

        const std::uint32_t key = (static_cast<std::uint32_t>(vendor) << 16) | product;

        std::scoped_lock lock( m_StateLock );
        return std::find( m_HandledIds.begin(), m_HandledIds.end(), key ) != m_HandledIds.end();
    }


    int __fastcall Gamepad::InputInitHook(
        void * self_pointer,
        void *
    )
    {
        Gamepad & self = GetInstance();
        const bool fresh = hook::Ref<void *>( gi::DIRECT_INPUT ) == nullptr;

        if (fresh)
        {
            self.m_VirtualIndex = -1;

            // The pads already plugged in have to be known to SDL before the list is built.
            if (self.m_ReadyEvent && ::WaitForSingleObject( self.m_ReadyEvent.Get(), READY_TIMEOUT_MS ) != WAIT_OBJECT_0)
            {
                LOG_WARN( "SDL did not list the controllers in time, DirectInput copies may show up" );
            }
        }

        const int result = self.m_InputInitHook->thiscall<int>( self_pointer );

        // No joystick was enumerated, the gamepad goes at the end of the list.
        if (fresh && result != 0 && self.m_VirtualIndex < 0 && hook::Ref<void *>( gi::DIRECT_INPUT ) != nullptr)
        {
            int & count = hook::Ref<int>( gi::DEVICE_COUNT );
            self.InsertDevice( count );
        }

        return result;
    }


    BOOL __stdcall Gamepad::EnumCallbackHook(
        const DIDEVICEINSTANCEA * instance,
        void * reference
    )
    {
        Gamepad & self = GetInstance();
        auto * count = static_cast<int *>(reference);

        if (instance != nullptr && count != nullptr && GET_DIDEVICE_TYPE( instance->dwDevType ) == DIDEVTYPE_JOYSTICK)
        {
            // Before the first joystick, keyboards and mice keep their places.
            if (self.m_VirtualIndex < 0)
            {
                self.InsertDevice( *count );
            }

            if (self.m_Settings.m_HideHandledDevices && self.IsHandledBySdl( instance ))
            {
                LOG_INFO( "DirectInput controller '{}' is read through SDL, not listed", instance->tszProductName );
                return *count < gi::MAX_DEVICES ? DIENUM_CONTINUE : DIENUM_STOP;
            }
        }

        return self.m_EnumCallbackHook->stdcall<BOOL>( instance, reference );
    }


    void __cdecl Gamepad::InputShutdownHook()
    {
        Gamepad & self = GetInstance();

        self.m_Device.StopAllEffects();

        // The motors are stopped by the SDL thread, wait a moment so they do not keep running.
        if (self.m_Thread && self.m_SdlReady.load())
        {
            const std::uint32_t request = self.m_RumbleRequest.fetch_add( 1 ) + 1;

            for (int i = 0; i < 25 && self.m_RumbleAck.load() < request; ++i)
            {
                ::Sleep( 2 );
            }
        }

        self.m_InputShutdownHook->ccall<void>();
    }


    char __cdecl Gamepad::DefaultBindingsHook(
        int device
    )
    {
        Gamepad & self = GetInstance();

        // The game binds what the device offers and the keyboard for the rest, the gamepad
        // actions are moved to the triggers and pad buttons afterwards.
        const char result = self.m_DefaultBindingsHook->ccall<char>( device );

        if (self.IsVirtualDevice( device ))
        {
            self.ApplyGamepadDefaults( device );
        }

        return result;
    }


    bool Gamepad::IsVirtualDevice(
        int device
    ) const
    {
        if (device < 0 || device != m_VirtualIndex || device >= hook::Ref<int>( gi::DEVICE_COUNT ))
            return false;

        const auto entry = hook::Ref<std::uintptr_t>( gi::DEVICE_TABLE + gi::DEVICE_STRIDE * static_cast<std::size_t>( device ) );
        return entry == reinterpret_cast<std::uintptr_t>( static_cast<const IDirectInputDevice2A *>( &m_Device ) );
    }


    void Gamepad::ApplyGamepadDefaults(
        int device
    ) const
    {
        const auto find_accessor = reinterpret_cast<FindAccessorFn>(gi::FIND_ACCESSOR.m_Address.Get());
        const auto clear_binding = reinterpret_cast<ClearBindingFn>(gi::CLEAR_BINDING.m_Address.Get());
        int applied = 0;

        for (const GamepadDefault & entry : GAMEPAD_DEFAULTS)
        {
            auto * accessor = static_cast<std::uint8_t *>(find_accessor( entry.m_Action ));

            if (accessor == nullptr || *reinterpret_cast<const std::uintptr_t *>(accessor) != gi::INPUT_ACCESSOR_VTABLE.Get())
            {
                LOG_WARN( "Controller action '{}' not found, its gamepad default is skipped", entry.m_Action );
                continue;
            }

            const Binding binding = { entry.m_Type, device, entry.m_Index, entry.m_Direction, 0, 0 };

            // Every other action on the same input is unbound first, as the binding screen does.
            clear_binding( binding.m_Type, binding.m_Device, binding.m_Index, binding.m_Direction, binding.m_Pov, binding.m_Key );
            std::memcpy( accessor + gi::ACCESSOR_BINDING, &binding, sizeof( binding ) );
            ++applied;
        }

        LOG_INFO( "Gamepad default bindings set for controller {}, {} of {} actions", device, applied, std::size( GAMEPAD_DEFAULTS ) );
    }

    // SDL thread.

    DWORD WINAPI Gamepad::ThreadProc(
        LPVOID parameter
    )
    {
        static_cast<Gamepad *>(parameter)->ThreadMain();
        return 0;
    }


    bool Gamepad::IsGameActive() const
    {
        const HWND foreground = ::GetForegroundWindow();

        if (foreground == nullptr)
            return false;

        DWORD process = 0;
        ::GetWindowThreadProcessId( foreground, &process );
        return process == ::GetCurrentProcessId();
    }


    void Gamepad::OpenPadById(
        SDL_JoystickID id
    )
    {
        for (const OpenPad & pad : m_Pads)
        {
            if (pad.m_Id == id)
                return;
        }

        SDL_Gamepad * handle = m_Sdl.OpenGamepad( id );

        if (handle == nullptr)
        {
            LOG_WARN( "SDL could not open gamepad {}: {}", static_cast<unsigned>(id), m_Sdl.GetError() );
            return;
        }

        OpenPad pad;
        pad.m_Handle = handle;
        pad.m_Id = id;
        pad.m_Vendor = m_Sdl.GetGamepadVendor( handle );
        pad.m_Product = m_Sdl.GetGamepadProduct( handle );
        pad.m_LastActivity = QpcNow();
        m_Pads.push_back( pad );

        // A pad plugged in is the one the player wants to use.
        m_ActiveId = id;

        const char * name = m_Sdl.GetGamepadName( handle );
        LOG_INFO( "Gamepad connected: {} [{:04X}:{:04X}]", name != nullptr ? name : "unknown", pad.m_Vendor, pad.m_Product );
    }


    void Gamepad::ClosePadById(
        SDL_JoystickID id
    )
    {
        const auto it = std::find_if( m_Pads.begin(), m_Pads.end(), [id] ( const OpenPad & pad )
        {
            return pad.m_Id == id;
        } );

        if (it == m_Pads.end())
            return;

        const char * name = m_Sdl.GetGamepadName( it->m_Handle );
        LOG_INFO( "Gamepad disconnected: {}", name != nullptr ? name : "unknown" );

        m_Sdl.CloseGamepad( it->m_Handle );
        m_Pads.erase( it );

        if (m_RumbleId == id)
        {
            m_RumbleId = 0;
            m_LastLow = 0.0f;
            m_LastHigh = 0.0f;
        }

        if (m_ActiveId == id)
        {
            m_ActiveId = m_Pads.empty() ? 0 : m_Pads.front().m_Id;
        }
    }


    void Gamepad::PublishHandledIds()
    {
        std::vector<std::uint32_t> ids;

        for (const OpenPad & pad : m_Pads)
        {
            ids.push_back( (static_cast<std::uint32_t>(pad.m_Vendor) << 16) | pad.m_Product );
        }

        std::scoped_lock lock( m_StateLock );
        m_HandledIds = std::move( ids );
    }


    void Gamepad::UpdatePads(
        std::int64_t now
    )
    {
        // Every pad is read, the one with the latest activity drives the game.
        for (OpenPad & pad : m_Pads)
        {
            std::uint32_t buttons = 0;

            for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT && button < 32; ++button)
            {
                if (m_Sdl.GetGamepadButton( pad.m_Handle, static_cast<SDL_GamepadButton>( button ) ))
                {
                    buttons |= Bit( button );
                }
            }

            float axes[6] = {};

            for (int axis = 0; axis < 6; ++axis)
            {
                const Sint16 raw = m_Sdl.GetGamepadAxis( pad.m_Handle, static_cast<SDL_GamepadAxis>( axis ) );
                axes[axis] = axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? NormalizeTrigger( raw ) : NormalizeStick( raw );
            }

            pad.m_Buttons = buttons;
            std::copy( std::begin( axes ), std::end( axes ), std::begin( pad.m_Axes ) );

            bool active = buttons != pad.m_LastButtons;

            for (int axis = 0; axis < 6 && !active; ++axis)
            {
                active = std::fabs( axes[axis] - pad.m_LastAxes[axis] ) > ACTIVITY_THRESHOLD;
            }

            if (active)
            {
                pad.m_LastActivity = now;
                pad.m_LastButtons = buttons;
                std::copy( std::begin( axes ), std::end( axes ), std::begin( pad.m_LastAxes ) );

                if (pad.m_Id != m_ActiveId && buttons != 0)
                {
                    m_ActiveId = pad.m_Id;
                }
            }
        }

        input::PadSnapshot snapshot;
        const auto active = std::find_if( m_Pads.begin(), m_Pads.end(), [this] ( const OpenPad & pad )
        {
            return pad.m_Id == m_ActiveId;
        } );

        if (active != m_Pads.end())
        {
            snapshot.m_Connected = true;

            const char * name = m_Sdl.GetGamepadName( active->m_Handle );
            std::strncpy( snapshot.m_Name, name != nullptr ? name : "Gamepad", sizeof( snapshot.m_Name ) - 1 );

            const bool readable = m_Settings.m_BackgroundInput || IsGameActive();

            if (readable)
            {
                snapshot.m_LeftX = active->m_Axes[SDL_GAMEPAD_AXIS_LEFTX];
                snapshot.m_LeftY = active->m_Axes[SDL_GAMEPAD_AXIS_LEFTY];
                snapshot.m_RightX = active->m_Axes[SDL_GAMEPAD_AXIS_RIGHTX];
                snapshot.m_RightY = active->m_Axes[SDL_GAMEPAD_AXIS_RIGHTY];
                snapshot.m_LeftTrigger = active->m_Axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER];
                snapshot.m_RightTrigger = active->m_Axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER];
                snapshot.m_Buttons = active->m_Buttons;

                ApplyStickDeadzone( snapshot.m_LeftX, snapshot.m_LeftY, m_Settings.m_StickDeadzone );
                ApplyStickDeadzone( snapshot.m_RightX, snapshot.m_RightY, m_Settings.m_StickDeadzone );
                snapshot.m_LeftTrigger = ApplyTriggerDeadzone( snapshot.m_LeftTrigger, m_Settings.m_TriggerDeadzone );
                snapshot.m_RightTrigger = ApplyTriggerDeadzone( snapshot.m_RightTrigger, m_Settings.m_TriggerDeadzone );
            }
        }

        {
            std::scoped_lock lock( m_StateLock );
            m_Pad = snapshot;
        }
    }


    void Gamepad::UpdateRumble(
        std::int64_t now
    )
    {
        float low = 0.0f;
        float high = 0.0f;

        if (m_Settings.m_Rumble)
        {
            m_Device.ComputeRumble( now, low, high );
            low = std::clamp( low * m_Settings.m_RumbleStrength, 0.0f, 1.0f );
            high = std::clamp( high * m_Settings.m_RumbleStrength, 0.0f, 1.0f );
        }

        // No rumble while the game is in the background.
        if (!IsGameActive())
        {
            low = 0.0f;
            high = 0.0f;
        }

        // Rumble follows the pad in use, the one used before is silenced.
        if (m_RumbleId != 0 && m_RumbleId != m_ActiveId)
        {
            const auto previous = std::find_if( m_Pads.begin(), m_Pads.end(), [this] ( const OpenPad & pad )
            {
                return pad.m_Id == m_RumbleId;
            } );

            if (previous != m_Pads.end())
            {
                m_Sdl.RumbleGamepad( previous->m_Handle, 0, 0, 0 );
            }

            m_RumbleId = 0;
            m_LastLow = 0.0f;
            m_LastHigh = 0.0f;
        }

        const auto active = std::find_if( m_Pads.begin(), m_Pads.end(), [this] ( const OpenPad & pad )
        {
            return pad.m_Id == m_ActiveId;
        } );

        if (active != m_Pads.end())
        {
            const bool changed = std::fabs( low - m_LastLow ) > 0.02f || std::fabs( high - m_LastHigh ) > 0.02f ||
                ((low == 0.0f) != (m_LastLow == 0.0f)) || ((high == 0.0f) != (m_LastHigh == 0.0f));
            const bool running = low > 0.0f || high > 0.0f;
            const bool refresh = running && now - m_LastRumbleSend >= QpcFromMs( RUMBLE_REFRESH_MS );

            if (changed || refresh)
            {
                const auto low_value = static_cast<Uint16>(std::lround( low * 65535.0f ));
                const auto high_value = static_cast<Uint16>(std::lround( high * 65535.0f ));
                m_Sdl.RumbleGamepad( active->m_Handle, low_value, high_value, running ? RUMBLE_DURATION_MS : 0 );

                m_RumbleId = active->m_Id;
                m_LastLow = low;
                m_LastHigh = high;
                m_LastRumbleSend = now;
            }
        }

        m_RumbleAck.store( m_RumbleRequest.load() );
    }


    void Gamepad::ThreadMain()
    {
        m_Sdl.SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );

        if (!m_Sdl.Init( SDL_INIT_GAMEPAD ))
        {
            LOG_ERROR( "SDL_Init failed: {}", m_Sdl.GetError() );
            ::SetEvent( m_ReadyEvent.Get() );
            return;
        }

        // Optional community mappings for unusual pads, 'gamecontrollerdb.txt' next to the ASI.
        const std::filesystem::path mappings = m_Folder / L"gamecontrollerdb.txt";
        std::error_code error;

        if (std::filesystem::exists( mappings, error ))
        {
            const int added = m_Sdl.AddGamepadMappingsFromFile( mappings.string().c_str() );
            LOG_INFO( "Gamepad mappings from 'gamecontrollerdb.txt': {}", added );
        }

        int count = 0;

        if (SDL_JoystickID * ids = m_Sdl.GetGamepads( &count ))
        {
            for (int i = 0; i < count; ++i)
            {
                OpenPadById( ids[i] );
            }

            m_Sdl.Free( ids );
        }

        // Drain the added events of the pads opened above.
        SDL_Event event = {};

        while (m_Sdl.PollEvent( &event ))
        {
            if (event.type == SDL_EVENT_GAMEPAD_ADDED)
            {
                OpenPadById( event.gdevice.which );
            }
        }

        PublishHandledIds();
        m_SdlReady.store( true );
        ::SetEvent( m_ReadyEvent.Get() );

        LOG_INFO( "SDL controller thread running, {} pad(s) connected", m_Pads.size() );

        while (!m_Stop.load())
        {
            bool devices_changed = false;

            if (m_Sdl.WaitEventTimeout( &event, POLL_INTERVAL_MS ))
            {
                do
                {
                    if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                    {
                        OpenPadById( event.gdevice.which );
                        devices_changed = true;
                    }
                    else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
                    {
                        ClosePadById( event.gdevice.which );
                        devices_changed = true;
                    }
                } while (m_Sdl.PollEvent( &event ));
            }

            if (devices_changed)
            {
                PublishHandledIds();
            }

            const std::int64_t now = QpcNow();
            UpdatePads( now );
            UpdateRumble( now );
        }

        for (OpenPad & pad : m_Pads)
        {
            m_Sdl.RumbleGamepad( pad.m_Handle, 0, 0, 0 );
            m_Sdl.CloseGamepad( pad.m_Handle );
        }

        m_Pads.clear();
        m_SdlReady.store( false );
        m_Sdl.Quit();
    }

    // Setup.

    bool Gamepad::Install(
        const ControllerSettings & settings,
        const std::filesystem::path & folder
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;
        m_Folder = folder;
        bool ok = true;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "SDL controller support disabled by config" );
            m_Installed = true;
            return ok;
        }

        if (!m_Sdl.Load( folder ))
        {
            m_Installed = true;
            return ok;
        }

        m_Device.Configure( this, settings.m_Rumble );

        m_InputInitHook = hook::CreateInline( "InputInit", game::input::INIT.m_Address, game::input::INIT.m_Signature,
            reinterpret_cast<void *>(&InputInitHook) );
        m_EnumCallbackHook = hook::CreateInline( "InputEnumCallback", game::input::ENUM_CALLBACK.m_Address, game::input::ENUM_CALLBACK.m_Signature,
            reinterpret_cast<void *>(&EnumCallbackHook) );
        m_InputShutdownHook = hook::CreateInline( "InputShutdown", game::input::SHUTDOWN.m_Address, game::input::SHUTDOWN.m_Signature,
            reinterpret_cast<void *>(&InputShutdownHook) );

        if (!m_InputInitHook || !m_EnumCallbackHook)
        {
            LOG_ERROR( "Controller list hooks failed, SDL controller support is off" );
            m_InputInitHook.reset();
            m_EnumCallbackHook.reset();
            m_InputShutdownHook.reset();
            m_Sdl.Unload();
            m_Installed = true;
            return false;
        }

        if (settings.m_TriggerBindings)
        {
            if (hook::MatchBytes( gi::FIND_ACCESSOR.m_Address, gi::FIND_ACCESSOR.m_Signature ) &&
                hook::MatchBytes( gi::CLEAR_BINDING.m_Address, gi::CLEAR_BINDING.m_Signature ))
            {
                m_DefaultBindingsHook = hook::CreateInline( "ControllerDefaultBindings", gi::DEFAULT_BINDINGS.m_Address,
                    gi::DEFAULT_BINDINGS.m_Signature, reinterpret_cast<void *>(&DefaultBindingsHook) );
            }
            else
            {
                LOG_ERROR( "Controller binding functions do not match, gamepad defaults stay on the game layout" );
            }

            ok &= m_DefaultBindingsHook != nullptr;
        }

        m_ReadyEvent.Reset( ::CreateEventW( nullptr, TRUE, FALSE, nullptr ) );
        m_Stop.store( false );
        m_Thread.Reset( ::CreateThread( nullptr, 0, &ThreadProc, this, 0, nullptr ) );

        if (!m_Thread)
        {
            LOG_ERROR( "SDL controller thread could not be created" );
            ::SetEvent( m_ReadyEvent.Get() );
            ok = false;
        }

        m_Installed = true;
        LOG_INFO( "SDL controller support installed, rumble {} gamepad bindings {} hide DirectInput copies {}, all hooks ok {}",
            settings.m_Rumble, settings.m_TriggerBindings, settings.m_HideHandledDevices, ok );
        return ok;
    }


    void Gamepad::Uninstall()
    {
        m_DefaultBindingsHook.reset();
        m_InputShutdownHook.reset();
        m_EnumCallbackHook.reset();
        m_InputInitHook.reset();

        if (m_Thread)
        {
            m_Stop.store( true );

            if (::WaitForSingleObject( m_Thread.Get(), 250 ) == WAIT_OBJECT_0)
            {
                m_Sdl.Unload();
            }

            m_Thread.Reset();
        }

        m_Installed = false;
    }
}