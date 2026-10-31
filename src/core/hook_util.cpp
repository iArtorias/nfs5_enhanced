#include "hook_util.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "log.h"

namespace nfs5::hook
{
    namespace
    {
        int HexValue(
            char ch
        )
        {
            if (ch >= '0' && ch <= '9')
            {
                return ch - '0';
            }

            if (ch >= 'a' && ch <= 'f')
            {
                return ch - 'a' + 10;
            }

            if (ch >= 'A' && ch <= 'F')
            {
                return ch - 'A' + 10;
            }

            return -1;
        }


        bool IsReadable(
            std::uintptr_t address,
            std::size_t size
        )
        {
            MEMORY_BASIC_INFORMATION info = {};
            if (::VirtualQuery( reinterpret_cast<const void *>(address), &info, sizeof( info ) ) == 0)
                return false;

            if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
                return false;

            const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            return address + size <= region_end;
        }


        std::string DumpBytes(
            std::uintptr_t address,
            std::size_t size
        )
        {
            std::string result;

            if (!IsReadable( address, size ))
            {
                return "<unreadable>";
            }

            const auto * bytes = reinterpret_cast<const std::uint8_t *>(address);

            for (std::size_t i = 0; i < size; ++i)
            {
                result += std::format( "{:02X} ", bytes[i] );
            }

            return result;
        }

        // Relocated dword positions (RVAs) of the main executable image, read from the .reloc
        // directory of the file on disk because that section is discardable in memory.
        struct RelocTable
        {
            std::uintptr_t m_Base = 0;
            std::size_t m_Size = 0;
            std::vector< std::uint32_t > m_Rvas;
        };


        const RelocTable & GetRelocTable()
        {
            static const RelocTable table = []()
            {
                RelocTable result;

                if (game::g_ImageDelta == 0)
                    return result;

                const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW( nullptr ));
                const auto * dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
                const auto * nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
                const IMAGE_DATA_DIRECTORY & directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

                result.m_Base = base;
                result.m_Size = nt->OptionalHeader.SizeOfImage;

                if (directory.VirtualAddress == 0 || directory.Size == 0)
                {
                    LOG_ERROR( "Executable image has no relocation directory, signatures with absolute operands will not match" );
                    return result;
                }

                std::wstring path( MAX_PATH, L'\0' );
                path.resize( ::GetModuleFileNameW( nullptr, path.data(), static_cast<DWORD>(path.size()) ) );

                std::ifstream file( path, std::ios::binary );
                const IMAGE_SECTION_HEADER * section = IMAGE_FIRST_SECTION( nt );
                std::vector< std::uint8_t > block;

                for (WORD i = 0; file && i < nt->FileHeader.NumberOfSections; ++i, ++section)
                {
                    const DWORD extent = (std::max)( section->Misc.VirtualSize, section->SizeOfRawData );

                    if (directory.VirtualAddress < section->VirtualAddress || directory.VirtualAddress >= section->VirtualAddress + extent)
                        continue;

                    block.resize( directory.Size );
                    file.seekg( section->PointerToRawData + (directory.VirtualAddress - section->VirtualAddress) );
                    file.read( reinterpret_cast<char *>(block.data()), static_cast<std::streamsize>(block.size()) );

                    if (!file)
                        block.clear();

                    break;
                }

                for (std::size_t offset = 0; offset + sizeof( IMAGE_BASE_RELOCATION ) <= block.size();)
                {
                    IMAGE_BASE_RELOCATION header = {};
                    std::memcpy( &header, block.data() + offset, sizeof( header ) );

                    if (header.SizeOfBlock < sizeof( header ) || offset + header.SizeOfBlock > block.size())
                        break;

                    const std::size_t count = (header.SizeOfBlock - sizeof( header )) / sizeof( WORD );

                    for (std::size_t e = 0; e < count; ++e)
                    {
                        WORD entry = 0;
                        std::memcpy( &entry, block.data() + offset + sizeof( header ) + e * sizeof( WORD ), sizeof( entry ) );

                        if ((entry >> 12) == IMAGE_REL_BASED_HIGHLOW)
                        {
                            result.m_Rvas.push_back( header.VirtualAddress + (entry & 0x0FFF) );
                        }
                    }

                    offset += header.SizeOfBlock;
                }

                std::sort( result.m_Rvas.begin(), result.m_Rvas.end() );

                if (result.m_Rvas.empty())
                {
                    LOG_ERROR( "Could not read the executable relocation table, signatures with absolute operands will not match" );
                }
                else
                {
                    LOG_INFO( "Executable relocation table loaded, {} entries", result.m_Rvas.size() );
                }

                return result;
            }();

            return table;
        }


        // Copies 'size' bytes and turns every relocated dword that overlaps them back into the
        // value stored on disk, so they can be compared with a link time signature.
        std::vector< std::uint8_t > ReadUnrelocated(
            std::uintptr_t address,
            std::size_t size
        )
        {
            std::vector< std::uint8_t > bytes( reinterpret_cast<const std::uint8_t *>(address), reinterpret_cast<const std::uint8_t *>(address) + size );

            if (game::g_ImageDelta == 0)
                return bytes;

            const RelocTable & table = GetRelocTable();

            if (address < table.m_Base || address - table.m_Base >= table.m_Size)
                return bytes;

            const auto first_rva = static_cast<std::uint32_t>(address - table.m_Base);
            const std::uint32_t window_start = first_rva >= 3 ? first_rva - 3 : 0;
            auto it = std::lower_bound( table.m_Rvas.begin(), table.m_Rvas.end(), window_start );

            for (; it != table.m_Rvas.end() && *it < first_rva + size; ++it)
            {
                std::uint32_t value = 0;
                std::memcpy( &value, reinterpret_cast<const void *>(table.m_Base + *it), sizeof( value ) );
                value -= static_cast<std::uint32_t>(game::g_ImageDelta);

                for (std::size_t k = 0; k < sizeof( value ); ++k)
                {
                    const std::int64_t position = static_cast<std::int64_t>(*it) + static_cast<std::int64_t>(k) - static_cast<std::int64_t>(first_rva);

                    if (position >= 0 && position < static_cast<std::int64_t>(size))
                    {
                        bytes[static_cast<std::size_t>(position)] = static_cast<std::uint8_t>(value >> (8 * k));
                    }
                }
            }

            return bytes;
        }
    }


    std::vector< std::int16_t > ParsePattern(
        std::string_view pattern
    )
    {
        std::vector< std::int16_t > result;
        std::size_t i = 0;

        while (i < pattern.size())
        {
            const char ch = pattern[i];

            if (ch == ' ')
            {
                ++i;
                continue;
            }

            if (ch == '?')
            {
                result.push_back( -1 );

                while (i < pattern.size() && pattern[i] == '?')
                {
                    ++i;
                }
                continue;
            }

            if (i + 1 < pattern.size())
            {
                const int high = HexValue( pattern[i] );
                const int low = HexValue( pattern[i + 1] );

                if (high >= 0 && low >= 0)
                {
                    result.push_back( static_cast<std::int16_t>((high << 4) | low) );
                }
            }

            i += 2;
        }

        return result;
    }


    bool MatchBytes(
        std::uintptr_t address,
        std::string_view pattern
    )
    {
        const std::vector< std::int16_t > bytes = ParsePattern( pattern );

        if (bytes.empty() || !IsReadable( address, bytes.size() ))
            return false;

        const std::vector< std::uint8_t > memory = ReadUnrelocated( address, bytes.size() );

        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            if (bytes[i] >= 0 && memory[i] != static_cast<std::uint8_t>( bytes[i] ))
            {
                return false;
            }
        }

        return true;
    }


    std::uintptr_t FindPattern(
        HMODULE module,
        std::string_view pattern,
        bool unique
    )
    {
        if (module == nullptr)
            return 0;

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto * dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        const auto * nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        const std::vector< std::int16_t > bytes = ParsePattern( pattern );

        if (bytes.empty())
            return 0;

        std::uintptr_t found = 0;
        const IMAGE_SECTION_HEADER * section = IMAGE_FIRST_SECTION( nt );

        for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section)
        {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const auto * begin = reinterpret_cast<const std::uint8_t *>( base + section->VirtualAddress );
            const std::size_t size = section->Misc.VirtualSize;

            if (size < bytes.size())
                continue;

            for (std::size_t i = 0; i + bytes.size() <= size; ++i)
            {
                bool match = true;

                for (std::size_t j = 0; j < bytes.size(); ++j)
                {
                    if (bytes[j] >= 0 && begin[i + j] != static_cast<std::uint8_t>( bytes[j] ))
                    {
                        match = false;
                        break;
                    }
                }

                if (!match)
                    continue;

                if (!unique)
                    return reinterpret_cast<std::uintptr_t>(begin + i);

                if (found != 0)
                    return 0;

                found = reinterpret_cast<std::uintptr_t>(begin + i);
            }
        }

        return found;
    }


    std::unique_ptr< safetyhook::InlineHook > CreateInline(
        std::string_view name,
        std::uintptr_t target,
        std::string_view expected,
        void * destination
    )
    {
        if (!MatchBytes( target, expected ))
        {
            LOG_ERROR( "Hook {} at {:08X} signature mismatch, found {}", name, target, DumpBytes( target, 16 ) );
            return nullptr;
        }

        auto result = safetyhook::InlineHook::create( reinterpret_cast<void *>(target), destination );

        if (!result)
        {
            LOG_ERROR( "Hook {} at {:08X} failed to install, error type {}", name, target, static_cast<int>(result.error().type) );
            return nullptr;
        }

        LOG_INFO( "Hook {} installed at {:08X}", name, target );
        return std::make_unique<safetyhook::InlineHook>( std::move( *result ) );
    }


    std::unique_ptr< safetyhook::MidHook > CreateMid(
        std::string_view name,
        std::uintptr_t target,
        std::string_view expected,
        safetyhook::MidHookFn destination
    )
    {
        if (!MatchBytes( target, expected ))
        {
            LOG_ERROR( "MidHook {} at {:08X} signature mismatch, found {}", name, target, DumpBytes( target, 16 ) );
            return nullptr;
        }

        auto result = safetyhook::MidHook::create( reinterpret_cast<void *>(target), destination );

        if (!result)
        {
            LOG_ERROR( "MidHook {} at {:08X} failed to install, error type {}", name, target, static_cast<int>(result.error().type) );
            return nullptr;
        }

        LOG_INFO( "MidHook {} installed at {:08X}", name, target );
        return std::make_unique<safetyhook::MidHook>( std::move( *result ) );
    }

    OperandPatch::~OperandPatch()
    {
        Restore();
    }

    bool OperandPatch::Apply(
        std::string_view name,
        std::uintptr_t instruction,
        std::string_view expected,
        std::size_t operand_offset,
        std::uint32_t value
    )
    {
        if (m_Applied)
        {
            return true;
        }

        if (!MatchBytes( instruction, expected ))
        {
            LOG_ERROR( "Operand patch {} at {:08X} signature mismatch, found {}", name, instruction, DumpBytes( instruction, 16 ) );
            return false;
        }

        m_Address = instruction + operand_offset;

        ScopedPageProtection protection( reinterpret_cast<void *>(m_Address), sizeof( std::uint32_t ) );

        if (!protection.IsChanged())
        {
            LOG_ERROR( "Operand patch {} could not unprotect {:08X}", name, m_Address );
            return false;
        }

        std::memcpy( &m_Original, reinterpret_cast<const void *>(m_Address), sizeof( m_Original ) );
        std::memcpy( reinterpret_cast<void *>(m_Address), &value, sizeof( value ) );
        m_Applied = true;

        LOG_INFO( "Operand patch {} at {:08X} {:08X} -> {:08X}", name, m_Address, m_Original, value );
        return true;
    }


    void OperandPatch::Restore()
    {
        if (!m_Applied)
            return;

        ScopedPageProtection protection( reinterpret_cast<void *>(m_Address), sizeof( std::uint32_t ) );

        if (protection.IsChanged())
        {
            std::memcpy( reinterpret_cast<void *>(m_Address), &m_Original, sizeof( m_Original ) );
        }

        m_Applied = false;
    }

    BytePatch::~BytePatch()
    {
        Restore();
    }

    bool BytePatch::Apply(
        std::string_view name,
        std::uintptr_t address,
        std::string_view expected,
        std::span< const std::uint8_t > replacement
    )
    {
        if (m_Applied)
            return true;

        if (replacement.empty() || !MatchBytes( address, expected ))
        {
            LOG_ERROR( "Byte patch {} at {:08X} signature mismatch, found {}", name, address, DumpBytes( address, 16 ) );
            return false;
        }

        ScopedPageProtection protection( reinterpret_cast<void *>(address), replacement.size() );

        if (!protection.IsChanged())
        {
            LOG_ERROR( "Byte patch {} could not unprotect {:08X}", name, address );
            return false;
        }

        m_Address = address;
        m_Original.assign( reinterpret_cast<const std::uint8_t *>(address), reinterpret_cast<const std::uint8_t *>(address) + replacement.size() );
        std::memcpy( reinterpret_cast<void *>(address), replacement.data(), replacement.size() );
        ::FlushInstructionCache( ::GetCurrentProcess(), reinterpret_cast<void *>(address), replacement.size() );

        m_Applied = true;

        LOG_INFO( "Byte patch {} at {:08X}, {} bytes", name, address, replacement.size() );
        return true;
    }


    void BytePatch::Restore()
    {
        if (!m_Applied)
            return;

        ScopedPageProtection protection( reinterpret_cast<void *>(m_Address), m_Original.size() );

        if (protection.IsChanged())
        {
            std::memcpy( reinterpret_cast<void *>(m_Address), m_Original.data(), m_Original.size() );
        }

        m_Applied = false;
    }


    CallSitePatch::~CallSitePatch()
    {
        Restore();
    }

    bool CallSitePatch::Apply(
        std::string_view name,
        std::uintptr_t site,
        void * destination
    )
    {
        if (m_Applied)
        {
            return true;
        }

        if (!MatchBytes( site, "E8 ? ? ? ?" ))
        {
            LOG_ERROR( "Call site patch {} at {:08X} is not a rel32 call, found {}", name, site, DumpBytes( site, 8 ) );
            return false;
        }

        std::memcpy( &m_OriginalRel, reinterpret_cast<const void *>(site + 1), sizeof( m_OriginalRel ) );
        m_OriginalTarget = site + 5 + static_cast<std::uintptr_t>(m_OriginalRel);

        const auto new_rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(destination) - (site + 5));

        ScopedPageProtection protection( reinterpret_cast<void *>(site + 1), sizeof( std::int32_t ) );

        if (!protection.IsChanged())
        {
            LOG_ERROR( "Call site patch {} could not unprotect {:08X}", name, site );
            return false;
        }

        std::memcpy( reinterpret_cast<void *>(site + 1), &new_rel, sizeof( new_rel ) );

        m_Site = site;
        m_Applied = true;

        LOG_INFO( "Call site patch {} at {:08X} original target {:08X}", name, site, m_OriginalTarget );
        return true;
    }

    void CallSitePatch::Restore()
    {
        if (!m_Applied)
            return;

        ScopedPageProtection protection( reinterpret_cast<void *>(m_Site + 1), sizeof( std::int32_t ) );

        if (protection.IsChanged())
        {
            std::memcpy( reinterpret_cast<void *>(m_Site + 1), &m_OriginalRel, sizeof( m_OriginalRel ) );
        }

        m_Applied = false;
    }


    IatPatch::~IatPatch()
    {
        Restore();
    }

    bool IatPatch::Apply(
        HMODULE module,
        std::string_view dll_name,
        std::string_view function_name,
        void * destination
    )
    {
        if (m_Slot != nullptr || module == nullptr)
            return m_Slot != nullptr;

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto * dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        const auto * nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY & directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

        if (directory.VirtualAddress == 0)
            return false;

        const auto * descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);

        for (; descriptor->Name != 0; ++descriptor)
        {
            const auto * module_name = reinterpret_cast<const char *>(base + descriptor->Name);

            if (_stricmp( module_name, std::string( dll_name ).c_str() ) != 0 || descriptor->OriginalFirstThunk == 0)
                continue;

            const auto * names = reinterpret_cast<const IMAGE_THUNK_DATA *>(base + descriptor->OriginalFirstThunk);
            auto ** slots = reinterpret_cast<void **>(base + descriptor->FirstThunk);

            for (std::size_t i = 0; names[i].u1.AddressOfData != 0; ++i)
            {
                if (IMAGE_SNAP_BY_ORDINAL( names[i].u1.Ordinal ))
                    continue;

                const auto * by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names[i].u1.AddressOfData);

                if (function_name != reinterpret_cast<const char *>(by_name->Name))
                    continue;

                ScopedPageProtection protection( &slots[i], sizeof( void * ), PAGE_READWRITE );

                if (!protection.IsChanged())
                    return false;

                m_Slot = &slots[i];
                m_Original = std::exchange( slots[i], destination );

                LOG_INFO( "IAT patch {}!{} slot {:08X}", dll_name, function_name, reinterpret_cast<std::uintptr_t>(m_Slot) );
                return true;
            }
        }

        LOG_WARN( "IAT patch {}!{} import not found", dll_name, function_name );
        return false;
    }


    void IatPatch::Restore()
    {
        if (m_Slot == nullptr)
            return;

        ScopedPageProtection protection( m_Slot, sizeof( void * ), PAGE_READWRITE );

        if (protection.IsChanged())
        {
            *m_Slot = m_Original;
        }

        m_Slot = nullptr;
    }
}