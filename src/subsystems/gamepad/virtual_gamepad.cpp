#include "virtual_gamepad.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#include <SDL3/SDL_gamepad.h>

#include "../../core/string_util.h"

namespace nfs5::input
{
    namespace
    {
        // The GUIDs are defined here so neither 'dxguid' nor 'dinput' has to be linked.
        constexpr GUID IID_UNKNOWN = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
        constexpr GUID IID_DEVICE_A = { 0x5944E680, 0xC92E, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
        constexpr GUID IID_DEVICE_2A = { 0x5944E682, 0xC92E, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
        constexpr GUID IID_EFFECT = { 0xE7E1F7C0, 0x88D2, 0x11D0, { 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35 } };

        constexpr GUID AXIS_GUIDS[] =
        {
            { 0xA36D02E0, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }, // X
            { 0xA36D02E1, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }, // Y
            { 0xA36D02E2, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }, // Z
            { 0xA36D02F4, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }, // Rx
            { 0xA36D02F5, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }, // Ry
            { 0xA36D02E3, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }, // Rz
            { 0xA36D02E4, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }, // Slider
        };

        constexpr GUID GUID_BUTTON_TYPE = { 0xA36D02F0, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
        constexpr GUID GUID_POV_TYPE = { 0xA36D02F2, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

        // Effect types, 13541C20 to 13541C2B.
        constexpr GUID EffectGuid(
            unsigned long index
        )
        {
            return { 0x13541C20ul + index, 0x8E33, 0x11D0, { 0x9A, 0xD0, 0x00, 0xA0, 0xC9, 0xA0, 0x6E, 0x35 } };
        }

        constexpr unsigned long EFFECT_CONSTANT = 0;
        constexpr unsigned long EFFECT_RAMP = 1;
        constexpr unsigned long EFFECT_PERIODIC_FIRST = 2;
        constexpr unsigned long EFFECT_PERIODIC_LAST = 6;
        constexpr unsigned long EFFECT_CONDITION_FIRST = 7;
        constexpr unsigned long EFFECT_CONDITION_LAST = 10;

        // Fixed identity, the game finds the controller at the same place on every start.
        constexpr GUID INSTANCE_GUID = { 0x4E465335, 0x5344, 0x4C33, { 0x9A, 0x47, 0x61, 0x6D, 0x65, 0x70, 0x61, 0x64 } };
        constexpr GUID PRODUCT_GUID = { 0x35534E46, 0x0000, 0x0000, { 0x00, 0x00, 0x50, 0x49, 0x44, 0x56, 0x49, 0x44 } };

        constexpr char PRODUCT_NAME[] = "NFS5 Enhanced Gamepad";
        constexpr char DISCONNECTED_NAME[] = "Gamepad (not connected)";

        // 'DIJOYSTATE' layout.
        constexpr DWORD OFFSET_POV = 32;
        constexpr DWORD OFFSET_BUTTONS = 48;

        // Periods at or above this many microseconds drive the heavy motor.
        constexpr DWORD SLOW_PERIOD_US = 60000;

        // Order of the DirectInput buttons. The game binds 0 to the handbrake, 2 to look back,
        // 3 to reset, 5 to the camera, 7 to shift up and 8 to shift down by default.
        constexpr int BUTTON_MAP[VirtualGamepad::BUTTON_COUNT] =
        {
            SDL_GAMEPAD_BUTTON_SOUTH,
            SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_START,
            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK,
            SDL_GAMEPAD_BUTTON_GUIDE,
            SDL_GAMEPAD_BUTTON_MISC1,
            SDL_GAMEPAD_BUTTON_TOUCHPAD,
            SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN,
            SDL_GAMEPAD_BUTTON_DPAD_LEFT,
            SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            -1, // Left trigger pressed past half.
            -2, // Right trigger pressed past half.
            SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
            SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
            SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
            SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
            SDL_GAMEPAD_BUTTON_MISC2
        };

        constexpr const char * BUTTON_NAMES[VirtualGamepad::BUTTON_COUNT] =
        {
            "A / Cross", "B / Circle", "X / Square", "Y / Triangle", "Left Stick", "Back / Share", "Start / Options",
            "Right Shoulder", "Left Shoulder", "Right Stick", "Guide", "Misc", "Touchpad", "D-Pad Up", "D-Pad Down",
            "D-Pad Left", "D-Pad Right", "Left Trigger", "Right Trigger", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4",
            "Misc 2"
        };

        constexpr const char * AXIS_NAMES[] =
        {
            "Left Stick X", "Triggers", "Right Stick X", "Left Trigger", "Right Trigger", "Right Stick Y", "Left Stick Y"
        };

        std::int64_t QpcNow()
        {
            LARGE_INTEGER value = {};
            ::QueryPerformanceCounter( &value );
            return value.QuadPart;
        }

        std::int64_t QpcFrequency()
        {
            static const std::int64_t frequency = []
            {
                LARGE_INTEGER value = {};
                ::QueryPerformanceFrequency( &value );
                return value.QuadPart > 0 ? value.QuadPart : 1;
            }();

            return frequency;
        }

        bool SameGuid(
            const GUID & a,
            const GUID & b
        )
        {
            return std::memcmp( &a, &b, sizeof( GUID ) ) == 0;
        }

        // Index of a standard effect GUID, '-1' for anything else.
        long EffectIndex(
            const GUID & guid
        )
        {
            for (unsigned long index = 0; index <= 11; ++index)
            {
                if (SameGuid( guid, EffectGuid( index ) ))
                {
                    return static_cast<long>(index);
                }
            }

            return -1;
        }

        // Copies a filled structure into a caller structure of possibly older size.
        template <typename T>
        void CopySized(
            const T & source,
            void * destination,
            DWORD size
        )
        {
            const DWORD bytes = std::min<DWORD>( size, sizeof( T ) );
            std::memcpy( destination, &source, bytes );
            *static_cast<DWORD *>(destination) = size;
        }

        // Field of a structure the caller sized, 'true' when the field lies inside its size.
        template <typename T>
        bool HasField(
            DWORD size,
            std::size_t offset
        )
        {
            return offset + sizeof( T ) <= size;
        }
    }

    // Effect.

    VirtualEffect::VirtualEffect(
        VirtualGamepad * device,
        REFGUID guid
    ) : m_Device( device ),
        m_Guid( guid )
    {
        const long index = EffectIndex( guid );

        if (index == static_cast<long>(EFFECT_CONSTANT))
        {
            m_Kind = Kind::CONSTANT;
        }
        else if (index == static_cast<long>(EFFECT_RAMP))
        {
            m_Kind = Kind::RAMP;
        }
        else if (index >= static_cast<long>(EFFECT_PERIODIC_FIRST) && index <= static_cast<long>(EFFECT_PERIODIC_LAST))
        {
            m_Kind = Kind::PERIODIC;
        }
        else if (index >= static_cast<long>(EFFECT_CONDITION_FIRST) && index <= static_cast<long>(EFFECT_CONDITION_LAST))
        {
            m_Kind = Kind::CONDITION;
        }
    }


    void VirtualEffect::ApplyParameters(
        LPCDIEFFECT effect,
        DWORD flags
    )
    {
        if (effect == nullptr || effect->dwSize < offsetof( DIEFFECT, dwSamplePeriod ))
            return;

        const DWORD size = effect->dwSize;

        if ((flags & DIEP_DURATION) != 0)
        {
            m_Duration = effect->dwDuration;
        }

        if ((flags & DIEP_GAIN) != 0 && HasField<DWORD>( size, offsetof( DIEFFECT, dwGain ) ))
        {
            m_Gain = std::min<DWORD>( effect->dwGain, 10000 );
        }

        if ((flags & DIEP_TYPESPECIFICPARAMS) != 0 && HasField<LPVOID>( size, offsetof( DIEFFECT, lpvTypeSpecificParams ) ) &&
            effect->lpvTypeSpecificParams != nullptr)
        {
            const DWORD bytes = effect->cbTypeSpecificParams;
            const void * params = effect->lpvTypeSpecificParams;

            switch (m_Kind)
            {
                case Kind::CONSTANT:
                    if (bytes >= sizeof( DICONSTANTFORCE ))
                    {
                        m_Magnitude = static_cast<float>(std::abs( static_cast<const DICONSTANTFORCE *>(params)->lMagnitude )) / 10000.0f;
                    }
                    break;

                case Kind::RAMP:
                    if (bytes >= sizeof( DIRAMPFORCE ))
                    {
                        const auto * ramp = static_cast<const DIRAMPFORCE *>(params);
                        m_Magnitude = static_cast<float>(std::max( std::abs( ramp->lStart ), std::abs( ramp->lEnd ) )) / 10000.0f;
                    }
                    break;

                case Kind::PERIODIC:
                    if (bytes >= sizeof( DIPERIODIC ))
                    {
                        const auto * periodic = static_cast<const DIPERIODIC *>(params);
                        m_Magnitude = static_cast<float>(periodic->dwMagnitude + static_cast<DWORD>(std::abs( periodic->lOffset ))) / 10000.0f;
                        m_Period = periodic->dwPeriod;
                    }
                    break;

                default:
                    // Springs and dampers center a wheel, a pad has nothing to center.
                    m_Magnitude = 0.0f;
                    break;
            }

            m_Magnitude = std::clamp( m_Magnitude, 0.0f, 1.0f );
        }
    }


    void VirtualEffect::Play(
        DWORD iterations
    )
    {
        const std::int64_t now = QpcNow();
        m_Playing = true;
        m_Start = now;

        if (m_Duration == INFINITE || iterations == INFINITE)
        {
            m_End = std::numeric_limits<std::int64_t>::max();
        }
        else
        {
            const std::int64_t micros = static_cast<std::int64_t>(m_Duration) * std::max<DWORD>( iterations, 1 );
            m_End = now + micros * QpcFrequency() / 1000000;
        }
    }


    bool VirtualEffect::Contribute(
        std::int64_t now,
        float & low,
        float & high
    ) const
    {
        if (!m_Playing || now >= m_End)
            return false;

        const float strength = m_Magnitude * static_cast<float>(m_Gain) / 10000.0f;

        switch (m_Kind)
        {
            case Kind::CONSTANT:
            case Kind::RAMP:
                // Knocks and kerbs, mostly the heavy motor.
                low += strength;
                high += strength * 0.5f;
                break;

            case Kind::PERIODIC:
                // Engine and road texture, slow waves on the heavy motor and fast ones on the light one.
                if (m_Period >= SLOW_PERIOD_US)
                {
                    low += strength;
                }
                else
                {
                    high += strength;
                }
                break;

            default:
                break;
        }

        return true;
    }


    HRESULT VirtualEffect::QueryInterface(
        REFIID riid,
        void ** object
    )
    {
        if (object == nullptr)
            return E_POINTER;

        if (SameGuid( riid, IID_UNKNOWN ) || SameGuid( riid, IID_EFFECT ))
        {
            *object = this;
            AddRef();
            return S_OK;
        }

        *object = nullptr;
        return E_NOINTERFACE;
    }


    ULONG VirtualEffect::AddRef()
    {
        return ++m_References;
    }


    ULONG VirtualEffect::Release()
    {
        const ULONG references = --m_References;

        if (references == 0)
        {
            m_Device->RemoveEffect( this );
            delete this;
        }

        return references;
    }


    HRESULT VirtualEffect::Initialize(
        HINSTANCE,
        DWORD,
        REFGUID
    )
    {
        return DI_OK;
    }


    HRESULT VirtualEffect::GetEffectGuid(
        LPGUID guid
    )
    {
        if (guid == nullptr)
            return E_POINTER;

        *guid = m_Guid;
        return DI_OK;
    }


    HRESULT VirtualEffect::GetParameters(
        LPDIEFFECT effect,
        DWORD flags
    )
    {
        if (effect == nullptr)
            return E_POINTER;

        std::scoped_lock lock( m_Device->m_EffectLock );

        if ((flags & DIEP_DURATION) != 0)
        {
            effect->dwDuration = m_Duration;
        }

        if ((flags & DIEP_GAIN) != 0 && HasField<DWORD>( effect->dwSize, offsetof( DIEFFECT, dwGain ) ))
        {
            effect->dwGain = m_Gain;
        }

        return DI_OK;
    }


    HRESULT VirtualEffect::SetParameters(
        LPCDIEFFECT effect,
        DWORD flags
    )
    {
        if (effect == nullptr)
            return E_POINTER;

        std::scoped_lock lock( m_Device->m_EffectLock );
        ApplyParameters( effect, flags );

        if ((flags & DIEP_START) != 0)
        {
            Play( 1 );
        }

        return DI_OK;
    }


    HRESULT VirtualEffect::Start(
        DWORD iterations,
        DWORD
    )
    {
        std::scoped_lock lock( m_Device->m_EffectLock );
        Play( iterations );
        return DI_OK;
    }


    HRESULT VirtualEffect::Stop()
    {
        std::scoped_lock lock( m_Device->m_EffectLock );
        m_Playing = false;
        return DI_OK;
    }


    HRESULT VirtualEffect::GetEffectStatus(
        LPDWORD status
    )
    {
        if (status == nullptr)
            return E_POINTER;

        std::scoped_lock lock( m_Device->m_EffectLock );
        *status = m_Playing && QpcNow() < m_End ? DIEGES_PLAYING : 0;
        return DI_OK;
    }


    HRESULT VirtualEffect::Download()
    {
        return DI_OK;
    }


    HRESULT VirtualEffect::Unload()
    {
        std::scoped_lock lock( m_Device->m_EffectLock );
        m_Playing = false;
        return DI_OK;
    }


    HRESULT VirtualEffect::Escape(
        LPDIEFFESCAPE
    )
    {
        return DIERR_UNSUPPORTED;
    }

    // Device.

    void VirtualGamepad::Configure(
        PadSource * source,
        bool force_feedback
    )
    {
        m_Source = source;
        m_ForceFeedback = force_feedback;
    }


    int VirtualGamepad::AxisFromOffset(
        DWORD offset
    )
    {
        if (offset % 4 != 0 || offset / 4 >= AXIS_COUNT)
            return -1;

        return static_cast<int>(offset / 4);
    }


    LONG VirtualGamepad::ToRange(
        int axis,
        float value
    ) const
    {
        const AxisRange & range = m_Ranges[static_cast<std::size_t>(axis)];
        const double unit = (std::clamp( static_cast<double>(value), -1.0, 1.0 ) + 1.0) * 0.5;
        const double span = static_cast<double>(range.m_Max) - static_cast<double>(range.m_Min);
        return static_cast<LONG>(std::lround( static_cast<double>(range.m_Min) + unit * span ));
    }


    void VirtualGamepad::FillObject(
        DIDEVICEOBJECTINSTANCEA & object,
        DWORD offset
    ) const
    {
        object = {};
        object.dwSize = sizeof( object );
        object.dwOfs = offset;

        const int axis = offset < OFFSET_POV ? AxisFromOffset( offset ) : -1;

        if (axis >= 0)
        {
            object.guidType = AXIS_GUIDS[axis];
            object.dwType = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE( axis );
            object.dwFlags = DIDOI_ASPECTPOSITION;
            CopyString( object.tszName, AXIS_NAMES[axis] );

            // The game plays its wheel effects on X and Y.
            if (m_ForceFeedback && (axis == AXIS_X || axis == AXIS_Y))
            {
                object.dwType |= DIDFT_FFACTUATOR;
                object.dwFlags |= DIDOI_FFACTUATOR;
                object.dwFFMaxForce = 10000;
                object.dwFFForceResolution = 1;
            }

            object.wUsagePage = 1;
            object.wUsage = static_cast<WORD>(0x30 + axis);
            return;
        }

        if (offset == OFFSET_POV)
        {
            object.guidType = GUID_POV_TYPE;
            object.dwType = DIDFT_POV | DIDFT_MAKEINSTANCE( 0 );
            CopyString( object.tszName, "D-Pad" );
            object.wUsagePage = 1;
            object.wUsage = 0x39;
            return;
        }

        const int button = static_cast<int>(offset) - static_cast<int>(OFFSET_BUTTONS);
        object.guidType = GUID_BUTTON_TYPE;
        object.dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE( button );
        CopyString( object.tszName, BUTTON_NAMES[button] );
        object.wUsagePage = 9;
        object.wUsage = static_cast<WORD>(button + 1);
    }


    void VirtualGamepad::RemoveEffect(
        VirtualEffect * effect
    )
    {
        std::scoped_lock lock( m_EffectLock );
        std::erase( m_Effects, effect );
    }


    void VirtualGamepad::ComputeRumble(
        std::int64_t now,
        float & low,
        float & high
    ) const
    {
        low = 0.0f;
        high = 0.0f;

        std::scoped_lock lock( m_EffectLock );

        if (!m_ActuatorsOn || m_Paused)
            return;

        for (const VirtualEffect * effect : m_Effects)
        {
            effect->Contribute( now, low, high );
        }

        const float gain = static_cast<float>(m_FeedbackGain) / 10000.0f;
        low = std::clamp( low * gain, 0.0f, 1.0f );
        high = std::clamp( high * gain, 0.0f, 1.0f );
    }


    void VirtualGamepad::StopAllEffects()
    {
        std::scoped_lock lock( m_EffectLock );

        for (VirtualEffect * effect : m_Effects)
        {
            effect->StopLocked();
        }
    }


    void VirtualGamepad::BuildState(
        const PadSnapshot & pad,
        DWORD size,
        void * out
    )
    {
        const std::uint32_t buttons = pad.m_Buttons;

        DIJOYSTATE2 state = {};
        state.lX = ToRange( AXIS_X, pad.m_LeftX );

        // Both triggers on one axis like the Xbox driver does it, right is up. The game binds
        // accelerate to up and brake to down of Y by default.
        state.lY = ToRange( AXIS_Y, pad.m_LeftTrigger - pad.m_RightTrigger );
        state.lZ = ToRange( AXIS_Z, pad.m_RightX );

        // Each trigger on its own as a half axis that rests in the middle, the binding screen
        // takes the half from the center to the end.
        state.lRx = ToRange( AXIS_RX, pad.m_LeftTrigger );
        state.lRy = ToRange( AXIS_RY, pad.m_RightTrigger );
        state.lRz = ToRange( AXIS_RZ, pad.m_RightY );
        state.rglSlider[0] = ToRange( AXIS_SLIDER, pad.m_LeftY );
        state.rglSlider[1] = ToRange( AXIS_SLIDER, 0.0f );

        const auto held = [&] ( int button )
        {
            return (buttons & (1u << button)) != 0;
        };

        const bool up = held( SDL_GAMEPAD_BUTTON_DPAD_UP );
        const bool down = held( SDL_GAMEPAD_BUTTON_DPAD_DOWN );
        const bool left = held( SDL_GAMEPAD_BUTTON_DPAD_LEFT );
        const bool right = held( SDL_GAMEPAD_BUTTON_DPAD_RIGHT );
        const int vertical = (down ? 1 : 0) - (up ? 1 : 0);
        const int horizontal = (right ? 1 : 0) - (left ? 1 : 0);

        static constexpr DWORD POV_ANGLES[3][3] =
        {
            { 31500, 0, 4500 },
            { 27000, INFINITE, 9000 },
            { 22500, 18000, 13500 }
        };

        state.rgdwPOV[0] = POV_ANGLES[vertical + 1][horizontal + 1];
        state.rgdwPOV[1] = INFINITE;
        state.rgdwPOV[2] = INFINITE;
        state.rgdwPOV[3] = INFINITE;

        for (int index = 0; index < BUTTON_COUNT; ++index)
        {
            const int source = BUTTON_MAP[index];
            bool pressed = false;

            if (source == -1)
            {
                pressed = pad.m_LeftTrigger > 0.5f;
            }
            else if (source == -2)
            {
                pressed = pad.m_RightTrigger > 0.5f;
            }
            else if (source >= 0 && source < 32)
            {
                pressed = held( source );
            }

            state.rgbButtons[index] = pressed ? 0x80 : 0x00;
        }

        std::memcpy( out, &state, std::min<std::size_t>( size, sizeof( state ) ) );
    }


    HRESULT VirtualGamepad::QueryInterface(
        REFIID riid,
        void ** object
    )
    {
        if (object == nullptr)
            return E_POINTER;

        if (SameGuid( riid, IID_UNKNOWN ) || SameGuid( riid, IID_DEVICE_A ) || SameGuid( riid, IID_DEVICE_2A ))
        {
            *object = this;
            return S_OK;
        }

        *object = nullptr;
        return E_NOINTERFACE;
    }


    // The device lives as long as the process, the game releases it on shutdown.
    ULONG VirtualGamepad::AddRef()
    {
        return 1;
    }


    ULONG VirtualGamepad::Release()
    {
        return 1;
    }


    HRESULT VirtualGamepad::GetCapabilities(
        LPDIDEVCAPS caps
    )
    {
        if (caps == nullptr || caps->dwSize < offsetof( DIDEVCAPS, dwFFSamplePeriod ))
            return DIERR_INVALIDPARAM;

        DIDEVCAPS result = {};
        result.dwSize = sizeof( result );
        result.dwFlags = DIDC_ATTACHED | (m_ForceFeedback ? DIDC_FORCEFEEDBACK : 0);
        result.dwDevType = DEVICE_TYPE;
        result.dwAxes = AXIS_COUNT;
        result.dwButtons = BUTTON_COUNT;
        result.dwPOVs = 1;
        result.dwFFSamplePeriod = 10000;
        result.dwFFMinTimeResolution = 1000;
        result.dwFirmwareRevision = 1;
        result.dwHardwareRevision = 1;
        result.dwFFDriverVersion = 1;
        CopySized( result, caps, caps->dwSize );
        return DI_OK;
    }


    HRESULT VirtualGamepad::EnumObjects(
        LPDIENUMDEVICEOBJECTSCALLBACKA callback,
        LPVOID reference,
        DWORD flags
    )
    {
        if (callback == nullptr)
            return DIERR_INVALIDPARAM;

        const DWORD type_filter = flags & 0x00FFFFFF;
        const bool want_all = type_filter == DIDFT_ALL;
        const bool want_ff = (flags & DIDFT_FFACTUATOR) != 0;

        std::vector<DWORD> offsets;

        if (want_all || (type_filter & DIDFT_AXIS) != 0 || want_ff)
        {
            for (DWORD axis = 0; axis < AXIS_COUNT; ++axis)
            {
                offsets.push_back( axis * 4 );
            }
        }

        if (!want_ff && (want_all || (type_filter & DIDFT_POV) != 0))
        {
            offsets.push_back( OFFSET_POV );
        }

        if (!want_ff && (want_all || (type_filter & DIDFT_BUTTON) != 0))
        {
            for (DWORD button = 0; button < BUTTON_COUNT; ++button)
            {
                offsets.push_back( OFFSET_BUTTONS + button );
            }
        }

        for (const DWORD offset : offsets)
        {
            DIDEVICEOBJECTINSTANCEA object = {};
            FillObject( object, offset );

            if (want_ff && (object.dwType & DIDFT_FFACTUATOR) == 0)
                continue;

            if (callback( &object, reference ) == DIENUM_STOP)
                break;
        }

        return DI_OK;
    }


    HRESULT VirtualGamepad::GetProperty(
        REFGUID property,
        LPDIPROPHEADER header
    )
    {
        if (header == nullptr)
            return DIERR_INVALIDPARAM;

        const auto id = reinterpret_cast<std::uintptr_t>(&property);

        switch (id)
        {
            case 4:
            {
                if (header->dwSize < sizeof( DIPROPRANGE ))
                    return DIERR_INVALIDPARAM;

                const int axis = header->dwHow == DIPH_BYOFFSET ? AxisFromOffset( header->dwObj ) : AXIS_X;

                if (axis < 0)
                    return DIERR_OBJECTNOTFOUND;

                auto * range = reinterpret_cast<DIPROPRANGE *>( header );
                range->lMin = m_Ranges[static_cast<std::size_t>( axis )].m_Min;
                range->lMax = m_Ranges[static_cast<std::size_t>( axis )].m_Max;
                return DI_OK;
            }

            case 1:
            case 2:
            case 3:
            case 5:
            case 6:
            case 7:
            case 9:
            {
                if (header->dwSize < sizeof( DIPROPDWORD ))
                    return DIERR_INVALIDPARAM;

                auto * value = reinterpret_cast<DIPROPDWORD *>( header );

                switch (id)
                {
                    case 3:
                        value->dwData = 1;
                        break;

                    case 6:
                        value->dwData = 10000;
                        break;

                    case 7:
                    {
                        std::scoped_lock lock( m_EffectLock );
                        value->dwData = m_FeedbackGain;
                        break;
                    }

                    default:
                        value->dwData = 0;
                        break;
                }

                return DI_OK;
            }

            default:
                return DIERR_UNSUPPORTED;
        }
    }


    HRESULT VirtualGamepad::SetProperty(
        REFGUID property,
        LPCDIPROPHEADER header
    )
    {
        if (header == nullptr)
            return DIERR_INVALIDPARAM;

        const auto id = reinterpret_cast<std::uintptr_t>(&property);

        if (id == 4)
        {
            if (header->dwSize < sizeof( DIPROPRANGE ))
                return DIERR_INVALIDPARAM;

            const auto * range = reinterpret_cast<const DIPROPRANGE *>( header );

            if (range->lMin >= range->lMax)
                return DIERR_INVALIDPARAM;

            if (header->dwHow == DIPH_DEVICE)
            {
                for (AxisRange & axis : m_Ranges)
                {
                    axis.m_Min = range->lMin;
                    axis.m_Max = range->lMax;
                }

                return DI_OK;
            }

            const int axis = header->dwHow == DIPH_BYOFFSET ? AxisFromOffset( header->dwObj ) : -1;

            if (axis < 0)
                return DIERR_OBJECTNOTFOUND;

            m_Ranges[static_cast<std::size_t>( axis )] = { range->lMin, range->lMax };
            return DI_OK;
        }

        if (id == 7 && header->dwSize >= sizeof( DIPROPDWORD ))
        {
            std::scoped_lock lock( m_EffectLock );
            m_FeedbackGain = std::min<DWORD>( reinterpret_cast<const DIPROPDWORD *>( header )->dwData, 10000 );
            return DI_OK;
        }

        // Buffer size, axis mode, dead zone, saturation and auto center change nothing here.
        return DI_OK;
    }


    HRESULT VirtualGamepad::Acquire()
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::Unacquire()
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::GetDeviceState(
        DWORD size,
        LPVOID data
    )
    {
        if (data == nullptr || (size != sizeof( DIJOYSTATE ) && size != sizeof( DIJOYSTATE2 )))
            return DIERR_INVALIDPARAM;

        PadSnapshot pad;

        if (m_Source != nullptr)
        {
            m_Source->ReadPad( pad );
        }

        BuildState( pad, size, data );
        return DI_OK;
    }


    HRESULT VirtualGamepad::GetDeviceData(
        DWORD,
        LPDIDEVICEOBJECTDATA,
        LPDWORD count,
        DWORD
    )
    {
        if (count != nullptr)
        {
            *count = 0;
        }

        return DI_OK;
    }


    HRESULT VirtualGamepad::SetDataFormat(
        LPCDIDATAFORMAT
    )
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::SetEventNotification(
        HANDLE
    )
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::SetCooperativeLevel(
        HWND,
        DWORD
    )
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::GetObjectInfo(
        LPDIDEVICEOBJECTINSTANCEA object,
        DWORD id,
        DWORD how
    )
    {
        if (object == nullptr || object->dwSize < offsetof( DIDEVICEOBJECTINSTANCEA, dwFFMaxForce ))
            return DIERR_INVALIDPARAM;

        DWORD offset = 0;

        if (how == DIPH_BYOFFSET)
        {
            offset = id;
        }
        else if (how == DIPH_BYID)
        {
            const DWORD instance = DIDFT_GETINSTANCE( id );

            if ((id & DIDFT_AXIS) != 0)
            {
                offset = instance * 4;
            }
            else if ((id & DIDFT_POV) != 0)
            {
                offset = instance == 0 ? OFFSET_POV : 0xFFFFFFFF;
            }
            else if ((id & DIDFT_BUTTON) != 0)
            {
                offset = OFFSET_BUTTONS + instance;
            }
            else
            {
                return DIERR_OBJECTNOTFOUND;
            }
        }
        else
        {
            return DIERR_INVALIDPARAM;
        }

        const bool axis = offset < OFFSET_POV && AxisFromOffset( offset ) >= 0;
        const bool pov = offset == OFFSET_POV;
        const bool button = offset >= OFFSET_BUTTONS && offset < OFFSET_BUTTONS + BUTTON_COUNT;

        if (!axis && !pov && !button)
            return DIERR_OBJECTNOTFOUND;

        DIDEVICEOBJECTINSTANCEA result = {};
        FillObject( result, offset );
        CopySized( result, object, object->dwSize );
        return DI_OK;
    }


    HRESULT VirtualGamepad::GetDeviceInfo(
        LPDIDEVICEINSTANCEA instance
    )
    {
        if (instance == nullptr || instance->dwSize < offsetof( DIDEVICEINSTANCEA, guidFFDriver ))
            return DIERR_INVALIDPARAM;

        PadSnapshot pad;

        if (m_Source != nullptr)
        {
            m_Source->ReadPad( pad );
        }

        DIDEVICEINSTANCEA result = {};
        result.dwSize = sizeof( result );
        result.guidInstance = INSTANCE_GUID;
        result.guidProduct = PRODUCT_GUID;
        result.dwDevType = DEVICE_TYPE;
        CopyString( result.tszInstanceName, pad.m_Connected && pad.m_Name[0] != '\0' ? pad.m_Name : DISCONNECTED_NAME );
        CopyString( result.tszProductName, PRODUCT_NAME );
        result.wUsagePage = 1;
        result.wUsage = 5;
        CopySized( result, instance, instance->dwSize );
        return DI_OK;
    }


    HRESULT VirtualGamepad::RunControlPanel(
        HWND,
        DWORD
    )
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::Initialize(
        HINSTANCE,
        DWORD,
        REFGUID
    )
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::CreateEffect(
        REFGUID guid,
        LPCDIEFFECT effect,
        LPDIRECTINPUTEFFECT * out,
        LPUNKNOWN outer
    )
    {
        if (out == nullptr)
            return E_POINTER;

        *out = nullptr;

        if (outer != nullptr)
            return CLASS_E_NOAGGREGATION;

        if (!m_ForceFeedback)
            return DIERR_UNSUPPORTED;

        if (EffectIndex( guid ) < 0)
            return DIERR_DEVICENOTREG;

        auto * created = new VirtualEffect( this, guid );

        {
            std::scoped_lock lock( m_EffectLock );
            m_Effects.push_back( created );
        }

        if (effect != nullptr)
        {
            created->SetParameters( effect, DIEP_ALLPARAMS & ~DIEP_START );
        }

        *out = created;
        return DI_OK;
    }


    HRESULT VirtualGamepad::EnumEffects(
        LPDIENUMEFFECTSCALLBACKA callback,
        LPVOID reference,
        DWORD type
    )
    {
        if (callback == nullptr)
            return DIERR_INVALIDPARAM;

        if (!m_ForceFeedback)
            return DI_OK;

        for (unsigned long index = 0; index <= EFFECT_CONDITION_LAST; ++index)
        {
            DIEFFECTINFOA info = {};
            GetEffectInfo( &info, EffectGuid( index ) );

            if (type != DIEFT_ALL && DIEFT_GETTYPE( info.dwEffType ) != DIEFT_GETTYPE( type ))
                continue;

            if (callback( &info, reference ) == DIENUM_STOP)
                break;
        }

        return DI_OK;
    }


    HRESULT VirtualGamepad::GetEffectInfo(
        LPDIEFFECTINFOA info,
        REFGUID guid
    )
    {
        if (info == nullptr)
            return DIERR_INVALIDPARAM;

        const long index = EffectIndex( guid );

        if (index < 0 || index > static_cast<long>(EFFECT_CONDITION_LAST))
            return DIERR_DEVICENOTREG;

        DIEFFECTINFOA result = {};
        result.dwSize = sizeof( result );
        result.guid = guid;

        if (index == static_cast<long>(EFFECT_CONSTANT))
        {
            result.dwEffType = DIEFT_CONSTANTFORCE;
            CopyString( result.tszName, "Constant Force" );
        }
        else if (index == static_cast<long>(EFFECT_RAMP))
        {
            result.dwEffType = DIEFT_RAMPFORCE;
            CopyString( result.tszName, "Ramp Force" );
        }
        else if (index <= static_cast<long>(EFFECT_PERIODIC_LAST))
        {
            result.dwEffType = DIEFT_PERIODIC;
            CopyString( result.tszName, "Periodic" );
        }
        else
        {
            result.dwEffType = DIEFT_CONDITION;
            CopyString( result.tszName, "Condition" );
        }

        result.dwStaticParams = DIEP_DURATION | DIEP_GAIN | DIEP_AXES | DIEP_DIRECTION | DIEP_TYPESPECIFICPARAMS;
        result.dwDynamicParams = result.dwStaticParams;
        CopySized( result, info, info->dwSize != 0 ? info->dwSize : sizeof( result ) );
        return DI_OK;
    }


    HRESULT VirtualGamepad::GetForceFeedbackState(
        LPDWORD state
    )
    {
        if (state == nullptr)
            return E_POINTER;

        std::scoped_lock lock( m_EffectLock );
        *state = DIGFFS_POWERON | (m_ActuatorsOn ? DIGFFS_ACTUATORSON : DIGFFS_ACTUATORSOFF) | (m_Paused ? DIGFFS_PAUSED : 0) |
            (m_Effects.empty() ? DIGFFS_EMPTY : 0);
        return DI_OK;
    }


    HRESULT VirtualGamepad::SendForceFeedbackCommand(
        DWORD flags
    )
    {
        if (!m_ForceFeedback)
            return DIERR_UNSUPPORTED;

        std::scoped_lock lock( m_EffectLock );

        if ((flags & (DISFFC_RESET | DISFFC_STOPALL)) != 0)
        {
            for (VirtualEffect * effect : m_Effects)
            {
                effect->StopLocked();
            }
        }

        if ((flags & DISFFC_RESET) != 0)
        {
            m_Paused = false;
            m_ActuatorsOn = true;
        }

        if ((flags & DISFFC_PAUSE) != 0)
        {
            m_Paused = true;
        }

        if ((flags & DISFFC_CONTINUE) != 0)
        {
            m_Paused = false;
        }

        if ((flags & DISFFC_SETACTUATORSON) != 0)
        {
            m_ActuatorsOn = true;
        }

        if ((flags & DISFFC_SETACTUATORSOFF) != 0)
        {
            m_ActuatorsOn = false;
        }

        return DI_OK;
    }


    HRESULT VirtualGamepad::EnumCreatedEffectObjects(
        LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback,
        LPVOID reference,
        DWORD
    )
    {
        if (callback == nullptr)
            return DIERR_INVALIDPARAM;

        std::vector<VirtualEffect *> effects;

        {
            std::scoped_lock lock( m_EffectLock );
            effects = m_Effects;
        }

        for (VirtualEffect * effect : effects)
        {
            if (callback( effect, reference ) == DIENUM_STOP)
                break;
        }

        return DI_OK;
    }


    HRESULT VirtualGamepad::Escape(
        LPDIEFFESCAPE
    )
    {
        return DIERR_UNSUPPORTED;
    }


    HRESULT VirtualGamepad::Poll()
    {
        return DI_OK;
    }


    HRESULT VirtualGamepad::SendDeviceData(
        DWORD,
        LPCDIDEVICEOBJECTDATA,
        LPDWORD count,
        DWORD
    )
    {
        if (count != nullptr)
        {
            *count = 0;
        }

        return DI_OK;
    }
}