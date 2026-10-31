#include "text_quality.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../core/log.h"
#include "../../game/addresses.h"
#include "../../game/thrash.h"

namespace nfs5
{
    namespace
    {
        using SetTextureFn = int( __stdcall * )(int);
        using TallocFn = int( __stdcall * )(int, int, int, int, int);
        using TupdateFn = int( __stdcall * )(int, const void *, const void *);
        using TfreeFn = int( __stdcall * )(int);
        using AboutFn = const std::uint8_t * (__stdcall *)();

        // 4 bit glyph sheets, EA texture format 1.
        constexpr std::int32_t FORMAT_PALETTIZED_4 = 1;

        // Index levels are stored as index times 17 so 15 maps to 255.
        constexpr float LEVEL_SCALE = 17.0f;
        constexpr float MAX_INDEX = 15.0f;

        // Border around a tile for the four tap filter.
        constexpr std::int32_t BORDER = 2;

        template <typename T>
        [[nodiscard]] T ReadField(
            const std::uint8_t * base,
            std::size_t offset
        )
        {
            T value;
            std::memcpy( &value, base + offset, sizeof( T ) );
            return value;
        }


        [[nodiscard]] std::uint32_t Checksum(
            const std::uint8_t * data,
            std::size_t size
        )
        {
            std::uint32_t hash = 2166136261u;

            for (std::size_t i = 0; i < size; ++i)
            {
                hash = (hash ^ data[i]) * 16777619u;
            }

            return hash;
        }


        // Catmull Rom weights for the four taps around a sample at fraction t.
        [[nodiscard]] std::array<float, 4> CatmullRom(
            float t
        )
        {
            const float t2 = t * t;
            const float t3 = t2 * t;

            return { (-t3 + 2.0f * t2 - t) * 0.5f,
                (3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f,
                (-3.0f * t3 + 4.0f * t2 + t) * 0.5f,
                (t3 - t2) * 0.5f };
        }


        [[nodiscard]] std::uint32_t LerpColor(
            std::uint32_t from,
            std::uint32_t to,
            float t
        )
        {
            std::uint32_t result = 0;

            for (int shift = 0; shift < 32; shift += 8)
            {
                const float a = static_cast<float>( (from >> shift) & 0xFF );
                const float b = static_cast<float>( (to >> shift) & 0xFF );
                const auto channel = static_cast<std::uint32_t>( std::lround( a + (b - a) * t ) );
                result |= std::min( channel, 255u ) << shift;
            }

            return result;
        }


        [[nodiscard]] std::uint16_t ToArgb4444(
            std::uint32_t color
        )
        {
            return static_cast<std::uint16_t>(((color >> 16) & 0xF000) | ((color >> 12) & 0x0F00) | ((color >> 8) & 0x00F0) |
                ((color >> 4) & 0x000F));
        }
    }

    TextQuality & TextQuality::GetInstance()
    {
        static TextQuality instance;
        return instance;
    }


    int __cdecl TextQuality::UploadHook(
        void * font
    )
    {
        TextQuality & self = GetInstance();

        const int result = self.m_UploadHook->ccall< int >( font );
        self.OnUpload( static_cast<const std::uint8_t *>(font) );
        return result;
    }


    int __cdecl TextQuality::DrawQuadsHook(
        int quad_count,
        game::ThrashVertex * vertices,
        void * indices
    )
    {
        TextQuality & self = GetInstance();

        // Screen pixels per glyph texel, UVs are still in texels at this point.
        float magnification = 0.0f;
        if (quad_count > 0 && vertices != nullptr)
        {
            const float du = std::fabs( vertices[1].m_U - vertices[0].m_U );
            const float dx = std::fabs( vertices[1].m_X - vertices[0].m_X );

            if (du > 0.0f)
            {
                magnification = dx / du;
            }
        }

        self.m_Magnification = magnification;
        const int result = self.m_DrawHook->ccall< int >( quad_count, vertices, indices );
        self.m_Magnification = 0.0f;
        return result;
    }


    int __cdecl TextQuality::SelectTileHook(
        void * texture
    )
    {
        TextQuality & self = GetInstance();

        using SelectFn = int( __cdecl * )(void *);
        const auto original = reinterpret_cast<SelectFn>(self.m_SelectPatch.GetOriginalTarget());
        const int result = original( texture );
        self.OnSelectTile( texture );
        return result;
    }

    int __cdecl TextQuality::FreeHook(
        game::FontTexture * texture
    )
    {
        TextQuality & self = GetInstance();

        if (const auto it = self.m_Sheets.find( texture ); it != self.m_Sheets.end())
        {
            self.ReleaseSheet( it->second, true );
            self.m_Sheets.erase( it );
        }

        return self.m_FreeHook->ccall< int >( texture );
    }

    int __stdcall TextQuality::TresetHook()
    {
        TextQuality & self = GetInstance();

        // The driver drops every texture, the handles are only forgotten.
        self.ReleaseAll( false );
        return self.m_OriginalTreset != nullptr ? self.m_OriginalTreset() : 0;
    }

    int __stdcall TextQuality::SetVideoModeHook(
        int width,
        int height,
        int bpp
    )
    {
        TextQuality & self = GetInstance();

        // Freed while the device still exists, the mode change recreates it.
        self.ReleaseAll( true );
        return self.m_OriginalSetVideoMode != nullptr ? self.m_OriginalSetVideoMode( width, height, bpp ) : 0;
    }

    void TextQuality::OnThrashDriverLoaded()
    {
        namespace text = game::text;
        auto & treset_slot = hook::Ref<TreSetFn>( text::THRASH_TRESET );

        if (treset_slot != nullptr && treset_slot != &TresetHook)
        {
            m_OriginalTreset = treset_slot;
            treset_slot = &TresetHook;
        }

        auto & mode_slot = hook::Ref<SetVideoModeFn>( text::THRASH_SETVIDEOMODE );

        if (mode_slot != nullptr && mode_slot != &SetVideoModeHook)
        {
            m_OriginalSetVideoMode = mode_slot;
            mode_slot = &SetVideoModeHook;
        }

        m_TextureFormat = 0;
        LOG_INFO( "Text 'THRASH_treset' and 'THRASH_setvideomode wrapped'" );
    }


    void TextQuality::OnUpload( const std::uint8_t * font )
    {
        namespace text = game::text;
        if (font == nullptr || ReadField< std::uintptr_t >( font, text::FONT_DRIVER ) != text::FONT_DRIVER_TABLE.Get())
            return;

        auto * texture = ReadField< game::FontTexture * >( font, text::FONT_TEXTURE );
        const auto * header = ReadField< const std::uint8_t * >( font, text::FONT_HEADER );
        const auto * shape = ReadField< const std::uint8_t * >( font, text::FONT_SHAPE );

        if (texture == nullptr || header == nullptr || shape == nullptr)
        {
            return;
        }

        const std::uint32_t flags = ReadField< std::uint32_t >( header, text::HEADER_FLAGS );
        const bool usable = texture->m_Format == FORMAT_PALETTIZED_4 && texture->m_TileCount > 0 &&
            texture->m_TileCount <= static_cast<std::int32_t>(MAX_TILES) && texture->m_TileSize >= 8 &&
            texture->m_TileSize <= 512 && texture->m_Width > 0 && texture->m_Width <= texture->m_TileSize &&
            texture->m_Height > 0 && (flags & text::FLAG_OUTLINE_PALETTE) == 0;

        if (!usable)
        {
            // Multi color outline palettes have no ordered index ramp to interpolate along.
            if (const auto it = m_Sheets.find( texture ); it != m_Sheets.end())
            {
                ReleaseSheet( it->second, true );
                m_Sheets.erase( it );
            }
            return;
        }

        const std::uint8_t * pixels = shape + 16;
        const std::size_t row_bytes = static_cast<std::size_t>(texture->m_Width + 1) / 2;
        const std::uint32_t checksum = Checksum( pixels, row_bytes * static_cast<std::size_t>(texture->m_Height) );

        Sheet & sheet = m_Sheets[texture];

        const bool same = sheet.m_Pixels == pixels && sheet.m_Width == texture->m_Width && sheet.m_Height == texture->m_Height &&
            sheet.m_TileSize == texture->m_TileSize && sheet.m_TileCount == texture->m_TileCount && sheet.m_Checksum == checksum;

        if (!same)
        {
            ReleaseSheet( sheet, true );
            sheet.m_Pixels = pixels;
            sheet.m_Width = texture->m_Width;
            sheet.m_Height = texture->m_Height;
            sheet.m_TileSize = texture->m_TileSize;
            sheet.m_TileCount = texture->m_TileCount;

            for (std::int32_t i = 0; i < texture->m_TileCount; ++i)
            {
                sheet.m_TileTops[i] = texture->m_Tiles[i].m_Top;
            }

            sheet.m_Checksum = checksum;
            sheet.m_Segments = (flags & text::FLAG_TWO_STAGE_PALETTE) != 0 ? 2 : 1;
            sheet.m_Levels.clear();
            sheet.m_Failed = false;
        }

        std::memcpy( sheet.m_Palette.data(), font + text::FONT_PALETTE, sizeof( Palette ) );
        sheet.m_HavePalette = true;
    }


    float TextQuality::DisplayFactor() const
    {
        // Menus run at 640x480 and DDrawCompat renders them at the display resolution,
        // races run at the display resolution themselves.
        const int screen_height = hook::Ref<int>( game::var::SCREEN_HEIGHT );

        if (screen_height <= 0 || m_DesktopHeight <= 0)
            return 1.0f;

        return std::max( 1.0f, static_cast<float>(m_DesktopHeight) / static_cast<float>(screen_height) );
    }


    void TextQuality::OnSelectTile(
        void * texture
    )
    {
        namespace text = game::text;

        if (m_Magnification <= 0.0f || texture == nullptr || hook::Ref<int>( text::DISPLAY_BPP ) <= 8)
        {
            return;
        }

        // Text drawn near its original size keeps the original sheet, a large sheet
        // would only be minified with point sampling.
        if (m_Magnification * DisplayFactor() < static_cast<float>(m_Upscale) * 0.5f)
            return;

        auto * font_texture = hook::Ref<game::FontTexture *>( text::CURRENT_FONT_TEXTURE );
        const auto it = m_Sheets.find( font_texture );

        if (it == m_Sheets.end())
            return;

        Sheet & sheet = it->second;

        if (!sheet.m_HavePalette || sheet.m_Failed)
            return;

        std::int32_t tile = -1;
        for (std::int32_t i = 0; i < sheet.m_TileCount; ++i)
        {
            if (font_texture->m_Textures[i] == texture)
            {
                tile = i;
                break;
            }
        }

        if (tile < 0)
        {
            return;
        }

        const ColorSet * set = FindOrCreateSet( sheet );

        if (set == nullptr || set->m_Handles[tile] == 0)
            return;

        const auto set_texture = hook::Ref<SetTextureFn>( text::THRASH_SETTEXTURE );

        if (set_texture != nullptr)
        {
            set_texture( set->m_Handles[tile] );
        }
    }


    bool TextQuality::PickTextureFormat()
    {
        namespace text = game::text;
        if (m_TextureFormat != 0)
            return m_TextureFormat > 0;

        m_TextureFormat = -1;
        const auto about = hook::Ref<AboutFn>( game::var::THRASH_ABOUT );
        const std::uint8_t * info = about != nullptr ? about() : nullptr;

        if (info == nullptr)
            return false;

        const int count = ReadField< int >( info, text::ABOUT_FORMAT_COUNT );
        const auto * formats = ReadField< const int * >( info, text::ABOUT_FORMATS );

        if (formats == nullptr)
            return false;

        for (const int format : { text::FORMAT_ARGB8888, text::FORMAT_ARGB4444 })
        {
            if (format <= count && formats[format] != 0)
            {
                m_TextureFormat = format;
                break;
            }
        }

        if (m_TextureFormat < 0)
        {
            LOG_WARN( "Text graphics driver offers no ARGB texture format, text stays at its original resolution" );
            return false;
        }

        LOG_INFO( "Text glyph sheets enlarged {}x as EA texture format {}", m_Upscale, m_TextureFormat );
        return true;
    }


    void TextQuality::BuildLevels(
        Sheet & sheet
    )
    {
        const std::int32_t factor = m_Upscale;
        const std::int32_t tile = sheet.m_TileSize;
        const std::int32_t size = tile * factor;
        const std::int32_t padded = tile + 2 * BORDER;
        const std::size_t row_bytes = static_cast<std::size_t>(sheet.m_Width + 1) / 2;

        // Output pixel o of a source texel samples at offset '( o + 0.5 ) / factor - 0.5'.
        std::vector< std::array< float, 4 > > weights( static_cast<std::size_t>(factor) );
        std::vector< std::int32_t > first_tap( static_cast<std::size_t>(factor) );

        for (std::int32_t o = 0; o < factor; ++o)
        {
            const float position = (static_cast<float>( o ) + 0.5f) / static_cast<float>( factor ) - 0.5f;
            const float base = std::floor( position );

            weights[o] = CatmullRom( position - base );
            first_tap[o] = static_cast<std::int32_t>( base ) - 1;
        }

        const float segment_width = MAX_INDEX / static_cast<float>( sheet.m_Segments );
        const float last_segment = static_cast<float>( sheet.m_Segments - 1 );

        std::vector< float > source( static_cast<std::size_t>(padded) * padded );
        std::vector< float > horizontal( static_cast<std::size_t>(padded) * size );
        sheet.m_Levels.assign( static_cast<std::size_t>(sheet.m_TileCount), {} );

        for (std::int32_t t = 0; t < sheet.m_TileCount; ++t)
        {
            const std::int32_t top = sheet.m_TileTops[t];

            for (std::int32_t y = 0; y < padded; ++y)
            {
                const std::int32_t sy = top + y - BORDER;

                for (std::int32_t x = 0; x < padded; ++x)
                {
                    const std::int32_t sx = x - BORDER;
                    float value = 0.0f;

                    if (sx >= 0 && sx < sheet.m_Width && sy >= 0 && sy < sheet.m_Height)
                    {
                        // High nibble is the left pixel.
                        const std::uint8_t packed = sheet.m_Pixels[static_cast<std::size_t>( sy ) * row_bytes + static_cast<std::size_t>( sx / 2 )];
                        value = static_cast<float>( (sx & 1) != 0 ? (packed & 0x0F) : (packed >> 4) );
                    }

                    source[static_cast<std::size_t>( y ) * padded + x] = value;
                }
            }

            for (std::int32_t y = 0; y < padded; ++y)
            {
                const float * row = &source[static_cast<std::size_t>( y ) * padded];
                float * out = &horizontal[static_cast<std::size_t>( y ) * size];

                for (std::int32_t x = 0; x < tile; ++x)
                {
                    for (std::int32_t o = 0; o < factor; ++o)
                    {
                        const std::int32_t tap = x + first_tap[o] + BORDER;
                        const auto & w = weights[o];

                        out[x * factor + o] = row[tap] * w[0] + row[tap + 1] * w[1] + row[tap + 2] * w[2] + row[tap + 3] * w[3];
                    }
                }
            }

            std::vector< std::uint8_t > & levels = sheet.m_Levels[t];
            levels.resize( static_cast<std::size_t>( size ) * size );

            for (std::int32_t y = 0; y < tile; ++y)
            {
                for (std::int32_t o = 0; o < factor; ++o)
                {
                    const std::int32_t tap = y + first_tap[o] + BORDER;
                    const auto & w = weights[o];
                    const float * r0 = &horizontal[static_cast<std::size_t>( tap ) * size];
                    const float * r1 = r0 + size;
                    const float * r2 = r1 + size;
                    const float * r3 = r2 + size;
                    std::uint8_t * out = &levels[static_cast<std::size_t>( y * factor + o ) * size];

                    for (std::int32_t x = 0; x < size; ++x)
                    {
                        float index = std::clamp( r0[x] * w[0] + r1[x] * w[1] + r2[x] * w[2] + r3[x] * w[3], 0.0f, MAX_INDEX );

                        // Steepen each ramp segment around its middle, the filtered
                        // antialiasing levels become a narrow smooth edge again.
                        const float segment = std::min( std::floor( index / segment_width ), last_segment );
                        const float local = (index - segment * segment_width) / segment_width;
                        const float sharpened = std::clamp( (local - 0.5f) * m_EdgeSlope + 0.5f, 0.0f, 1.0f );
                        index = (segment + sharpened) * segment_width;
                        out[x] = static_cast<std::uint8_t>( std::lround( index * LEVEL_SCALE ) );
                    }
                }
            }
        }
    }


    TextQuality::ColorSet * TextQuality::FindOrCreateSet(
        Sheet & sheet
    )
    {
        for (ColorSet & set : sheet.m_Sets)
        {
            if (set.m_Palette == sheet.m_Palette)
            {
                set.m_LastUse = ++m_UseCounter;
                return &set;
            }
        }

        if (!PickTextureFormat())
        {
            sheet.m_Failed = true;
            return nullptr;
        }

        if (sheet.m_Levels.empty())
        {
            BuildLevels( sheet );
        }

        if (sheet.m_Sets.size() >= MAX_COLOR_SETS)
        {
            const auto oldest = std::min_element( sheet.m_Sets.begin(), sheet.m_Sets.end(),
                [] ( const ColorSet & a, const ColorSet & b )
            {
                return a.m_LastUse < b.m_LastUse;
            } );

            FreeSet( *oldest );
            sheet.m_Sets.erase( oldest );
        }

        ColorSet & set = sheet.m_Sets.emplace_back();
        set.m_Palette = sheet.m_Palette;
        set.m_LastUse = ++m_UseCounter;

        if (!UploadSet( sheet, set ))
        {
            FreeSet( set );

            sheet.m_Sets.pop_back();
            sheet.m_Failed = true;
            LOG_WARN( "Text could not create a {}x{} texture, this font keeps its original resolution", sheet.m_TileSize * m_Upscale,
                sheet.m_TileSize * m_Upscale );

            return nullptr;
        }

        return &set;
    }


    bool TextQuality::UploadSet(
        Sheet & sheet,
        ColorSet & set
    )
    {
        namespace text = game::text;
        const auto talloc = hook::Ref<TallocFn>( text::THRASH_TALLOC );
        const auto tupdate = hook::Ref<TupdateFn>( text::THRASH_TUPDATE );

        if (talloc == nullptr || tupdate == nullptr)
            return false;

        // Palette entry 0 is the background and 15 the full glyph color, levels between
        // two entries blend them.
        std::array< std::uint32_t, 256 > lut = {};
        for (std::size_t level = 0; level < lut.size(); ++level)
        {
            const float index = static_cast<float>( level ) / LEVEL_SCALE;
            const std::size_t low = std::min< std::size_t >( static_cast<std::size_t>( index ), 14 );
            lut[level] = LerpColor( set.m_Palette[low], set.m_Palette[low + 1], std::min( index - static_cast<float>( low ), 1.0f ) );
        }

        const std::int32_t size = sheet.m_TileSize * m_Upscale;
        const std::size_t pixel_count = static_cast<std::size_t>( size ) * size;

        m_Staging.resize( pixel_count );

        for (std::int32_t t = 0; t < sheet.m_TileCount; ++t)
        {
            const int handle = talloc( size, size, m_TextureFormat, 0, 0 );

            if (handle == 0)
                return false;

            set.m_Handles[t] = handle;

            const std::vector< std::uint8_t > & levels = sheet.m_Levels[t];
            for (std::size_t i = 0; i < pixel_count; ++i)
            {
                m_Staging[i] = lut[levels[i]];
            }

            if (m_TextureFormat == text::FORMAT_ARGB4444)
            {
                // Packed in place, each 16 bit pixel lands at or before its 32 bit source.
                auto * packed = reinterpret_cast<std::uint16_t *>( m_Staging.data() );
                for (std::size_t i = 0; i < pixel_count; ++i)
                {
                    packed[i] = ToArgb4444( m_Staging[i] );
                }
            }

            tupdate( handle, m_Staging.data(), nullptr );
        }

        return true;
    }


    void TextQuality::FreeSet(
        ColorSet & set
    )
    {
        const auto tfree = hook::Ref<TfreeFn>( game::text::THRASH_TFREE );

        for (int & handle : set.m_Handles)
        {
            if (handle != 0 && tfree != nullptr)
            {
                tfree( handle );
            }

            handle = 0;
        }
    }


    void TextQuality::ReleaseSheet(
        Sheet & sheet,
        bool free_handles
    )
    {
        if (free_handles)
        {
            for (ColorSet & set : sheet.m_Sets)
            {
                FreeSet( set );
            }
        }

        sheet.m_Sets.clear();
    }


    void TextQuality::ReleaseAll(
        bool free_handles
    )
    {
        for (auto & entry : m_Sheets)
        {
            ReleaseSheet( entry.second, free_handles );
            entry.second.m_Failed = false;
        }
    }


    bool TextQuality::Install(
        const TextSettings & settings
    )
    {
        if (m_Installed)
        {
            return true;
        }

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "Text subsystem disabled by config" );
            return true;
        }

        DEVMODEW mode = {};
        mode.dmSize = sizeof( mode );

        if (::EnumDisplaySettingsW( nullptr, ENUM_CURRENT_SETTINGS, &mode ) != FALSE)
        {
            m_DesktopHeight = static_cast<std::int32_t>(mode.dmPelsHeight);
        }

        m_Upscale = settings.m_Upscale;

        if (m_Upscale == 0)
        {
            const float ratio = static_cast<float>(m_DesktopHeight) / 480.0f;
            m_Upscale = ratio >= 3.0f ? 4 : (ratio >= 1.5f ? 2 : 0);
        }

        if (m_Upscale < 2)
        {
            LOG_INFO( "Text display height {} is close to 480, glyph sheets stay at their original resolution", m_DesktopHeight );
            return true;
        }

        m_EdgeSlope = std::max( 1.0f, 0.6f * static_cast<float>( m_Upscale ) * settings.m_Sharpness );

        namespace text = game::text;
        if (!hook::MatchBytes( text::FONT_UPLOAD.m_Address, text::FONT_UPLOAD.m_Signature ) ||
            !hook::MatchBytes( text::FONT_DRAW_QUADS.m_Address, text::FONT_DRAW_QUADS.m_Signature ) ||
            !hook::MatchBytes( text::FONT_SELECT_TILE_CALL.m_Address, text::FONT_SELECT_TILE_CALL.m_Signature ) ||
            !hook::MatchBytes( text::FONT_TEXTURE_FREE.m_Address, text::FONT_TEXTURE_FREE.m_Signature ))
        {
            LOG_ERROR( "Text font sites do not match, text stays at its original resolution" );
            return false;
        }

        m_FreeHook = hook::CreateInline( "FontTextureFree", text::FONT_TEXTURE_FREE.m_Address, text::FONT_TEXTURE_FREE.m_Signature,
            reinterpret_cast<void *>(&FreeHook) );
        m_UploadHook = hook::CreateInline( "FontUpload", text::FONT_UPLOAD.m_Address, text::FONT_UPLOAD.m_Signature,
            reinterpret_cast<void *>(&UploadHook) );
        m_DrawHook = hook::CreateInline( "FontDrawQuads", text::FONT_DRAW_QUADS.m_Address, text::FONT_DRAW_QUADS.m_Signature,
            reinterpret_cast<void *>(&DrawQuadsHook) );

        if (m_FreeHook == nullptr || m_UploadHook == nullptr || m_DrawHook == nullptr ||
            !m_SelectPatch.Apply( "FontSelectTile", text::FONT_SELECT_TILE_CALL.m_Address, reinterpret_cast<void *>(&SelectTileHook) ))
        {
            LOG_ERROR( "Text hooks failed, text stays at its original resolution" );
            Uninstall();
            return false;
        }

        game::ThrashDriver::GetInstance().AddLoadListener( [this] ( HMODULE )
        {
            OnThrashDriverLoaded();
        } );

        m_Installed = true;
        LOG_INFO( "Text installed, glyph sheets {}x for display height {}, edge slope {:.2f}", m_Upscale, m_DesktopHeight, m_EdgeSlope );
        return true;
    }


    void TextQuality::Uninstall()
    {
        m_SelectPatch.Restore();
        m_DrawHook.reset();
        m_UploadHook.reset();
        m_FreeHook.reset();

        auto & treset_slot = hook::Ref<TreSetFn>( game::text::THRASH_TRESET );

        if (treset_slot == &TresetHook)
            treset_slot = m_OriginalTreset;

        auto & mode_slot = hook::Ref<SetVideoModeFn>( game::text::THRASH_SETVIDEOMODE );

        if (mode_slot == &SetVideoModeHook)
            mode_slot = m_OriginalSetVideoMode;

        m_Installed = false;
    }
}