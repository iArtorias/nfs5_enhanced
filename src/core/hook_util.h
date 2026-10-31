#pragma once

// Hooking helpers.
// Every hook site is checked against an expected byte signature before anything is written.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <safetyhook.hpp>

#include "../game/game_addr.h"
#include "win_handle.h"

namespace nfs5::hook
{
    // Typed access to a fixed game address.
    template <typename T>
    [[nodiscard]] inline T & Ref(
        std::uintptr_t address
    ) noexcept
    {
        return *reinterpret_cast<T *>(address);
    }

    template <typename T>
    [[nodiscard]] inline T * Ptr(
        std::uintptr_t address
    ) noexcept
    {
        return reinterpret_cast<T *>(address);
    }

    template <typename T>
    [[nodiscard]] inline T & Ref(
        game::GameAddr address
    ) noexcept
    {
        return *reinterpret_cast<T *>(address.Get());
    }

    template <typename T>
    [[nodiscard]] inline T * Ptr(
        game::GameAddr address
    ) noexcept
    {
        return reinterpret_cast<T *>(address.Get());
    }

    // Parses '8B 44 24 ? 53' style signatures, '?' is a wildcard.
    [[nodiscard]] std::vector< std::int16_t > ParsePattern(
        std::string_view pattern
    );

    // Absolute operands inside the main executable image are compared as stored on disk, so a
    // signature written for the link time image base also matches a relocated image.
    [[nodiscard]] bool MatchBytes(
        std::uintptr_t address,
        std::string_view pattern
    );

    [[nodiscard]] inline bool MatchBytes(
        game::GameAddr address,
        std::string_view pattern
    )
    {
        return MatchBytes( address.Get(), pattern );
    }

    // Searches a loaded module image for a pattern, returns '0' if not found
    // or if more than one match exists when unique is requested.
    [[nodiscard]] std::uintptr_t FindPattern(
        HMODULE module,
        std::string_view pattern,
        bool unique
    );

    [[nodiscard]] std::unique_ptr< safetyhook::InlineHook > CreateInline(
        std::string_view name,
        std::uintptr_t target,
        std::string_view expected,
        void * destination
    );

    [[nodiscard]] std::unique_ptr< safetyhook::MidHook > CreateMid(
        std::string_view name,
        std::uintptr_t target,
        std::string_view expected,
        safetyhook::MidHookFn destination
    );

    [[nodiscard]] inline std::unique_ptr< safetyhook::InlineHook > CreateInline(
        std::string_view name,
        game::GameAddr target,
        std::string_view expected,
        void * destination
    )
    {
        return CreateInline( name, target.Get(), expected, destination );
    }

    [[nodiscard]] inline std::unique_ptr< safetyhook::MidHook > CreateMid(
        std::string_view name,
        game::GameAddr target,
        std::string_view expected,
        safetyhook::MidHookFn destination
    )
    {
        return CreateMid( name, target.Get(), expected, destination );
    }

    // Rewrites a 32 bit absolute pointer operand inside a verified instruction
    // The original value is restored when the object is destroyed.
    class OperandPatch
    {
    private:
        std::uintptr_t m_Address = 0;
        std::uint32_t m_Original = 0;
        bool m_Applied = false;

    public:

        OperandPatch() = default;
        ~OperandPatch();

        OperandPatch( const OperandPatch & ) = delete;
        OperandPatch & operator=( const OperandPatch & ) = delete;

        bool Apply(
            std::string_view name,
            std::uintptr_t instruction,
            std::string_view expected,
            std::size_t operand_offset,
            std::uint32_t value
        );

        bool Apply(
            std::string_view name,
            game::GameAddr instruction,
            std::string_view expected,
            std::size_t operand_offset,
            std::uint32_t value
        )
        {
            return Apply( name, instruction.Get(), expected, operand_offset, value );
        }

        void Restore();

        [[nodiscard]] bool IsApplied() const
        {
            return m_Applied;
        }
    };

    // Replaces a few verified bytes, the original bytes are restored when the object is destroyed.
    class BytePatch
    {
    private:

        std::uintptr_t m_Address = 0;
        std::vector< std::uint8_t > m_Original;
        bool m_Applied = false;

    public:

        BytePatch() = default;
        ~BytePatch();

        BytePatch( const BytePatch & ) = delete;
        BytePatch & operator=( const BytePatch & ) = delete;

        // 'expected' covers the patched bytes and may continue past them.
        bool Apply(
            std::string_view name,
            std::uintptr_t address,
            std::string_view expected,
            std::span<const std::uint8_t> replacement
        );

        bool Apply(
            std::string_view name,
            game::GameAddr address,
            std::string_view expected,
            std::span<const std::uint8_t> replacement
        )
        {
            return Apply( name, address.Get(), expected, replacement );
        }

        void Restore();

        [[nodiscard]] bool IsApplied() const
        {
            return m_Applied;
        }
    };

    // Redirects one 'E8 rel32' call site, keeps the original target for chaining.
    class CallSitePatch
    {
    private:

        std::uintptr_t m_Site = 0;
        std::int32_t m_OriginalRel = 0;
        std::uintptr_t m_OriginalTarget = 0;
        bool m_Applied = false;

    public:

        CallSitePatch() = default;
        ~CallSitePatch();

        CallSitePatch( const CallSitePatch & ) = delete;
        CallSitePatch & operator=( const CallSitePatch & ) = delete;

        bool Apply(
            std::string_view name,
            std::uintptr_t site,
            void * destination
        );

        bool Apply(
            std::string_view name,
            game::GameAddr site,
            void * destination
        )
        {
            return Apply( name, site.Get(), destination );
        }

        void Restore();

        [[nodiscard]] std::uintptr_t GetOriginalTarget() const
        {
            return m_OriginalTarget;
        }

        [[nodiscard]] bool IsApplied() const
        {
            return m_Applied;
        }
    };

    // Swaps one import address table slot of a module by library and function name.
    class IatPatch
    {
    private:

        void ** m_Slot = nullptr;
        void * m_Original = nullptr;

    public:

        IatPatch() = default;
        ~IatPatch();

        IatPatch( const IatPatch & ) = delete;
        IatPatch & operator=( const IatPatch & ) = delete;

        bool Apply(
            HMODULE module,
            std::string_view dll_name,
            std::string_view function_name,
            void * destination
        );

        void Restore();

        template <typename T>
        [[nodiscard]] T GetOriginal() const
        {
            return reinterpret_cast<T>(m_Original);
        }

        [[nodiscard]] bool IsApplied() const
        {
            return m_Slot != nullptr;
        }
    };
}