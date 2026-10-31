#include "qpc_timer.h"

#include <mmsystem.h>

#include <algorithm>

#include "../../core/hook_util.h"
#include "../../core/log.h"
#include "../../game/addresses.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace nfs5
{
    namespace
    {
        using TimerCallback = void( __cdecl * )();
        using EaThreadCreateFn = HANDLE( __cdecl * )(TimerCallback, SIZE_T, int, int, void *);
        using GameAtExitFn = int( __cdecl * )(TimerCallback);

        constexpr game::GameAddr GAME_ATEXIT = 0x00557380;
        constexpr std::string_view GAME_ATEXIT_SIG = "8B 44 24 04 50 E8 76 B0 04 00";

        // EA priority 2 maps to 'THREAD_PRIORITY_HIGHEST' in 'sub_55F8B0'.
        constexpr int EA_PRIORITY_HIGHEST = 2;

        // PIT clock units per millisecond as used by internal 'lowtimer.c'.
        constexpr std::int64_t PIT_UNITS_PER_SECOND = 1193000;

        // Computes 'value * numerator / denominator' without 64 bit overflow
        // for the magnitudes used here.
        std::int64_t MulDiv64(
            std::int64_t value,
            std::int64_t numerator,
            std::int64_t denominator
        )
        {
            const std::int64_t quotient = value / denominator;
            const std::int64_t remainder = value % denominator;
            return quotient * numerator + remainder * numerator / denominator;
        }


        // Windows 11 may ignore timer resolution requests and throttle the process when
        // its window is not in focus, the game logic depends on accurate waits.
        void OptOutOfPowerThrottling()
        {
            struct PowerThrottlingState
            {
                ULONG m_Version;
                ULONG m_ControlMask;
                ULONG m_StateMask;
            };

            using SetProcessInformationFn = BOOL( WINAPI * )(HANDLE, int, LPVOID, DWORD);

            constexpr int PROCESS_POWER_THROTTLING_CLASS = 4;
            constexpr ULONG THROTTLE_EXECUTION_SPEED = 0x1;
            constexpr ULONG THROTTLE_IGNORE_TIMER_RESOLUTION = 0x4;

            HMODULE kernel = ::GetModuleHandleW( L"kernel32.dll" );
            auto set_info = reinterpret_cast<SetProcessInformationFn>(::GetProcAddress( kernel, "SetProcessInformation" ));

            if (set_info == nullptr)
                return;

            PowerThrottlingState state = { 1, THROTTLE_EXECUTION_SPEED | THROTTLE_IGNORE_TIMER_RESOLUTION, 0 };

            if (set_info( ::GetCurrentProcess(), PROCESS_POWER_THROTTLING_CLASS, &state, sizeof( state ) ) == FALSE)
            {
                // Older builds reject the timer resolution flag, retry with execution speed only.
                state.m_ControlMask = THROTTLE_EXECUTION_SPEED;
                set_info( ::GetCurrentProcess(), PROCESS_POWER_THROTTLING_CLASS, &state, sizeof( state ) );
            }

            LOG_INFO( "Process power throttling opt out requested" );
        }
    }


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
            return value.QuadPart;
        }();

        return frequency;
    }


    QpcTimer & QpcTimer::GetInstance()
    {
        static QpcTimer instance;
        return instance;
    }


    void QpcTimer::WaitUntil(
        std::int64_t due_qpc
    )
    {
        for (;; )
        {
            const std::int64_t remaining = due_qpc - QpcNow();
            if (remaining <= 0 || !m_Running.load( std::memory_order_acquire ))
            {
                return;
            }

            // 100 ns units for the waitable timer.
            const std::int64_t remaining_100ns = MulDiv64( remaining, 10000000, m_QpcFrequency );
            if (remaining_100ns < 2000)
            {
                // Under 0.2 ms, hand the core to the game thread instead of spinning on it.
                ::SwitchToThread();
                continue;
            }

            if (m_HighResolutionTimer)
            {
                LARGE_INTEGER due = {};
                due.QuadPart = -remaining_100ns;

                if (::SetWaitableTimer( m_WaitTimer.Get(), &due, 0, nullptr, nullptr, FALSE ) != FALSE)
                {
                    const HANDLE handles[2] = { m_WaitTimer.Get(), m_StopEvent.Get() };
                    ::WaitForMultipleObjects( 2, handles, FALSE, INFINITE );
                    continue;
                }
            }

            // Fallback relies on the 1 ms system timer resolution requested in 'InitHook'.
            const auto wait_ms = static_cast<DWORD>(std::max< std::int64_t >( 1, remaining_100ns / 10000 - 1 ));
            ::WaitForSingleObject( m_StopEvent.Get(), wait_ms );
        }
    }


    void QpcTimer::Run()
    {
        m_ThreadId.store( ::GetCurrentThreadId(), std::memory_order_release );

        const std::int64_t frequency = m_QpcFrequency;
        const auto hz = static_cast<std::int64_t>(m_TickFrequency);
        const auto max_catch_up = static_cast<std::int64_t>(m_Settings.m_MaxCatchUpTicks);

        auto & ticks = hook::Ref<volatile std::int32_t>( game::var::TIMER_TICKS );
        auto & ticks_secondary = hook::Ref<volatile std::int32_t>( game::var::TIMER_TICKS_SECONDARY );
        auto & pit_ticks = hook::Ref<volatile std::int32_t>( game::var::TIMER_PIT_TICKS );
        auto * callbacks = hook::Ptr<TimerCallback>( game::var::TIMER_CALLBACKS );
        auto & frame_event = hook::Ref<HANDLE>( game::var::TIMER_FRAME_EVENT );

        std::int64_t schedule_start = QpcNow();
        const std::int64_t pit_start = schedule_start;
        std::int64_t pit_reported = 0;
        std::int64_t processed = 0;
        std::int64_t resync_count = 0;
        bool first_tick = true;

        while (m_Running.load( std::memory_order_acquire ))
        {
            WaitUntil( schedule_start + MulDiv64( processed + 1, frequency, hz ) );

            if (!m_Running.load( std::memory_order_acquire ))
                break;

            const std::int64_t now = QpcNow();
            const std::int64_t due_ticks = MulDiv64( now - schedule_start, hz, frequency );
            std::int64_t pending = due_ticks - processed;

            if (pending <= 0)
                continue;

            if (pending > max_catch_up)
            {
                // Too far behind, usually a debugger break or a long hitch. Run the
                // budget and move the schedule so game time does not race ahead,
                if ((resync_count++ % 32) == 0)
                {
                    LOG_WARN( "Timer fell behind by {} ticks, resynchronizing", pending );
                }

                pending = max_catch_up;
                schedule_start = now - MulDiv64( processed + pending, frequency, hz );
            }

            for (std::int64_t i = 0; i < pending; ++i)
            {
                ticks = ticks + 1;
                ticks_secondary = ticks_secondary + 1;

                const std::int64_t pit_total = MulDiv64( now - pit_start, PIT_UNITS_PER_SECOND, frequency ) >> 16;

                if (pit_total != pit_reported)
                {
                    pit_ticks = pit_ticks + static_cast<std::int32_t>( pit_total - pit_reported );
                    pit_reported = pit_total;
                }

                for (std::size_t slot = 0; slot < game::var::TIMER_CALLBACK_COUNT; ++slot)
                {
                    const TimerCallback callback = callbacks[slot];

                    if (callback != nullptr)
                    {
                        callback();
                    }
                }

                // The multimedia callback pulsed this event every tick, waiters sync frames on it.
                HANDLE event = frame_event;
                if (event != nullptr)
                {
                    ::SetEvent( event );
                }
            }

            processed += pending;

            if (first_tick)
            {
                first_tick = false;
                ::SetEvent( m_FirstTickEvent.Get() );
            }
        }

        m_ThreadId.store( 0, std::memory_order_release );
        ::SetEvent( m_ExitedEvent.Get() );
    }


    void __cdecl QpcTimer::ThreadProc()
    {
        GetInstance().Run();
    }


    void QpcTimer::Stop()
    {
        if (!m_Running.exchange( false, std::memory_order_acq_rel ))
            return;

        ::SetEvent( m_StopEvent.Get() );

        if (m_ThreadId.load( std::memory_order_acquire ) != ::GetCurrentThreadId())
        {
            if (::WaitForSingleObject( m_ExitedEvent.Get(), 2000 ) != WAIT_OBJECT_0)
            {
                LOG_WARN( "Timer thread did not exit in time" );
            }
        }

        hook::Ref<std::int32_t>( game::var::TIMER_RUNNING ) = 0;
        hook::Ref<std::int32_t>( game::var::TIMER_FREQUENCY ) = 0;
        hook::Ref<std::int32_t>( game::var::TIMER_FRACTION ) = 0;
        hook::Ref<std::int32_t>( game::var::TIMER_SUPPRESS_SIGNAL ) = 0;
        hook::Ref<std::int32_t>( game::var::TIMER_RESOLUTION ) = 0;
        hook::Ref<std::int32_t>( game::var::TIMER_ID ) = 0;

        if (m_PeriodRequested)
        {
            ::timeEndPeriod( 1 );
            m_PeriodRequested = false;
        }

        LOG_INFO( "QPC timer stopped" );
    }


    void __cdecl QpcTimer::ShutdownHook()
    {
        GetInstance().Stop();
    }


    void __cdecl QpcTimer::InitHook(
        int frequency
    )
    {
        QpcTimer & self = GetInstance();
        self.Stop();

        if (frequency < 1 || frequency > 10000)
        {
            LOG_WARN( "TIMER_init bad frequency {}, using 100", frequency );
            frequency = 100;
        }

        // Mirror the globals the original 'TIMER_init' publishes.
        hook::Ref<std::int32_t>( game::var::TIMER_FRACTION ) = 0;
        hook::Ref<std::int32_t>( game::var::TIMER_PERIOD_FIXED ) = 65536000 / frequency;
        hook::Ref<std::int32_t>( game::var::TIMER_TICKS ) = 0;
        hook::Ref<std::int32_t>( game::var::TIMER_PIT_TICKS ) = 0;

        // Game code sleeps with 'SleepEx(1)' and 'SleepEx(10)', keep the 1 ms resolution the original requested.
        self.m_PeriodRequested = ::timeBeginPeriod( 1 ) == TIMERR_NOERROR;
        hook::Ref<std::int32_t>( game::var::TIMER_RESOLUTION ) = 1;

        self.m_TickFrequency = frequency;
        self.m_QpcFrequency = QpcFrequency();

        ::ResetEvent( self.m_StopEvent.Get() );
        ::ResetEvent( self.m_ExitedEvent.Get() );
        ::ResetEvent( self.m_FirstTickEvent.Get() );

        self.m_Running.store( true, std::memory_order_release );

        hook::Ref<std::int32_t>( game::var::TIMER_FREQUENCY ) = frequency;
        hook::Ref<std::int32_t>( game::var::TIMER_RUNNING ) = frequency;

        // Created through the EA thread layer so the thread record matches the original timer thread.
        const auto create_thread = reinterpret_cast<EaThreadCreateFn>(game::fn::EA_THREAD_CREATE.Get());
        HANDLE thread = create_thread( &ThreadProc, 0, EA_PRIORITY_HIGHEST, -1, hook::Ptr<void>( game::var::TIMER_THREAD_RECORD ) );

        if (thread == nullptr)
        {
            LOG_ERROR( "Timer thread creation failed, falling back to the original multimedia timer" );

            self.m_Running.store( false, std::memory_order_release );

            if (self.m_PeriodRequested)
            {
                ::timeEndPeriod( 1 );
                self.m_PeriodRequested = false;
            }

            self.m_InitHook->ccall< void >( frequency );
            return;
        }

        // Same shutdown registration as the original so exit order is unchanged.
        if (hook::MatchBytes( GAME_ATEXIT, GAME_ATEXIT_SIG ))
        {
            reinterpret_cast<GameAtExitFn>(GAME_ATEXIT.Get())(reinterpret_cast<TimerCallback>(game::fn::TIMER_SHUTDOWN.m_Address.Get()));
        }

        if (::WaitForSingleObject( self.m_FirstTickEvent.Get(), 1000 ) != WAIT_OBJECT_0)
        {
            LOG_ERROR( "Timer thread produced no tick within one second" );
        }

        LOG_INFO( "QPC timer running at {} Hz, high resolution waitable timer {}", frequency, self.m_HighResolutionTimer );
    }


    bool QpcTimer::Install(
        const TimingSettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;
        OptOutOfPowerThrottling();

        if (!settings.m_QpcTimer)
        {
            LOG_INFO( "QPC timer disabled by config, original multimedia timer stays active" );
            return true;
        }

        if (!hook::MatchBytes( game::fn::EA_THREAD_CREATE, game::fn::EA_THREAD_CREATE_SIG ))
        {
            LOG_ERROR( "EA thread create signature mismatch, QPC timer not installed" );
            return false;
        }

        m_StopEvent.Reset( ::CreateEventW( nullptr, TRUE, FALSE, nullptr ) );
        m_ExitedEvent.Reset( ::CreateEventW( nullptr, TRUE, FALSE, nullptr ) );
        m_FirstTickEvent.Reset( ::CreateEventW( nullptr, TRUE, FALSE, nullptr ) );

        if (!m_StopEvent || !m_ExitedEvent || !m_FirstTickEvent)
        {
            LOG_ERROR( "Timer events could not be created" );
            return false;
        }

        if (settings.m_HighResolutionWaitableTimer)
        {
            m_WaitTimer.Reset( ::CreateWaitableTimerExW( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS ) );
            m_HighResolutionTimer = m_WaitTimer.IsValid();

            if (!m_HighResolutionTimer)
            {
                LOG_WARN( "High resolution waitable timer unavailable, error {}", ::GetLastError() );
            }
        }

        m_ShutdownHook = hook::CreateInline( "TIMER_restore", game::fn::TIMER_SHUTDOWN.m_Address, game::fn::TIMER_SHUTDOWN.m_Signature,
            reinterpret_cast<void *>(&ShutdownHook) );
        m_InitHook = hook::CreateInline( "TIMER_init", game::fn::TIMER_INIT.m_Address, game::fn::TIMER_INIT.m_Signature,
            reinterpret_cast<void *>(&InitHook) );

        if (!m_InitHook || !m_ShutdownHook)
        {
            m_InitHook.reset();
            m_ShutdownHook.reset();
            return false;
        }

        m_Installed = true;
        return true;
    }

    void QpcTimer::Uninstall()
    {
        Stop();

        m_InitHook.reset();
        m_ShutdownHook.reset();
        m_Installed = false;
    }
}