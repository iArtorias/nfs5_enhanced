#include "interpolation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "../../core/hook_util.h"
#include "../../core/log.h"

#include "../../game/addresses.h"

#include "../patch_silent_compat/qpc_timer.h"

namespace nfs5
{
    namespace
    {
        // Simulation steps per second assumed until the real rate has been measured.
        constexpr double DEFAULT_STEPS_PER_SECOND = 64.0;

        // Steps measured before the rate is locked.
        constexpr std::int32_t PERIOD_MEASURE_STEPS = 256;

        // A gap longer than this many step periods restarts the clock, pause or loading.
        constexpr double CLOCK_GAP_PERIODS = 8.0;

        // Upward drift allowed for the lateness floor per step, in periods.
        constexpr double LATENESS_RELAX_PERIODS = 1.0 / 4096.0;

        // Frames without a renderer write before a value is blended again.
        constexpr std::int32_t RENDERER_OWNED_FRAMES = 30;

        // Steps a smaller render delay must suffice before it is used.
        constexpr std::int32_t DELAY_SHRINK_STEPS = 256;

        // Tracks not seen for this many steps are dropped.
        constexpr std::int32_t TRACK_EXPIRY_STEPS = 64;

        // Rotation change above this angle is a cut, cos 60 degrees.
        constexpr float SNAP_ROTATION_DOT = 0.5f;

        using Vec3 = std::array<float, 3>;

        bool IsFinite(
            const game::Transform & transform
        )
        {
            for (const float value : transform.m_Position)
            {
                if (!std::isfinite( value ))
                {
                    return false;
                }
            }

            for (const float value : transform.m_Rotation)
            {
                if (!std::isfinite( value ))
                {
                    return false;
                }
            }

            return true;
        }


        bool IsSame(
            const game::Transform & a,
            const game::Transform & b
        )
        {
            return std::memcmp( &a, &b, sizeof( game::Transform ) ) == 0;
        }


        game::Transform Read(
            const float * position,
            const float * rotation
        )
        {
            game::Transform result = {};

            // Rotation only entries carry a zero position that is never written back.
            if (position != nullptr)
            {
                std::memcpy( result.m_Position, position, sizeof( result.m_Position ) );
            }

            // Position only entries carry an identity rotation that is never written back.
            if (rotation != nullptr)
            {
                std::memcpy( result.m_Rotation, rotation, sizeof( result.m_Rotation ) );
            }
            else
            {
                result.m_Rotation[0] = 1.0f;
                result.m_Rotation[4] = 1.0f;
                result.m_Rotation[8] = 1.0f;
            }

            return result;
        }


        void Write(
            float * position,
            float * rotation,
            const game::Transform & transform
        )
        {
            if (position != nullptr)
            {
                std::memcpy( position, transform.m_Position, sizeof( transform.m_Position ) );
            }

            if (rotation != nullptr)
            {
                std::memcpy( rotation, transform.m_Rotation, sizeof( transform.m_Rotation ) );
            }
        }


        Vec3 Row(
            const float * matrix,
            int index
        )
        {
            return { matrix[index * 3], matrix[index * 3 + 1], matrix[index * 3 + 2] };
        }


        float Dot(
            const Vec3 & a,
            const Vec3 & b
        )
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }


        Vec3 Cross(
            const Vec3 & a,
            const Vec3 & b
        )
        {
            return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
        }


        Vec3 Scale(
            const Vec3 & v,
            float factor
        )
        {
            return { v[0] * factor, v[1] * factor, v[2] * factor };
        }


        Vec3 Subtract(
            const Vec3 & a,
            const Vec3 & b
        )
        {
            return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
        }


        float Length(
            const Vec3 & v
        )
        {
            return std::sqrt( Dot( v, v ) );
        }


        Vec3 Normalize(
            const Vec3 & v
        )
        {
            const float length = Length( v );
            return length > 1e-6f ? Scale( v, 1.0f / length ) : v;
        }


        // Cosine of the angle between the matching rows of two rotations, the smallest of the three.
        float RotationSimilarity(
            const float * a,
            const float * b
        )
        {
            float result = 1.0f;

            for (int row = 0; row < 3; ++row)
            {
                result = std::min( result, Dot( Normalize( Row( a, row ) ), Normalize( Row( b, row ) ) ) );
            }

            return result;
        }


        // Linear blend followed by Gram Schmidt, keeps the handedness and row lengths of the source.
        void BlendRotation(
            const float * from,
            const float * to,
            float alpha,
            float * out
        )
        {
            std::array< Vec3, 3 > rows = {};
            std::array< float, 3 > lengths = {};

            for (int row = 0; row < 3; ++row)
            {
                const Vec3 a = Row( from, row );
                const Vec3 b = Row( to, row );
                rows[row] = { a[0] + (b[0] - a[0]) * alpha, a[1] + (b[1] - a[1]) * alpha, a[2] + (b[2] - a[2]) * alpha };
                lengths[row] = Length( a ) + (Length( b ) - Length( a )) * alpha;
            }

            const Vec3 x_axis = Normalize( rows[0] );
            const Vec3 y_axis = Normalize( Subtract( rows[1], Scale( x_axis, Dot( rows[1], x_axis ) ) ) );
            Vec3 z_axis = Cross( x_axis, y_axis );

            if (Dot( z_axis, rows[2] ) < 0.0f)
            {
                z_axis = Scale( z_axis, -1.0f );
            }

            const std::array< Vec3, 3 > result = { Scale( x_axis, lengths[0] ), Scale( y_axis, lengths[1] ), Scale( z_axis, lengths[2] ) };

            for (int row = 0; row < 3; ++row)
            {
                out[row * 3] = result[row][0];
                out[row * 3 + 1] = result[row][1];
                out[row * 3 + 2] = result[row][2];
            }
        }


        game::Transform Blend(
            const game::Transform & from,
            const game::Transform & to,
            float alpha
        )
        {
            game::Transform result = {};

            for (int i = 0; i < 3; ++i)
            {
                result.m_Position[i] = from.m_Position[i] + (to.m_Position[i] - from.m_Position[i]) * alpha;
            }

            BlendRotation( from.m_Rotation, to.m_Rotation, alpha, result.m_Rotation );
            return result;
        }


        float Distance(
            const game::Transform & a,
            const game::Transform & b
        )
        {
            const Vec3 delta = { a.m_Position[0] - b.m_Position[0], a.m_Position[1] - b.m_Position[1], a.m_Position[2] - b.m_Position[2] };
            return Length( delta );
        }
    }

    Interpolation & Interpolation::GetInstance()
    {
        static Interpolation instance;
        return instance;
    }


    bool Interpolation::IsRaceRunning()
    {
        return hook::Ref<std::int32_t>( game::var::RACE_PAUSED ) == 0 && hook::Ref<std::int32_t>( game::var::RACE_SUSPENDED ) == 0 &&
            hook::Ref<std::int32_t>( game::var::RACE_EXIT ) == 0;
    }


    int __cdecl Interpolation::SimStepHook()
    {
        Interpolation & self = GetInstance();

        const int result = self.m_SimStepHook->ccall< int >();
        self.OnSimStep();
        return result;
    }


    void Interpolation::ResetClock(
        std::int32_t step,
        std::int64_t now
    )
    {
        m_AnchorStep = step;
        m_AnchorTime = now;
        m_LatenessMin = 0.0;
        m_ClockValid = true;
        m_HaveLastRenderStep = false;
    }


    void Interpolation::UpdateClock(
        std::int32_t step,
        std::int64_t now
    )
    {
        if (m_StepPeriod <= 0.0)
        {
            m_StepPeriod = static_cast<double>(QpcFrequency()) / DEFAULT_STEPS_PER_SECOND;
        }

        const bool continuous = m_ClockValid && step == m_LastStep + 1 &&
            static_cast<double>(now - m_LastStepTime) <= m_StepPeriod * CLOCK_GAP_PERIODS;

        if (!continuous)
        {
            if (m_ClockValid && step <= m_LastStep)
            {
                // Counter went back, a new race or a restart.
                m_Tracks.clear();
            }

            ResetClock( step, now );
        }
        else
        {
            // Steps are never processed before they are due, the earliest one marks the true schedule.
            const double ideal = static_cast<double>(m_AnchorTime) + static_cast<double>(step - m_AnchorStep) * m_StepPeriod;
            const double lateness = static_cast<double>(now) - ideal;

            m_LatenessMin = std::min( lateness, m_LatenessMin + m_StepPeriod * LATENESS_RELAX_PERIODS );

            if (!m_PeriodLocked && step - m_AnchorStep >= PERIOD_MEASURE_STEPS)
            {
                const double measured = static_cast<double>(now - m_AnchorTime) / static_cast<double>(step - m_AnchorStep);
                const double steps_per_second = static_cast<double>(QpcFrequency()) / measured;
                double locked_rate = steps_per_second;

                for (const double candidate : { 32.0, 64.0, 128.0 })
                {
                    if (std::abs( steps_per_second - candidate ) < candidate * 0.03)
                    {
                        locked_rate = candidate;
                    }
                }

                m_StepPeriod = static_cast<double>(QpcFrequency()) / locked_rate;
                m_PeriodLocked = true;

                LOG_INFO( "Interpolation measured {:.2f} simulation steps per second, using {:.2f}", steps_per_second, locked_rate );
                ResetClock( step, now );
            }
        }

        m_LastStep = step;
        m_LastStepTime = now;
    }


    double Interpolation::CurrentStep(
        std::int64_t now
    ) const
    {
        return static_cast<double>(m_AnchorStep) +
            (static_cast<double>(now - m_AnchorTime) - m_LatenessMin) / m_StepPeriod;
    }


    void Interpolation::Track::Push(
        const Sample & sample
    )
    {
        if (m_Count == HISTORY_SIZE)
        {
            std::move( m_History.begin() + 1, m_History.end(), m_History.begin() );
            --m_Count;
        }
        m_History[m_Count++] = sample;
    }


    void Interpolation::Track::Reset(
        const Sample & sample
    )
    {
        m_History[0] = sample;
        m_Count = 1;
    }


    void Interpolation::UpdateRenderDelay(
        std::int32_t step
    )
    {
        std::int32_t needed = 1;

        for (const auto & [key, track] : m_Tracks)
        {
            if (track.m_LastSeenStep == step && track.m_RendererOwnedFrames == 0)
            {
                needed = std::max( needed, track.m_StableSpacing );
            }
        }

        // Grow at once, shrink only after the larger delay was not needed for a while.
        if (needed >= m_RenderDelay)
        {
            m_DelayNeededAt = step;
        }

        const bool grow = needed > m_RenderDelay;
        const bool shrink = needed < m_RenderDelay && step - m_DelayNeededAt > DELAY_SHRINK_STEPS;

        if (grow || shrink)
        {
            LOG_INFO( "Interpolation render delay {} steps", needed );

            m_RenderDelay = needed;
            m_DelayNeededAt = step;
        }
    }


    void Interpolation::Capture(
        float * position,
        float * rotation,
        std::int32_t step
    )
    {
        const game::Transform state = Read( position, rotation );

        if (!IsFinite( state ))
            return;

        const void * key = position != nullptr ? static_cast<const void *>(position) : static_cast<const void *>(rotation);
        auto [it, inserted] = m_Tracks.try_emplace( key );
        Track & track = it->second;

        if (inserted || track.m_Rotation != rotation || track.m_Position != position || track.m_Count == 0 || track.m_ResetPending)
        {
            const std::int32_t owned_frames = inserted ? 0 : track.m_RendererOwnedFrames;

            track = Track {};
            track.m_Position = position;
            track.m_Rotation = rotation;
            track.m_RendererOwnedFrames = owned_frames;
            track.Reset( { state, step } );
            track.m_LastSeenStep = step;
            return;
        }

        track.m_LastSeenStep = step;

        const Sample & newest = track.Newest();

        if (IsSame( state, newest.m_State ))
            return;

        // Teleports, resets and camera cuts are shown at once, never blended.
        if (Distance( newest.m_State, state ) > m_Settings.m_SnapDistance ||
            RotationSimilarity( newest.m_State.m_Rotation, state.m_Rotation ) < SNAP_ROTATION_DOT)
        {
            track.Reset( { state, step } );
            return;
        }

        const std::int32_t spacing = step - newest.m_Step;

        if (spacing <= MAX_SPACING)
        {
            track.m_StableSpacing = spacing == track.m_LastSpacing ? spacing : track.m_StableSpacing;
            track.m_LastSpacing = spacing;
        }

        if (spacing > MAX_SPACING)
        {
            // Leaving a rest, the old state holds until just before the move.
            const Sample rest_end = { newest.m_State, step - m_RenderDelay };
            track.Push( rest_end );
        }

        track.Push( { state, step } );
    }


    void Interpolation::CaptureAll(
        std::int32_t step
    )
    {
        const auto capture_list = [this, step](
            game::GameAddr list,
            game::GameAddr count_address,
            std::int32_t capacity
            )
        {
            const std::int32_t count = std::clamp( hook::Ref<std::int32_t>( count_address ), 0, capacity );
            auto * const * cars = hook::Ptr<game::CarObject *>( list );

            for (std::int32_t i = 0; i < count; ++i)
            {
                game::CarObject * car = cars[i];

                if (car != nullptr)
                {
                    Capture( car->m_Position, car->m_Rotation, step );
                }
            }
        };

        capture_list( game::var::CAR_LIST_RACERS, game::var::CAR_LIST_RACERS_COUNT, 9 );
        capture_list( game::var::CAR_LIST_POLICE, game::var::CAR_LIST_POLICE_COUNT, 9 );
        capture_list( game::var::CAR_LIST_TRAFFIC, game::var::CAR_LIST_TRAFFIC_COUNT, 128 );

        if (m_Settings.m_InterpolateCamera)
        {
            auto * blocks = hook::Ptr<game::CameraBlock>( game::var::CAMERA_BLOCKS );

            for (std::size_t i = 0; i < game::var::CAMERA_BLOCK_COUNT; ++i)
            {
                // Position and rotation are separate tracks. In the chase cameras the race
                // renderer rewrites the rotation every frame, easing it towards the target,
                // and then owns it. The orbit and replay cameras only set it on simulation
                // steps, there it is blended like everything else.
                if (blocks[i].m_Target != nullptr)
                {
                    Capture( blocks[i].m_Position, nullptr, step );
                    Capture( nullptr, blocks[i].m_Rotation, step );
                }
            }
        }

        if ((step & 63) == 0)
        {
            std::erase_if( m_Tracks, [step] ( const auto & entry )
            {
                return step - entry.second.m_LastSeenStep > TRACK_EXPIRY_STEPS;
            } );
        }
    }


    void Interpolation::OnSimStep()
    {
        const std::int64_t now = QpcNow();
        const std::int32_t step = hook::Ref<std::int32_t>( game::var::SIM_STEP_COUNT );

        UpdateClock( step, now );
        CaptureAll( step );
        UpdateRenderDelay( step );
    }


    void Interpolation::Apply(
        std::int64_t now
    )
    {
        m_Applied.clear();

        // One render time for every entity keeps cars and cameras consistent with each other.
        double render_step = CurrentStep( now ) - static_cast<double>(m_RenderDelay);

        if (m_HaveLastRenderStep && render_step < m_LastRenderStep)
        {
            // A clock correction would show older states again, hold the previous time instead.
            render_step = m_LastRenderStep;
        }

        m_LastRenderStep = render_step;
        m_HaveLastRenderStep = true;

        for (auto & [key, track] : m_Tracks)
        {
            if (track.m_LastSeenStep != m_LastStep)
                continue;

            if (track.m_RendererOwnedFrames > 0)
            {
                AppliedEntry watch;
                watch.m_Track = &track;
                watch.m_WatchOnly = true;
                watch.m_Position = track.m_Position;
                watch.m_Rotation = track.m_Rotation;
                watch.m_Saved = Read( track.m_Position, track.m_Rotation );
                m_Applied.push_back( watch );
                continue;
            }

            if (track.m_Count < 2)
            {
                continue;
            }

            // Something outside the simulation step moved the entity, a collision fix up or a
            // reset. The new value becomes the newest state so the blend continues from it.
            const game::Transform live = Read( track.m_Position, track.m_Rotation );

            if (!IsSame( live, track.Newest().m_State ))
            {
                if (!IsFinite( live ))
                {
                    continue;
                }
                Sample & newest = track.m_History[track.m_Count - 1];
                if (newest.m_Step == m_LastStep)
                {
                    newest.m_State = live;
                }
                else
                {
                    track.Push( { live, m_LastStep } );
                }
            }

            // Newest pair whose older entry is not after the render time.
            std::int32_t index = track.m_Count - 2;
            while (index > 0 && static_cast<double>(track.m_History[index].m_Step) > render_step)
            {
                --index;
            }

            const Sample & from = track.m_History[index];
            const Sample & to = track.m_History[index + 1];

            const double span = std::max( 1, to.m_Step - from.m_Step );
            const double alpha = std::clamp( (render_step - static_cast<double>(from.m_Step)) / span, 0.0, 1.0 );

            AppliedEntry entry;
            entry.m_Track = &track;
            entry.m_Position = track.m_Position;
            entry.m_Rotation = track.m_Rotation;
            entry.m_Saved = live;

            if (alpha <= 0.0)
            {
                entry.m_Written = from.m_State;
            }
            else if (alpha >= 1.0)
            {
                entry.m_Written = to.m_State;
            }
            else
            {
                entry.m_Written = Blend( from.m_State, to.m_State, static_cast<float>(alpha) );
            }

            if (!IsFinite( entry.m_Written ) || IsSame( entry.m_Written, live ))
                continue;

            Write( entry.m_Position, entry.m_Rotation, entry.m_Written );
            m_Applied.push_back( entry );
        }
    }


    void Interpolation::Restore()
    {
        for (const AppliedEntry & entry : m_Applied)
        {
            const game::Transform after = Read( entry.m_Position, entry.m_Rotation );
            if (entry.m_WatchOnly)
            {
                // Still written by the renderer, keep it owned, otherwise count down.
                Track & track = *entry.m_Track;

                if (!IsSame( after, entry.m_Saved ))
                {
                    track.m_RendererOwnedFrames = RENDERER_OWNED_FRAMES;
                }
                else if (--track.m_RendererOwnedFrames == 0)
                {
                    // Recorded states came from the renderer, start over from simulation values.
                    track.m_ResetPending = true;
                }
                continue;
            }

            if (IsSame( after, entry.m_Written ))
            {
                Write( entry.m_Position, entry.m_Rotation, entry.m_Saved );
            }
            else
            {
                // The frame itself wrote this value, it is kept and the renderer owns it for now.
                entry.m_Track->m_RendererOwnedFrames = RENDERER_OWNED_FRAMES;
            }
        }

        m_Applied.clear();
    }


    void Interpolation::RenderCheckMidHook(
        safetyhook::Context &
    )
    {
        Interpolation & self = GetInstance();

        if (!self.m_ClockValid || !IsRaceRunning())
            return;

        const std::int64_t now = QpcNow();

        // Restarting after a pause or a load, wait for the next step before blending.
        if (static_cast<double>(now - self.m_LastStepTime) > self.m_StepPeriod * CLOCK_GAP_PERIODS)
            return;

        auto & skip_render = hook::Ref<std::int32_t>( game::var::RACE_SKIP_RENDER );

        if (skip_render != 0 && hook::Ref<std::uint8_t>( game::var::RACE_MENU_RETURN ) == 0)
        {
            // No new step, draw anyway so the blend can move on.
            skip_render = 0;
        }

        if (skip_render == 0)
        {
            self.Apply( now );
        }
    }


    void Interpolation::AfterRenderMidHook(
        safetyhook::Context &
    )
    {
        Interpolation & self = GetInstance();
        self.Restore();
    }


    bool Interpolation::Install(
        const FramerateSettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Enabled || !settings.m_Interpolation)
        {
            LOG_INFO( "Interpolation disabled by config" );
            return true;
        }

        namespace fn = game::fn;
        namespace site = game::site;
        m_SimStepHook = hook::CreateInline( "SimStep", fn::SIM_STEP.m_Address, fn::SIM_STEP.m_Signature, reinterpret_cast<void *>(&SimStepHook) );

        // Both race loop sites are checked before either is hooked, a half install would leak blended values.
        if (m_SimStepHook == nullptr || !hook::MatchBytes( site::RACE_RENDER_CHECK.m_Address, site::RACE_RENDER_CHECK.m_Signature ) ||
            !hook::MatchBytes( site::RACE_AFTER_RENDER.m_Address, site::RACE_AFTER_RENDER.m_Signature ))
        {
            LOG_ERROR( "Interpolation sites do not match, interpolation stays off" );
            m_SimStepHook.reset();
            return false;
        }

        m_AfterRenderHook = hook::CreateMid( "RaceAfterRender", site::RACE_AFTER_RENDER.m_Address, site::RACE_AFTER_RENDER.m_Signature,
            &AfterRenderMidHook );

        if (m_AfterRenderHook != nullptr)
        {
            m_RenderCheckHook = hook::CreateMid( "RaceRenderCheck", site::RACE_RENDER_CHECK.m_Address, site::RACE_RENDER_CHECK.m_Signature,
                &RenderCheckMidHook );
        }

        if (m_RenderCheckHook == nullptr)
        {
            LOG_ERROR( "Interpolation render hooks failed, interpolation stays off" );

            m_AfterRenderHook.reset();
            m_SimStepHook.reset();
            return false;
        }

        m_Tracks.reserve( 256 );
        m_Applied.reserve( 256 );
        m_Installed = true;

        LOG_INFO( "Interpolation installed, camera {} snap distance {:.1f}", settings.m_InterpolateCamera, settings.m_SnapDistance );
        return true;
    }


    void Interpolation::Uninstall()
    {
        Restore();

        m_RenderCheckHook.reset();
        m_AfterRenderHook.reset();
        m_SimStepHook.reset();
        m_Installed = false;
    }
}