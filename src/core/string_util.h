#pragma once

#include <cstddef>

namespace nfs5
{
    // Copies a C string into a fixed char array. The text is cut to fit, the array is always
    // null terminated and the rest of it is zeroed, like 'strncpy( dest, source, N - 1 )' on a
    // zeroed array. A null source gives an empty string.
    template< std::size_t N >
    void CopyString(
        char ( & dest )[N],
        const char * source
    )
    {
        static_assert( N > 0 );

        std::size_t i = 0;

        if (source != nullptr)
        {
            for (; i + 1 < N && source[i] != '\0'; ++i)
            {
                dest[i] = source[i];
            }
        }

        for (; i < N; ++i)
        {
            dest[i] = '\0';
        }
    }
}
