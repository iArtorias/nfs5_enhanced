#include "io_hooks.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <string_view>

#include "../../core/log.h"

#include "../../game/addresses.h"
#include "../../game/structs.h"

#include "../patch_silent_compat/qpc_timer.h"

namespace nfs5
{
    namespace
    {
        // Free space is only used for save checks, a short cache is safe.
        constexpr std::uint64_t DISK_SPACE_TTL_MS = 2000;
    }

    IoHooks & IoHooks::GetInstance()
    {
        static IoHooks instance;
        return instance;
    }

    namespace
    {
        std::string LowerGamePath( std::string value )
        {
            std::replace( value.begin(), value.end(), '/', '\\' );
            std::transform( value.begin(), value.end(), value.begin(), [] ( unsigned char c )
            {
                return static_cast<char>( std::tolower( c ) );
            } );
            return value;
        }

        bool IsGameplayAssetPath( std::string_view relative )
        {
            return relative.starts_with( "fedata\\" ) || relative.starts_with( "gamedata\\" );
        }
    }

    namespace
    {
        struct PendingGameplayOpen
        {
            std::string original;
            std::string replacement;
            bool redirected = false;
            bool failed = false;
        };

        thread_local PendingGameplayOpen * t_PendingGameplayOpen = nullptr;

        struct ScopedGameplayOpen
        {
            PendingGameplayOpen * previous = nullptr;
            explicit ScopedGameplayOpen( PendingGameplayOpen * pending ) noexcept
                : previous( t_PendingGameplayOpen )
            {
                t_PendingGameplayOpen = pending;
            }
            ~ScopedGameplayOpen() { t_PendingGameplayOpen = previous; }
            ScopedGameplayOpen( const ScopedGameplayOpen & ) = delete;
            ScopedGameplayOpen & operator=( const ScopedGameplayOpen & ) = delete;
        };

        // Canonical comparison for a single game's directory only; no filesystem edits.
        std::string RelativeGameOpenName( const char * name, const std::string & game_root )
        {
            if (name == nullptr)
                return {};
            std::string value = LowerGamePath( name );
            const std::string root = LowerGamePath( game_root );
            if (!root.empty() && value.starts_with( root ))
                value.erase( 0, root.size() );
            while (value.starts_with( ".\\" ))
                value.erase( 0, 2 );
            return value;
        }
    }

    HANDLE WINAPI IoHooks::CreateFileHook(
        LPCSTR name, DWORD desired_access, DWORD share_mode, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD attributes, HANDLE template_file )
    {
        IoHooks & self = GetInstance();
        const auto original = self.m_CreateFilePatch.GetOriginal< decltype(&::CreateFileA) >();
        auto * pending = t_PendingGameplayOpen;
        const DWORD write_access = GENERIC_ALL | GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA |
            FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | DELETE | WRITE_DAC | WRITE_OWNER;
        if (self.m_Settings.m_GameplayRedirect && name != nullptr && disposition == OPEN_EXISTING &&
            (desired_access & write_access) == 0 && (attributes & FILE_FLAG_BACKUP_SEMANTICS) == 0)
        {
            // v0.7: An external CreateFileA can occur outside FILE_LAYER_OPEN. Resolve
            // exactly the same allowlisted override for both entry paths. Retain the EA
            // original filename/handle bookkeeping inside FILE_LAYER_OPEN.
            const bool pending_match = pending != nullptr &&
                RelativeGameOpenName( name, self.m_GameRoot ) ==
                RelativeGameOpenName( pending->original.c_str(), self.m_GameRoot );
            const std::string candidate = pending_match ? pending->replacement :
                self.ResolveGameplayOverride( name );
            if (!candidate.empty())
            {
                HANDLE handle = original( candidate.c_str(), desired_access, share_mode,
                    security, disposition, attributes, template_file );
                if (handle != INVALID_HANDLE_VALUE)
                {
                    if (pending_match)
                        pending->redirected = true;
                    else if (self.m_Settings.m_GameplayRedirectTrace)
                        LOG_INFO( "Gameplay OS direct redirect opened: '{}' -> '{}'", name, candidate );
                    return handle;
                }
                if (pending_match)
                    pending->failed = true;
                else if (self.m_Settings.m_GameplayRedirectTrace)
                    LOG_WARN( "Gameplay OS direct redirect failed; using original: '{}'", name );
            }
        }
        return original( name, desired_access, share_mode, security, disposition, attributes, template_file );
    }

    DWORD WINAPI IoHooks::GetFileAttributesHook( LPCSTR name )
    {
        IoHooks & self = GetInstance();
        const auto original = self.m_AttributesPatch.GetOriginal< decltype(&::GetFileAttributesA) >();
        if (self.m_Settings.m_GameplayRedirect && name != nullptr)
        {
            const std::string candidate = self.ResolveGameplayOverride( name );
            if (!candidate.empty())
            {
                const DWORD attributes = original( candidate.c_str() );
                if (attributes != INVALID_FILE_ATTRIBUTES)
                {
                    if (self.m_Settings.m_GameplayRedirectTrace)
                        LOG_INFO( "Gameplay OS attributes redirected: '{}' -> '{}'", name, candidate );
                    return attributes;
                }
            }
        }
        return original( name );
    }

    std::string IoHooks::ResolveGameplayOverride( const char * file_name ) const
    {
        if (file_name == nullptr || m_OverrideRoot.empty() || m_GameRoot.empty())
            return {};

        std::string relative( file_name );
        std::replace( relative.begin(), relative.end(), '/', '\\' );
        std::string folded = LowerGamePath( relative );
        const std::string game_root_folded = LowerGamePath( m_GameRoot );

        // Accept absolute paths only when they are inside this game's directory.
        if (relative.size() >= 2 && relative[1] == ':')
        {
            if (!folded.starts_with( game_root_folded ))
                return {};
            relative.erase( 0, m_GameRoot.size() );
            folded.erase( 0, m_GameRoot.size() );
        }
        else if (!relative.empty() && relative[0] == '\\')
        {
            // No rooted paths or network shares.
            return {};
        }
        else
        {
            while (folded.starts_with( ".\\" ))
            {
                relative.erase( 0, 2 );
                folded.erase( 0, 2 );
            }
        }

        // Prevent traversal and redirection outside FEData / GameData.
        if (!IsGameplayAssetPath( folded ) || folded.find( ".." ) != std::string::npos ||
            folded.find( ':' ) != std::string::npos)
            return {};

        const std::string override_file = m_OverrideRoot + relative;
        const DWORD attributes = ::GetFileAttributesA( override_file.c_str() );
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return {};
        return override_file;
    }
    int __cdecl IoHooks::FileOpenHook(
        const char * file_name,
        std::uint32_t flags,
        std::uint32_t block_size,
        int * out_handle
    )
    {
        IoHooks & self = GetInstance();
        namespace ff = game::file_flag;

        // Unbuffered handles need sector sized transfers, those are left untouched.
        const bool read_only = (flags & ff::READ) != 0 && (flags & ff::WRITE) == 0 && (flags & ff::NO_BUFFERING) == 0;
        if (self.m_Settings.m_GameplayRedirectTrace && file_name != nullptr)
        {
            const std::string folded = LowerGamePath( file_name );
            if (folded.find( "nfs5.car" ) != std::string::npos ||
                folded.find( "nfs5.prt" ) != std::string::npos ||
                folded.find( "fedata" ) != std::string::npos ||
                folded.find( "gamedata" ) != std::string::npos)
            {
                LOG_INFO( "Gameplay file probe: name='{}' flags={:08X} read_only={}",
                    file_name, flags, read_only );
            }
        }

        const std::string override_file = read_only && self.m_Settings.m_GameplayRedirect &&
            self.m_CreateFilePatch.IsApplied() ? self.ResolveGameplayOverride( file_name ) : std::string{};
        PendingGameplayOpen pending{ file_name == nullptr ? "" : file_name, override_file };
        int result = 0;
        {
            ScopedGameplayOpen scope( override_file.empty() ? nullptr : &pending );
            // Crucially, pass the ORIGINAL request into the EA layer.
            result = self.m_OpenHook->ccall< int >( file_name, flags, block_size, out_handle );
        }
        if (pending.redirected && self.m_Settings.m_GameplayRedirectTrace)
            LOG_INFO( "Gameplay OS redirect opened: '{}' -> '{}'", file_name, override_file );
        else if (pending.failed)
            LOG_WARN( "Gameplay OS redirect failed; original CreateFileA was tried: '{}'", file_name );
        else if (!override_file.empty() && self.m_Settings.m_GameplayRedirectTrace)
            LOG_WARN( "Gameplay OS redirect not observed (CreateFileA bypassed?): '{}'", file_name );

        if (result == 0 || out_handle == nullptr || !read_only)
        {
            return result;
        }

        // Handles are the bitwise complement of the table index.
        const int index = ~*out_handle;
        const int count = hook::Ref<int>( game::var::FILE_TABLE_COUNT );
        auto * table = hook::Ref<game::FileEntry *>( game::var::FILE_TABLE );

        if (table == nullptr || index < 0 || index >= count)
        {
            return result;
        }

        // The block size only bounds the 'ReadFile' and 'memcpy' chunk in 'sub_591DF0',
        // the async reader queries it for the file size alone, raising it is safe.
        game::FileEntry & entry = table[index];
        const auto chunk = static_cast<std::uint32_t>( self.m_Settings.m_ReadChunkKB ) * 1024u;

        if (entry.m_InUse != 0 && entry.m_BlockSize < chunk)
        {
            entry.m_BlockSize = chunk;
        }

        return result;
    }


    int IoHooks::CachedCpuSpeed(
        safetyhook::InlineHook & hook,
        std::optional< int > & cache
    )
    {
        std::scoped_lock lock( m_CpuSpeedLock );

        if (cache.has_value())
        {
            return *cache;
        }

        const std::int64_t start = QpcNow();
        cache = hook.ccall<int>();

        LOG_INFO( "CPU speed measured once in {} ms, value {:08X} cached", (QpcNow() - start) * 1000 / QpcFrequency(),
            static_cast<std::uint32_t>(*cache) );

        return *cache;
    }


    int __cdecl IoHooks::CpuSpeedRdtscHook()
    {
        IoHooks & self = GetInstance();

        if (!self.m_CpuSpeedRdtsc.has_value())
        {
            return self.CachedCpuSpeed( *self.m_CpuSpeedRdtscHook, self.m_CpuSpeedRdtsc );
        }

        return *self.m_CpuSpeedRdtsc;
    }


    int __cdecl IoHooks::CpuSpeedLegacyHook()
    {
        IoHooks & self = GetInstance();
        return self.CachedCpuSpeed( *self.m_CpuSpeedLegacyHook, self.m_CpuSpeedLegacy );
    }


    BOOL WINAPI IoHooks::GetDiskFreeSpaceHook(
        LPCSTR root_path,
        LPDWORD sectors_per_cluster,
        LPDWORD bytes_per_sector,
        LPDWORD free_clusters,
        LPDWORD total_clusters
    )
    {
        IoHooks & self = GetInstance();
        auto original = self.m_DiskSpacePatch.GetOriginal< decltype(&::GetDiskFreeSpaceA) >();

        if (root_path == nullptr)
        {
            return original( root_path, sectors_per_cluster, bytes_per_sector, free_clusters, total_clusters );
        }

        std::string key( root_path );
        std::transform( key.begin(), key.end(), key.begin(), [] ( char ch )
        {
            return static_cast<char>(std::toupper( static_cast<unsigned char>(ch) ));
        } );

        const std::uint64_t now = ::GetTickCount64();

        DiskSpaceEntry entry;
        bool cached = false;
        {
            std::scoped_lock lock( self.m_DiskSpaceLock );
            const auto it = self.m_DiskSpaceCache.find( key );

            if (it != self.m_DiskSpaceCache.end() && now - it->second.m_Stamp < DISK_SPACE_TTL_MS)
            {
                entry = it->second;
                cached = true;
            }
        }

        if (!cached)
        {
            entry.m_Result = original( root_path, &entry.m_SectorsPerCluster, &entry.m_BytesPerSector, &entry.m_FreeClusters, &entry.m_TotalClusters );
            entry.m_LastError = ::GetLastError();
            entry.m_Stamp = now;

            std::scoped_lock lock( self.m_DiskSpaceLock );
            self.m_DiskSpaceCache[key] = entry;
        }

        if (sectors_per_cluster != nullptr)
        {
            *sectors_per_cluster = entry.m_SectorsPerCluster;
        }

        if (bytes_per_sector != nullptr)
        {
            *bytes_per_sector = entry.m_BytesPerSector;
        }

        if (free_clusters != nullptr)
        {
            *free_clusters = entry.m_FreeClusters;
        }

        if (total_clusters != nullptr)
        {
            *total_clusters = entry.m_TotalClusters;
        }

        ::SetLastError( entry.m_Result != FALSE ? 0 : entry.m_LastError );
        return entry.m_Result;
    }

    bool IoHooks::Install( const IoSettings & settings )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;
        m_GameRoot.clear();
        m_OverrideRoot.clear();
        if (settings.m_GameplayRedirect || settings.m_GameplayRedirectTrace)
        {
            std::array<char, MAX_PATH> exe_path = {};
            const DWORD length = ::GetModuleFileNameA( nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()) );
            if (length != 0 && length < exe_path.size())
            {
                std::string absolute( exe_path.data(), length );
                const auto slash = absolute.find_last_of( "\\/" );
                if (slash != std::string::npos)
                {
                    m_GameRoot = absolute.substr( 0, slash + 1 );
                    m_OverrideRoot = m_GameRoot + "GameplayFixes\\";
                    LOG_INFO( "Gameplay override configured: enabled={} trace={} root='{}'",
                        settings.m_GameplayRedirect, settings.m_GameplayRedirectTrace, m_OverrideRoot );
                }
            }
            if (m_GameRoot.empty())
                LOG_WARN( "Gameplay override path could not be initialized; redirect disabled" );
        }

        // Not an IO change, so it does not depend on the IO switch.
        if (settings.m_FastCpuSpeedCheck)
        {
            namespace fn = game::fn;

            // The result is a signed clock in Hz, any CPU above 2.1 GHz wraps it around and the
            // texture detail detection 'sub_44E370' then rates the machine below 235 MHz and loads
            // every texture at its lowest quality. The measurement is replaced by the constant the
            // Enhanced executable returns, 2130 MHz, the highest value that stays positive.
            m_CpuSpeedRdtsc = static_cast<int>(fn::CPU_SPEED_CONSTANT);
            m_CpuSpeedLegacy = static_cast<int>(fn::CPU_SPEED_CONSTANT);
            LOG_INFO( "CPU speed measurement skipped, reported as {:08X}", fn::CPU_SPEED_CONSTANT );

            m_CpuSpeedRdtscHook = hook::CreateInline( "CpuSpeedRdtsc", fn::CPU_SPEED_RDTSC.m_Address, fn::CPU_SPEED_RDTSC.m_Signature,
                reinterpret_cast<void *>(&CpuSpeedRdtscHook) );
            m_CpuSpeedLegacyHook = hook::CreateInline( "CpuSpeedLegacy", fn::CPU_SPEED_LEGACY.m_Address, fn::CPU_SPEED_LEGACY.m_Signature,
                reinterpret_cast<void *>(&CpuSpeedLegacyHook) );

            LOG_INFO( "CPU speed check hooks ok {}", m_CpuSpeedRdtscHook != nullptr && m_CpuSpeedLegacyHook != nullptr );
        }


        if (!settings.m_Enabled)
        {
            LOG_INFO( "IO subsystem disabled by config" );
            m_Installed = true;
            return true;
        }

        bool ok = true;
        if (settings.m_GameplayRedirect && !m_OverrideRoot.empty())
        {
            if (!m_CreateFilePatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "CreateFileA",
                reinterpret_cast<void *>(&CreateFileHook) ))
                LOG_WARN( "Gameplay OS redirect disabled: Porsche.exe CreateFileA import is not available" );
        }
        // Optional: Porsche.exe versions may not import GetFileAttributesA at all.
        // Never assume an IAT hook worked unless installation logs confirm it.
        if (settings.m_GameplayRedirect && !m_OverrideRoot.empty())
        {
            if (!m_AttributesPatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "GetFileAttributesA",
                reinterpret_cast<void *>(&GetFileAttributesHook) ))
                LOG_WARN( "Gameplay OS attribute redirect unavailable: Porsche.exe has no matching GetFileAttributesA import" );
            else
                LOG_INFO( "Gameplay OS attribute redirect installed for Porsche.exe" );
        }
        m_OpenHook = hook::CreateInline( "FileOpen", game::fn::FILE_LAYER_OPEN.m_Address, game::fn::FILE_LAYER_OPEN.m_Signature,
            reinterpret_cast<void *>(&FileOpenHook) );

        ok &= m_OpenHook != nullptr;

        if (settings.m_CacheDiskFreeSpace)
        {
            ok &= m_DiskSpacePatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "GetDiskFreeSpaceA",
                reinterpret_cast<void *>(&GetDiskFreeSpaceHook) );
        }

        m_Installed = true;

        LOG_INFO( "IO installed, read chunk {} KB, all hooks ok {}", settings.m_ReadChunkKB, ok );
        return ok;
    }

    void IoHooks::Uninstall()
    {
        m_CpuSpeedLegacyHook.reset();
        m_CpuSpeedRdtscHook.reset();
        m_DiskSpacePatch.Restore();
        m_OpenHook.reset();
        m_AttributesPatch.Restore();
        m_CreateFilePatch.Restore();
        m_Installed = false;
    }
}