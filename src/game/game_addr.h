#pragma once

// Game addresses are stored for the link time image base of 'Porsche.exe' and rebased at run time,
// because Windows may load the image elsewhere (for example Mandatory ASLR).
// There is no implicit conversion to an integer or pointer, 'Get()' is the only way out so no
// code can use a link time address by accident.

#include <cstddef>
#include <cstdint>

namespace nfs5::game
{
    // Run time image base minus the link time image base, set once by 'VerifyExecutable'.
    inline std::intptr_t g_ImageDelta = 0;

    class GameAddr
    {
    private:

        std::uintptr_t m_Link = 0;

    public:

        constexpr GameAddr() = default;

        constexpr GameAddr(
            std::uintptr_t link_address
        )
            : m_Link( link_address )
        {
        }

        [[nodiscard]] std::uintptr_t Get() const noexcept
        {
            return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(m_Link) + g_ImageDelta);
        }

        // The address as stored in 'Porsche.exe' on disk.
        [[nodiscard]] constexpr std::uintptr_t Link() const noexcept
        {
            return m_Link;
        }

        [[nodiscard]] constexpr GameAddr operator+(
            std::size_t offset
        ) const noexcept
        {
            return GameAddr( m_Link + offset );
        }
    };
}
