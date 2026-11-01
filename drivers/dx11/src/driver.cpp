#include "driver.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "formats.h"
#include "shaders.h"

extern HMODULE g_Module;

namespace dx11z
{
    namespace
    {
        using CompileFn = HRESULT( WINAPI * )(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT,
            ID3DBlob **, ID3DBlob **);

        constexpr std::uint32_t ABOUT_SIGNATURE = 0x44334437;
        constexpr std::uint32_t DRIVER_VERSION = 0x6B;
        constexpr std::uint32_t TEXTURE_MEMORY = 512u * 1024u * 1024u;

        // Vendor class reported to the game, 0x10 is what dx7z returns for current cards.
        constexpr std::int32_t SUBTYPE = 0x10;
        constexpr std::int32_t DX_VERSION = 11;
        constexpr float DEPTH_BIAS_UNIT = 1.0f / 32768.0f;
        constexpr std::size_t RAW_VERTICES = 65536;
        constexpr std::size_t RAW_INDICES = RAW_VERTICES * 4;

        const char AUTHOR[] = "NFS5 Enhanced D3D11 THRASH driver by iArtorias";

        struct PresentConstants
        {
            float m_Rect[4];
            float m_Params[4];
            float m_Source[4];
        };

        struct ClearConstants
        {
            float m_Rect[4];
            float m_Target[4];
            float m_Color[4];
        };

        [[nodiscard]] float AsFloat(
            std::uint32_t bits
        )
        {
            float value;
            std::memcpy( &value, &bits, sizeof( value ) );
            return value;
        }

        [[nodiscard]] std::uint32_t AsBits(
            float value
        )
        {
            std::uint32_t bits;
            std::memcpy( &bits, &value, sizeof( bits ) );
            return bits;
        }

        void ColorToFloat4(
            std::uint32_t color,
            float * out
        )
        {
            out[0] = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
            out[1] = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
            out[2] = static_cast<float>(color & 0xFF) / 255.0f;
            out[3] = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        }

        // D3DBLEND to D3D11, the alpha channel version maps colour factors to alpha ones.
        [[nodiscard]] D3D11_BLEND ToBlend(
            std::uint32_t blend,
            bool alpha
        )
        {
            switch (blend)
            {
                case 1:
                    return D3D11_BLEND_ZERO;

                case 2:
                    return D3D11_BLEND_ONE;

                case 3:
                    return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;

                case 4:
                    return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;

                case 5:
                    return D3D11_BLEND_SRC_ALPHA;

                case 6:
                    return D3D11_BLEND_INV_SRC_ALPHA;

                case 7:
                    return D3D11_BLEND_DEST_ALPHA;

                case 8:
                    return D3D11_BLEND_INV_DEST_ALPHA;

                case 9:
                    return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;

                case 10:
                    return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;

                case 11:
                    return D3D11_BLEND_SRC_ALPHA_SAT;

                default:
                    return D3D11_BLEND_ONE;
            }
        }

        [[nodiscard]] D3D11_TEXTURE_ADDRESS_MODE ToAddress(
            std::uint32_t mode
        )
        {
            switch (mode)
            {
                case 2:
                    return D3D11_TEXTURE_ADDRESS_MIRROR;

                case 3:
                    return D3D11_TEXTURE_ADDRESS_CLAMP;
                case 4:
                    return D3D11_TEXTURE_ADDRESS_BORDER;

                default:
                    return D3D11_TEXTURE_ADDRESS_WRAP;
            }
        }

        [[nodiscard]] D3D11_COMPARISON_FUNC ToCompare(
            std::uint32_t func
        )
        {
            if (func < 1 || func > 8)
            {
                return D3D11_COMPARISON_ALWAYS;
            }

            return static_cast<D3D11_COMPARISON_FUNC>(func);
        }

        [[nodiscard]] D3D11_STENCIL_OP ToStencilOp(
            std::uint32_t op
        )
        {
            if (op < 1 || op > 8)
            {
                return D3D11_STENCIL_OP_KEEP;
            }

            return static_cast<D3D11_STENCIL_OP>(op);
        }
    }

    namespace
    {
        void ToRgb565(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            std::uint8_t * destination,
            std::uint32_t destination_pitch,
            int width,
            int height
        )
        {
            for (int y = 0; y < height; ++y)
            {
                const auto * in = reinterpret_cast<const std::uint32_t *>( source + static_cast<std::size_t>( y ) * source_pitch );
                auto * out = reinterpret_cast<std::uint16_t *>( destination + static_cast<std::size_t>( y ) * destination_pitch );
                for (int x = 0; x < width; ++x)
                {
                    const std::uint32_t c = in[x];
                    out[x] = static_cast<std::uint16_t>( ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F) );
                }
            }
        }

        // Frames of the movie player are packed 4:2:2 with 7 bit components, two pixels per 32 bit
        // word: byte 0 is V, byte 1 is U, byte 2 is Y of the second pixel and byte 3 is Y of the
        // first. This is the conversion of the game ['sub_56AD70', 'sub_592610'] with a full range
        // result: the ITU-R BT.601 factors, biases per channel and 2 * field - bias clamped.
        // The game repeats the color of a pair for both pixels. The color sits between the two
        // pixels of a pair, so each pixel takes three quarters of its own pair and one quarter of
        // the neighbor on its side, which removes the color blocks at edges.
        void FromMovieYuv(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            std::uint8_t * destination,
            std::uint32_t destination_pitch,
            int width,
            int height
        )
        {
            auto channel = [] ( int field, int bias )
            {
                return static_cast<std::uint32_t>(std::clamp( 2 * (field & 0x1FF) - bias, 0, 255 ));
            };

            const int pairs = width / 2;

            for (int y = 0; y < height; ++y)
            {
                const std::uint8_t * row = source + static_cast<std::size_t>( y ) * source_pitch;
                auto * out = reinterpret_cast<std::uint32_t *>( destination + static_cast<std::size_t>( y ) * destination_pitch );

                for (int pair = 0; pair < pairs; ++pair)
                {
                    const std::uint8_t * in = row + pair * 4;
                    const std::uint8_t * before = row + std::max( pair - 1, 0 ) * 4;
                    const std::uint8_t * after = row + std::min( pair + 1, pairs - 1 ) * 4;
                    const int luma[2] = { in[3] & 0x7F, in[2] & 0x7F };
                    const std::uint8_t * neighbor[2] = { before, after };

                    for (int k = 0; k < 2; ++k)
                    {
                        const float v = 0.75f * static_cast<float>(in[0] & 0x7F) + 0.25f * static_cast<float>(neighbor[k][0] & 0x7F);
                        const float u = 0.75f * static_cast<float>(in[1] & 0x7F) + 0.25f * static_cast<float>(neighbor[k][1] & 0x7F);
                        const float fv = v - 64.0f + 0.5f;
                        const float fu = u - 64.0f + 0.5f;

                        const std::uint32_t r = channel( 89 + luma[k] + static_cast<int>(std::lrintf( fv * 1.402f )), 177 );
                        const std::uint32_t g = channel( 67 + luma[k] + static_cast<int>(std::lrintf( fu * -0.3441f )) + static_cast<int>(std::lrintf( fv * -0.7141f )), 133 );
                        const std::uint32_t b = channel( 113 + luma[k] + static_cast<int>(std::lrintf( fu * 1.772f )), 225 );
                        out[pair * 2 + k] = 0xFF000000u | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }

        // Counts the rows at the top and at the bottom of a movie frame that are black from edge
        // to edge, the letterbox some movies carry inside the frame. Returns 'false' for a frame
        // that is black all over, which says nothing about the bars.
        bool FindMovieBars(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            int width,
            int height,
            int & top,
            int & bottom
        )
        {
            constexpr int BLACK = 3;

            auto black = [&] ( int y )
            {
                const std::uint8_t * in = source + static_cast<std::size_t>( y ) * source_pitch;

                for (int x = 0; x + 1 < width; x += 2, in += 4)
                {
                    if ((in[2] & 0x7F) > BLACK || (in[3] & 0x7F) > BLACK)
                        return false;
                }

                return true;
            };

            top = 0;
            bottom = 0;

            while (top < height && black( top ))
                ++top;

            if (top == height)
                return false;

            while (bottom < height - top && black( height - 1 - bottom ))
                ++bottom;

            return true;
        }

        void FromRgb565(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            std::uint8_t * destination,
            std::uint32_t destination_pitch,
            int width,
            int height
        )
        {
            for (int y = 0; y < height; ++y)
            {
                const auto * in = reinterpret_cast<const std::uint16_t *>( source + static_cast<std::size_t>( y ) * source_pitch );
                auto * out = reinterpret_cast<std::uint32_t *>( destination + static_cast<std::size_t>( y ) * destination_pitch );
                for (int x = 0; x < width; ++x)
                {
                    const std::uint32_t v = in[x];
                    const std::uint32_t r = (v >> 11) & 31;
                    const std::uint32_t g = (v >> 5) & 63;
                    const std::uint32_t b = v & 31;
                    out[x] = 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
                }
            }
        }
    }

    namespace
    {
        using SetForegroundWindowFn = BOOL( WINAPI * )(HWND);
        using SetActiveWindowFn = HWND( WINAPI * )(HWND);

        SetForegroundWindowFn g_SetForegroundWindow = nullptr;
        SetActiveWindowFn g_SetActiveWindow = nullptr;

        // Replaces one import of a module, returns the previous target.
        void * PatchImport(
            HMODULE module,
            const char * dll,
            const char * name,
            void * replacement
        )
        {
            auto * base = reinterpret_cast<std::uint8_t *>(module);
            const auto * dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
            const auto * nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
            const IMAGE_DATA_DIRECTORY & directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

            if (directory.VirtualAddress == 0)
            {
                return nullptr;
            }

            for (auto * entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress); entry->Name != 0; ++entry)
            {
                if (_stricmp( reinterpret_cast<const char *>(base + entry->Name), dll ) != 0 || entry->OriginalFirstThunk == 0)
                    continue;

                auto * names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + entry->OriginalFirstThunk);
                auto * slots = reinterpret_cast<IMAGE_THUNK_DATA *>(base + entry->FirstThunk);

                for (; names->u1.AddressOfData != 0; ++names, ++slots)
                {
                    if (IMAGE_SNAP_BY_ORDINAL( names->u1.Ordinal ))
                        continue;

                    const auto * by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);

                    if (std::strcmp( reinterpret_cast<const char *>(by_name->Name), name ) != 0)
                        continue;

                    DWORD protection = 0;
                    ::VirtualProtect( &slots->u1.Function, sizeof( slots->u1.Function ), PAGE_READWRITE, &protection );
                    void * previous = reinterpret_cast<void *>(slots->u1.Function);
                    slots->u1.Function = reinterpret_cast<std::uintptr_t>(replacement);
                    ::VirtualProtect( &slots->u1.Function, sizeof( slots->u1.Function ), protection, &protection );
                    return previous;
                }
            }

            return nullptr;
        }
    }

    Driver & Driver::GetInstance()
    {
        static Driver * instance = new Driver();
        return *instance;
    }

    void Driver::Log(
        const char * format,
        ...
    )
    {
        if (m_LogPath.empty())
        {
            char path[MAX_PATH] = {};
            ::GetModuleFileNameA( g_Module, path, MAX_PATH );

            m_LogPath = path;

            const std::size_t dot = m_LogPath.find_last_of( '.' );

            if (dot != std::string::npos)
            {
                m_LogPath.resize( dot );
            }

            m_LogPath += ".log";

            if (FILE * file = std::fopen( m_LogPath.c_str(), "w" ))
            {
                std::fclose( file );
            }
        }

        char buffer[1024];
        va_list args;
        va_start( args, format );
        std::vsnprintf( buffer, sizeof( buffer ), format, args );
        va_end( args );

        if (FILE * file = std::fopen( m_LogPath.c_str(), "a" ))
        {
            std::fprintf( file, "%s\n", buffer );
            std::fclose( file );
        }
    }


    void Driver::Trace(
        const char * format,
        ...
    )
    {
        if (m_TraceLines <= 0)
            return;

        --m_TraceLines;
        char buffer[512];
        va_list args;
        va_start( args, format );
        std::vsnprintf( buffer, sizeof( buffer ), format, args );
        va_end( args );
        Log( "[%u] %s", m_FlipCount, buffer );
    }

    // Device and resources.

    bool Driver::CreateDevice()
    {
        if (m_Device)
            return true;

        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL obtained = {};
        HRESULT hr = ::D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
            static_cast<UINT>(std::size( levels )), D3D11_SDK_VERSION, m_Device.Put(), &obtained, m_Context.Put() );

        if (FAILED( hr ))
        {
            Log( "D3D11CreateDevice failed %08lX", static_cast<unsigned long>(hr) );
            return false;
        }

        Log( "D3D11 device created, feature level %04X", static_cast<unsigned>(obtained) );

        if (!CreateShaders())
            return false;

        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        desc.ByteWidth = static_cast<UINT>(RAW_VERTICES * sizeof( TlVertex ));
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        m_Device->CreateBuffer( &desc, nullptr, m_VertexBuffer.Put() );

        desc.ByteWidth = static_cast<UINT>(RAW_INDICES * sizeof( std::uint16_t ));
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        m_Device->CreateBuffer( &desc, nullptr, m_IndexBuffer.Put() );

        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth = sizeof( DrawConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_DrawConstants.Put() );
        desc.ByteWidth = sizeof( PresentConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_PresentConstants.Put() );
        desc.ByteWidth = sizeof( ClearConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_ClearConstants.Put() );

        D3D11_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        m_Device->CreateSamplerState( &sampler, m_PresentSampler.Put() );
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        m_Device->CreateSamplerState( &sampler, m_PointSampler.Put() );

        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        m_Device->CreateBlendState( &blend, m_OpaqueBlend.Put() );
        m_Device->CreateBlendState( &blend, m_ClearColorBlend.Put() );
        blend.RenderTarget[0].RenderTargetWriteMask = 0;
        m_Device->CreateBlendState( &blend, m_ClearDepthOnlyBlend.Put() );

        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        m_Device->CreateRasterizerState( &raster, m_PlainRaster.Put() );

        D3D11_DEPTH_STENCIL_DESC depth = {};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depth.StencilEnable = TRUE;
        depth.StencilReadMask = 0xFF;
        depth.StencilWriteMask = 0xFF;
        depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
        depth.FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
        depth.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
        depth.BackFace = depth.FrontFace;
        m_Device->CreateDepthStencilState( &depth, m_ClearDepth.Put() );

        D3D11_DEPTH_STENCIL_DESC color_only = {};
        m_Device->CreateDepthStencilState( &color_only, m_ClearColorOnly.Put() );

        m_BatchVertices.reserve( BATCH_VERTICES );
        m_BatchIndices.reserve( BATCH_INDICES );

        // Full, so the first batch maps the new buffers with a discard.
        m_VertexCursor = RAW_VERTICES;
        m_IndexCursor = RAW_INDICES;
        InvalidatePipeline();
        return true;
    }


    bool Driver::CreateShaders()
    {
        m_Compiler = ::LoadLibraryA( "d3dcompiler_47.dll" );

        if (m_Compiler == nullptr)
        {
            Log( "d3dcompiler_47.dll not found" );
            return false;
        }

        const auto compile = reinterpret_cast<CompileFn>(reinterpret_cast<void *>(::GetProcAddress( m_Compiler, "D3DCompile" )));

        if (compile == nullptr)
        {
            return false;
        }

        auto build = [&] ( const char * source, std::size_t length, const char * entry, const char * profile, ComPtr<ID3DBlob> & blob )
        {
            ComPtr<ID3DBlob> errors;
            const HRESULT hr = compile( source, length, nullptr, nullptr, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.Put(),
                errors.Put() );

            if (FAILED( hr ))
            {
                Log( "Shader %s failed: %s", entry, errors ? static_cast<const char *>(errors->GetBufferPointer()) : "?" );
                return false;
            }

            return true;
        };

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;

        if (!build( shaders::SCENE, sizeof( shaders::SCENE ) - 1, "MainVS", "vs_4_0", vs ) ||
            !build( shaders::SCENE, sizeof( shaders::SCENE ) - 1, "MainPS", "ps_4_0", ps ))
        {
            return false;
        }

        m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_SceneVs.Put() );
        m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ScenePs.Put() );

        const D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        m_Device->CreateInputLayout( layout, static_cast<UINT>(std::size( layout )), vs->GetBufferPointer(), vs->GetBufferSize(), m_SceneLayout.Put() );

        if (!build( shaders::PRESENT, sizeof( shaders::PRESENT ) - 1, "MainVS", "vs_4_0", vs ) ||
            !build( shaders::PRESENT, sizeof( shaders::PRESENT ) - 1, "MainPS", "ps_4_0", ps ))
        {
            return false;
        }

        m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_PresentVs.Put() );
        m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_PresentPs.Put() );

        // Movies fall back to the game's own scaling when this one does not build.
        if (build( shaders::MOVIE, sizeof( shaders::MOVIE ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_MoviePs.Put() );
        }

        if (!build( shaders::CLEAR, sizeof( shaders::CLEAR ) - 1, "MainVS", "vs_4_0", vs ) ||
            !build( shaders::CLEAR, sizeof( shaders::CLEAR ) - 1, "MainPS", "ps_4_0", ps ))
        {
            return false;
        }

        m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_ClearVs.Put() );
        m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ClearPs.Put() );
        return m_SceneVs && m_ScenePs && m_SceneLayout && m_PresentVs && m_PresentPs && m_ClearVs && m_ClearPs;
    }


    void Driver::FitWindow()
    {
        if (m_Window == nullptr && m_Callbacks[1] != 0)
            m_Window = reinterpret_cast<HWND( * )()>(m_Callbacks[1])();

        if (m_Window == nullptr)
            return;

        SubclassWindow();

        MONITORINFO info = {};
        info.cbSize = sizeof( info );
        ::GetMonitorInfoA( ::MonitorFromWindow( m_Window, MONITOR_DEFAULTTOPRIMARY ), &info );

        const RECT & area = info.rcMonitor;
        const RECT & work = info.rcWork;

        using GetDpiFn = UINT( WINAPI * )(HWND);
        const auto get_dpi = reinterpret_cast<GetDpiFn>(reinterpret_cast<void *>(::GetProcAddress( ::GetModuleHandleA( "user32.dll" ), "GetDpiForWindow" )));

        Log( "Monitor %ldx%ld, window DPI %u, mode %d", area.right - area.left, area.bottom - area.top, get_dpi != nullptr ? get_dpi( m_Window ) : 0u,
            m_ConfigWindowMode );

        if (m_ConfigWindowMode == WINDOW_WINDOWED)
        {
            // Placed once, later mode changes keep the size and position the player chose.
            if (m_WindowPlaced)
            {
                RECT client = {};
                ::GetClientRect( m_Window, &client );

                m_BackBufferWidth = client.right - client.left;
                m_BackBufferHeight = client.bottom - client.top;
                return;
            }

            m_WindowPlaced = true;

            const int work_width = work.right - work.left;
            const int work_height = work.bottom - work.top;
            int width = m_ConfigWindowWidth;
            int height = m_ConfigWindowHeight;

            if (width <= 0 || height <= 0)
            {
                // Three quarters of the work area with the monitor aspect ratio.
                width = work_width * 3 / 4;
                height = width * (area.bottom - area.top) / std::max<LONG>( area.right - area.left, 1 );

                if (height > work_height * 3 / 4)
                {
                    height = work_height * 3 / 4;
                    width = height * (area.right - area.left) / std::max<LONG>( area.bottom - area.top, 1 );
                }
            }

            const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            const DWORD ex_style = static_cast<DWORD>(::GetWindowLongPtrA( m_Window, GWL_EXSTYLE )) & ~static_cast<DWORD>(WS_EX_TOPMOST);
            RECT rect = { 0, 0, width, height };
            ::AdjustWindowRectEx( &rect, style, FALSE, ex_style );
            const int outer_width = rect.right - rect.left;
            const int outer_height = rect.bottom - rect.top;
            const int x = work.left + std::max( (work_width - outer_width) / 2, 0 );
            const int y = work.top + std::max( (work_height - outer_height) / 2, 0 );

            ::SetWindowLongPtrA( m_Window, GWL_STYLE, static_cast<LONG_PTR>(style) );
            ::SetWindowLongPtrA( m_Window, GWL_EXSTYLE, static_cast<LONG_PTR>(ex_style) );
            ::SetWindowPos( m_Window, HWND_NOTOPMOST, x, y, outer_width, outer_height, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS );

            m_BackBufferWidth = width;
            m_BackBufferHeight = height;
            return;
        }

        // Borderless and exclusive fullscreen cover the whole monitor, the frame is scaled into it.
        ::SetWindowLongPtrA( m_Window, GWL_STYLE, WS_POPUP | WS_VISIBLE );
        ::SetWindowPos( m_Window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS );
        m_BackBufferWidth = area.right - area.left;
        m_BackBufferHeight = area.bottom - area.top;
    }


    void Driver::SubclassWindow()
    {
        if (m_Window == nullptr || m_SubclassedWindow == m_Window)
            return;

        RestoreWindowProc();
        PatchFocusImports();
        m_OriginalWndProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrA( m_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc) ));
        m_SubclassedWindow = m_OriginalWndProc != nullptr ? m_Window : nullptr;
    }


    void Driver::PatchFocusImports()
    {
        if (m_FocusPatched)
            return;

        m_FocusPatched = true;

        const HMODULE game = ::GetModuleHandleA( nullptr );
        g_SetForegroundWindow = reinterpret_cast<SetForegroundWindowFn>(
            PatchImport( game, "USER32.dll", "SetForegroundWindow", reinterpret_cast<void *>(&SetForegroundWindowFilter) ));
        g_SetActiveWindow = reinterpret_cast<SetActiveWindowFn>(
            PatchImport( game, "USER32.dll", "SetActiveWindow", reinterpret_cast<void *>(&SetActiveWindowFilter) ));

        Log( "Focus imports patched %d %d", g_SetForegroundWindow != nullptr ? 1 : 0, g_SetActiveWindow != nullptr ? 1 : 0 );
    }


    BOOL WINAPI Driver::SetForegroundWindowFilter(
        HWND window
    )
    {
        Driver & self = GetInstance();

        if (self.m_Inactive && window == self.m_Window)
        {
            self.Trace( "'SetForegroundWindow' from the game ignored" );
            return TRUE;
        }

        return g_SetForegroundWindow != nullptr ? g_SetForegroundWindow( window ) : ::SetForegroundWindow( window );
    }


    HWND WINAPI Driver::SetActiveWindowFilter(
        HWND window
    )
    {
        Driver & self = GetInstance();

        if (self.m_Inactive && window == self.m_Window)
        {
            self.Trace( "'SetActiveWindow' from the game ignored" );
            return nullptr;
        }

        return g_SetActiveWindow != nullptr ? g_SetActiveWindow( window ) : ::SetActiveWindow( window );
    }


    void Driver::RestoreWindowProc()
    {
        if (m_SubclassedWindow != nullptr && m_OriginalWndProc != nullptr && ::IsWindow( m_SubclassedWindow ))
        {
            ::SetWindowLongPtrA( m_SubclassedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_OriginalWndProc) );
        }

        m_SubclassedWindow = nullptr;
        m_OriginalWndProc = nullptr;
    }


    LRESULT CALLBACK Driver::WindowProc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam
    )
    {
        Driver & self = GetInstance();

        const WNDPROC original = self.m_OriginalWndProc;
        const LRESULT result = original != nullptr ? ::CallWindowProcA( original, window, message, wparam, lparam )
            : ::DefWindowProcA( window, message, wparam, lparam );

        if (message == WM_ACTIVATEAPP)
        {
            self.Trace( "WM_ACTIVATEAPP %u iconic %d", static_cast<unsigned>(wparam), ::IsIconic( window ) ? 1 : 0 );
            self.m_Inactive = wparam == FALSE;

            if (wparam != FALSE)
            {
                self.m_ReenterFullscreen = self.m_ConfigWindowMode == WINDOW_FULLSCREEN;
            }

            // The full screen modes behave like the exclusive mode of the original driver on
            // alt tab, the window gets out of the way and comes back when activated again.
            if (self.m_ConfigMinimize && self.m_ConfigWindowMode != WINDOW_WINDOWED)
            {
                if (wparam == FALSE)
                {
                    ::ShowWindow( window, SW_SHOWMINNOACTIVE );
                }
                else if (::IsIconic( window ))
                {
                    ::ShowWindow( window, SW_RESTORE );
                }
            }
        }

        return result;
    }


    void Driver::EnterFullscreen()
    {
        if (!m_SwapChain || m_ConfigWindowMode != WINDOW_FULLSCREEN)
            return;

        BOOL fullscreen = FALSE;
        m_SwapChain->GetFullscreenState( &fullscreen, nullptr );

        if (fullscreen)
            return;

        // The output the window is on, at its current desktop mode.
        ComPtr<IDXGIOutput> output;
        m_SwapChain->GetContainingOutput( output.Put() );
        DXGI_OUTPUT_DESC output_desc = {};

        if (output)
        {
            output->GetDesc( &output_desc );
        }

        const int width = output ? output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left : m_BackBufferWidth;
        const int height = output ? output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top : m_BackBufferHeight;

        DXGI_MODE_DESC mode = {};
        mode.Width = static_cast<UINT>(width);
        mode.Height = static_cast<UINT>(height);
        mode.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

        const HRESULT hr = m_SwapChain->SetFullscreenState( TRUE, output.Get() );

        if (FAILED( hr ))
        {
            // Not possible while another program is active, tried again on a later page flip.
            Trace( "SetFullscreenState failed %08lX", static_cast<unsigned long>(hr) );
            m_ReenterFullscreen = true;
            return;
        }
        m_SwapChain->ResizeTarget( &mode );
        m_BackBufferWidth = width;
        m_BackBufferHeight = height;
        CreateSwapChain();

        Log( "Exclusive fullscreen %dx%d, back buffer view %d", width, height, m_BackBufferView ? 1 : 0 );
    }


    bool Driver::CreateSwapChain()
    {
        if (m_Window == nullptr && m_Callbacks[1] != 0)
        {
            m_Window = reinterpret_cast<HWND( * )()>(m_Callbacks[1])();
        }

        if (m_Window == nullptr)
        {
            Log( "No window for the swap chain" );
            return false;
        }

        if (m_BackBufferWidth <= 0 || m_BackBufferHeight <= 0)
        {
            RECT client = {};
            ::GetClientRect( m_Window, &client );
            m_BackBufferWidth = std::max<LONG>( client.right - client.left, 1 );
            m_BackBufferHeight = std::max<LONG>( client.bottom - client.top, 1 );
        }

        if (m_SwapChain)
        {
            m_BackBufferView.Reset();
            m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
            InvalidatePipeline();
            m_SwapChain->ResizeBuffers( 0, static_cast<UINT>(m_BackBufferWidth), static_cast<UINT>(m_BackBufferHeight), DXGI_FORMAT_UNKNOWN, m_SwapChainFlags );
        }
        else
        {
            ComPtr<IDXGIDevice> dxgi_device;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            m_Device->QueryInterface( __uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgi_device.Put()) );

            if (dxgi_device)
            {
                dxgi_device->GetAdapter( adapter.Put() );
            }

            if (adapter)
            {
                adapter->GetParent( __uuidof(IDXGIFactory2), reinterpret_cast<void **>(factory.Put()) );
            }

            if (!factory)
            {
                Log( "IDXGIFactory2 not available" );
                return false;
            }

            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width = static_cast<UINT>(m_BackBufferWidth);
            desc.Height = static_cast<UINT>(m_BackBufferHeight);
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

            // Without vsync the flip model needs tearing support to show frames at once.
            m_AllowTearing = false;

            if (!m_ConfigVsync)
            {
                ComPtr<IDXGIFactory5> factory5;
                factory->QueryInterface( __uuidof(IDXGIFactory5), reinterpret_cast<void **>(factory5.Put()) );
                BOOL tearing = FALSE;

                if (factory5 && SUCCEEDED( factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof( tearing ) ) ))
                {
                    m_AllowTearing = tearing != FALSE;
                }
            }

            m_SwapChainFlags = m_AllowTearing ? static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) : 0u;
            desc.Flags = m_SwapChainFlags;

            HRESULT hr = factory->CreateSwapChainForHwnd( m_Device.Get(), m_Window, &desc, nullptr, nullptr, m_SwapChain.Put() );
            if (FAILED( hr ))
            {
                // Windows 8.1 and older have no flip discard.
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                hr = factory->CreateSwapChainForHwnd( m_Device.Get(), m_Window, &desc, nullptr, nullptr, m_SwapChain.Put() );
            }
            if (FAILED( hr ))
            {
                Log( "CreateSwapChainForHwnd failed %08lX", static_cast<unsigned long>(hr) );
                return false;
            }

            factory->MakeWindowAssociation( m_Window, DXGI_MWA_NO_ALT_ENTER );
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        m_SwapChain->GetBuffer( 0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(back_buffer.Put()) );
        m_Device->CreateRenderTargetView( back_buffer.Get(), nullptr, m_BackBufferView.Put() );

        Log( "Swap chain %dx%d", m_BackBufferWidth, m_BackBufferHeight );

        return static_cast<bool>(m_BackBufferView);
    }


    bool Driver::CreateFrame(
        int width,
        int height
    )
    {
        ReleaseFrame();

        m_Width = width;
        m_Height = height;

        ComputeRenderSize();

        // Highest supported sample count up to the configured one.
        m_Samples = 1;
        for (UINT samples = static_cast<UINT>(std::clamp( m_ConfigSamples, 1, 8 )); samples > 1; samples >>= 1)
        {
            UINT color_levels = 0;
            UINT depth_levels = 0;
            m_Device->CheckMultisampleQualityLevels( DXGI_FORMAT_B8G8R8A8_UNORM, samples, &color_levels );
            m_Device->CheckMultisampleQualityLevels( DXGI_FORMAT_D24_UNORM_S8_UINT, samples, &depth_levels );

            if (color_levels > 0 && depth_levels > 0)
            {
                m_Samples = samples;
                break;
            }
        }

        m_Resolve = m_Samples > 1;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(m_RenderWidth);
        desc.Height = static_cast<UINT>(m_RenderHeight);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = m_Samples;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | (m_Resolve ? 0u : static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE));

        if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_Frame.Put() ) ))
        {
            Log( "Frame %dx%d with %u samples failed", m_RenderWidth, m_RenderHeight, m_Samples );
            return false;
        }

        m_Device->CreateRenderTargetView( m_Frame.Get(), nullptr, m_FrameView.Put() );

        if (m_Resolve)
        {
            desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            m_Device->CreateTexture2D( &desc, nullptr, m_Resolved.Put() );
        }
        else
        {
            m_Resolved = m_Frame;
        }

        m_Device->CreateShaderResourceView( m_Resolved.Get(), nullptr, m_ResolvedView.Put() );

        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        m_Device->CreateTexture2D( &desc, nullptr, m_Front.Put() );
        m_Device->CreateRenderTargetView( m_Front.Get(), nullptr, m_FrontTarget.Put() );
        m_Device->CreateShaderResourceView( m_Front.Get(), nullptr, m_FrontView.Put() );

        // Depth, D24S8 is what every sample count above supports.
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.SampleDesc.Count = m_Samples;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        m_Device->CreateTexture2D( &desc, nullptr, m_Depth.Put() );
        m_Device->CreateDepthStencilView( m_Depth.Get(), nullptr, m_DepthView.Put() );

        // Game sized copies for locks and rectangle copies.
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;

        m_Device->CreateTexture2D( &desc, nullptr, m_GameFrame.Put() );
        m_Device->CreateRenderTargetView( m_GameFrame.Get(), nullptr, m_GameFrameView.Put() );

        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        m_Device->CreateTexture2D( &desc, nullptr, m_Upload.Put() );
        m_Device->CreateShaderResourceView( m_Upload.Get(), nullptr, m_UploadView.Put() );

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        m_Device->CreateTexture2D( &desc, nullptr, m_Staging.Put() );

        m_Clip = { 0, 0, width, height };

        const float black[4] = { 0, 0, 0, 1 };
        m_Context->ClearRenderTargetView( m_FrameView.Get(), black );
        m_Context->ClearRenderTargetView( m_FrontTarget.Get(), black );
        m_Context->ClearDepthStencilView( m_DepthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0 );

        Log( "Frame %dx%d rendered at %dx%d with %u samples", width, height, m_RenderWidth, m_RenderHeight, m_Samples );
        return m_FrameView && m_DepthView && m_ResolvedView && m_FrontView && m_GameFrameView && m_UploadView && m_Staging;
    }


    void Driver::ReleaseFrame()
    {
        if (m_Context)
        {
            m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
            ID3D11ShaderResourceView * none[MAX_STAGES] = {};
            m_Context->PSSetShaderResources( 0, MAX_STAGES, none );
        }

        m_Frame.Reset();
        m_FrameView.Reset();
        m_Resolved.Reset();
        m_ResolvedView.Reset();
        m_Front.Reset();
        m_FrontTarget.Reset();
        m_FrontView.Reset();
        m_Depth.Reset();
        m_DepthView.Reset();
        m_GameFrame.Reset();
        m_GameFrameView.Reset();
        m_Upload.Reset();
        m_UploadView.Reset();
        m_Staging.Reset();

        ResetProbes();
        InvalidatePipeline();
    }


    void Driver::ComputeRenderSize()
    {
        int width = m_Width;
        int height = m_Height;

        if (m_ConfigScale > 0)
        {
            width = m_Width * m_ConfigScale;
            height = m_Height * m_ConfigScale;
        }
        else if (m_ConfigScale == 0 && m_BackBufferWidth > 0 && m_BackBufferHeight > 0)
        {
            // The size the frame covers on the display, never below the game resolution.
            const double fit = std::min( static_cast<double>(m_BackBufferWidth) / m_Width, static_cast<double>(m_BackBufferHeight) / m_Height );
            if (fit > 1.0)
            {
                width = static_cast<int>(std::lround( m_Width * fit ));
                height = static_cast<int>(std::lround( m_Height * fit ));
            }
        }

        // D3D11 texture limit.
        const double limit = std::min( 16384.0 / width, 16384.0 / height );
        if (limit < 1.0)
        {
            width = static_cast<int>( width * limit );
            height = static_cast<int>( height * limit );
        }

        m_RenderWidth = std::max( width, m_Width );
        m_RenderHeight = std::max( height, m_Height );
        m_ScaleX = static_cast<float>( m_RenderWidth ) / static_cast<float>( m_Width );
        m_ScaleY = static_cast<float>( m_RenderHeight ) / static_cast<float>( m_Height );
    }


    D3D11_RECT Driver::ScaledClip() const
    {
        D3D11_RECT rect;
        rect.left = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.left) * m_ScaleX ));
        rect.top = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.top) * m_ScaleY ));
        rect.right = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.right) * m_ScaleX ));
        rect.bottom = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.bottom) * m_ScaleY ));
        return rect;
    }


    void Driver::ResolveFrame()
    {
        if (m_Resolve && m_Frame && m_Resolved)
        {
            m_Context->ResolveSubresource( m_Resolved.Get(), 0, m_Frame.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM );
        }
    }


    void Driver::DownsampleFrame(
        bool front
    )
    {
        if (!front)
            ResolveFrame();

        const float destination[4] = { 0, 0, static_cast<float>(m_Width), static_cast<float>(m_Height) };
        const float source[4] = { 0, 0, 1, 1 };

        Blit( front ? m_FrontView.Get() : m_ResolvedView.Get(), m_GameFrameView.Get(), m_Width, m_Height, destination, source, 1.0f,
            m_PresentSampler.Get() );
    }


    void Driver::Blit(
        ID3D11ShaderResourceView * source,
        ID3D11RenderTargetView * target,
        int target_width,
        int target_height,
        const float * destination_rect,
        const float * source_rect,
        float brightness,
        ID3D11SamplerState * sampler,
        ID3D11PixelShader * pixel_shader,
        float shape
    )
    {
        // The blit binds its own targets, shaders and states.
        InvalidatePipeline();

        PresentConstants constants = {};
        std::memcpy( constants.m_Rect, destination_rect, sizeof( constants.m_Rect ) );
        std::memcpy( constants.m_Source, source_rect, sizeof( constants.m_Source ) );

        constants.m_Params[0] = brightness;
        constants.m_Params[1] = static_cast<float>(target_width);
        constants.m_Params[2] = static_cast<float>(target_height);
        constants.m_Params[3] = shape;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED( m_Context->Map( m_PresentConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            std::memcpy( mapped.pData, &constants, sizeof( constants ) );
            m_Context->Unmap( m_PresentConstants.Get(), 0 );
        }

        ID3D11ShaderResourceView * none[MAX_STAGES] = {};
        m_Context->PSSetShaderResources( 0, MAX_STAGES, none );
        m_Context->OMSetRenderTargets( 1, &target, nullptr );
        D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(target_width), static_cast<float>(target_height), 0, 1 };
        m_Context->RSSetViewports( 1, &viewport );
        m_Context->RSSetState( m_PlainRaster.Get() );

        const float factor[4] = {};
        m_Context->OMSetBlendState( m_OpaqueBlend.Get(), factor, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( m_ClearColorOnly.Get(), 0 );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
        m_Context->VSSetShader( m_PresentVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( pixel_shader != nullptr ? pixel_shader : m_PresentPs.Get(), nullptr, 0 );

        ID3D11Buffer * buffer = m_PresentConstants.Get();
        m_Context->VSSetConstantBuffers( 0, 1, &buffer );
        m_Context->PSSetConstantBuffers( 0, 1, &buffer );
        m_Context->PSSetShaderResources( 0, 1, &source );
        m_Context->PSSetSamplers( 0, 1, &sampler );
        m_Context->Draw( 4, 0 );
        m_Context->PSSetShaderResources( 0, MAX_STAGES, none );
    }


    void Driver::BuildModeList()
    {
        m_Modes.clear();
        m_Modes.resize( 6 );

        // Entries 1 to 5 are the fixed slots the game expects
        const int fixed[5][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 1600, 1200 } };

        for (int i = 0; i < 5; ++i)
        {
            ModeRecord & mode = m_Modes[i + 1];
            mode.m_Width = fixed[i][0];
            mode.m_Height = fixed[i][1];
            mode.m_Bpp = 16;
            mode.m_Format = formats::RGB565;
            mode.m_Valid = 1;
            mode.m_Buffers = 3;
            mode.m_Capability = 2;
        }

        auto add = [&] ( int width, int height )
        {
            for (int bpp : { 16, 32 })
            {
                const bool exists = std::any_of( m_Modes.begin() + 1, m_Modes.end(), [&] ( const ModeRecord & mode )
                {
                    return mode.m_Width == width && mode.m_Height == height && mode.m_Bpp == bpp;
                } );

                if (!exists)
                {
                    ModeRecord mode = {};
                    mode.m_Width = width;
                    mode.m_Height = height;
                    mode.m_Bpp = bpp;
                    mode.m_Format = bpp == 32 ? formats::ARGB8888 : formats::RGB565;
                    mode.m_Valid = 1;
                    mode.m_Buffers = 3;
                    mode.m_Capability = 2;
                    m_Modes.push_back( mode );
                }
            }
        };

        // Every mode of the primary display, the frame is rendered at any size anyway.
        DEVMODEA dev = {};
        dev.dmSize = sizeof( dev );

        for (DWORD i = 0; ::EnumDisplaySettingsA( nullptr, i, &dev ) != FALSE; ++i)
        {
            if (dev.dmBitsPerPel >= 16 && dev.dmPelsWidth >= 640 && dev.dmPelsHeight >= 480)
            {
                add( static_cast<int>(dev.dmPelsWidth), static_cast<int>(dev.dmPelsHeight) );
            }
        }

        for (const auto & fixed_mode : fixed)
        {
            add( fixed_mode[0], fixed_mode[1] );
        }

        // The display reports its modes in no particular order and the options list shows them
        // as they come, so everything after the fixed slots is sorted by size and depth.
        std::sort( m_Modes.begin() + 6, m_Modes.end(), [] ( const ModeRecord & left, const ModeRecord & right )
        {
            if (left.m_Width != right.m_Width)
                return left.m_Width < right.m_Width;

            if (left.m_Height != right.m_Height)
                return left.m_Height < right.m_Height;

            return left.m_Bpp < right.m_Bpp;
        } );

        Log( "%d display modes", static_cast<int>(m_Modes.size()) - 1 );
    }

    // THRASH entry points.

    AboutBlock * Driver::About()
    {
        if (m_Modes.empty())
            BuildModeList();

        m_FormatFlags.fill( 0 );
        m_FormatFlags[formats::ARGB1555] = 1;
        m_FormatFlags[formats::RGB565] = 1;
        m_FormatFlags[formats::ARGB8888] = 1;
        m_FormatFlags[formats::ARGB4444] = 1;

        AboutBlock & about = m_About;
        about = {};
        about.m_Signature = ABOUT_SIGNATURE;
        about.m_Size = sizeof( AboutBlock );
        about.m_Version = DRIVER_VERSION;

        // Square textures, power of 2 width and height, windowed capable.
        about.m_Flags = 0x01 | 0x02 | 0x04 | 0x08 | 0x20;
        about.m_TextureWidthMin = 1;
        about.m_TextureWidthMax = 4096;
        about.m_TextureWidthMultiple = 1;
        about.m_TextureHeightMin = 1;
        about.m_TextureHeightMax = 4096;
        about.m_TextureHeightMultiple = 1;
        about.m_FormatCount = static_cast<std::int32_t>(m_FormatFlags.size());
        about.m_Formats = m_FormatFlags.data();
        about.m_PaletteFormatCount = static_cast<std::int32_t>(m_PaletteFlags.size());
        about.m_PaletteFormats = m_PaletteFlags.data();
        about.m_ModeCount = static_cast<std::int32_t>(m_Modes.size()) - 1;
        about.m_Modes = m_Modes.data();
        about.m_Stages = MAX_STAGES;
        about.m_Subtype = SUBTYPE;
        about.m_TextureMemory = TEXTURE_MEMORY;
        about.m_Author = AUTHOR;
        about.m_DxVersion = DX_VERSION;
        std::strncpy( about.m_Name, "Direct3D 11", sizeof( about.m_Name ) - 1 );
        return &about;
    }


    int Driver::Init()
    {
        Log( "THRASH_init" );

        LoadConfig();
        MakeDpiAware();

        if (!CreateDevice())
        {
            return 0;
        }

        BuildModeList();
        m_D3D.Reset();
        ResetStateCache();
        m_StateStore.clear();
        return 1;
    }


    void Driver::LoadConfig()
    {
        if (m_ConfigLoaded)
            return;

        m_ConfigLoaded = true;

        char path[MAX_PATH] = {};
        ::GetModuleFileNameA( nullptr, path, MAX_PATH );

        std::string ini = path;
        const std::size_t slash = ini.find_last_of( "\\/" );
        ini = (slash != std::string::npos ? ini.substr( 0, slash + 1 ) : std::string()) + "nfs5_enhanced.ini";

        m_ConfigScale = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "ResolutionScale", 0, ini.c_str() ));
        m_ConfigSamples = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Antialiasing", 4, ini.c_str() ));
        m_ConfigAnisotropy = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Anisotropy", 0, ini.c_str() ));
        m_ConfigWindowMode = std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "WindowMode", 0, ini.c_str() )), 0, 2 );
        m_ConfigWindowWidth = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "WindowWidth", 0, ini.c_str() ));
        m_ConfigWindowHeight = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "WindowHeight", 0, ini.c_str() ));
        m_ConfigVsync = ::GetPrivateProfileIntA( "Dx11", "VSync", 1, ini.c_str() ) != 0;
        m_ConfigMinimize = ::GetPrivateProfileIntA( "Dx11", "MinimizeOnFocusLoss", 1, ini.c_str() ) != 0;
        m_ConfigFogEnabled = ::GetPrivateProfileIntA( "Dx11", "FogEnabled", 1, ini.c_str() ) != 0;
        m_ConfigMovie = ::GetPrivateProfileIntA( "Dx11", "MovieFilter", 1, ini.c_str() ) != 0;
        m_ConfigMovieFill = ::GetPrivateProfileIntA( "Dx11", "MovieFill", 1, ini.c_str() ) != 0;
        m_MovieSharpness = static_cast<float>(std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "MovieSharpness", 33, ini.c_str() )), 0, 100 )) / 100.0f;
        m_TraceLines = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Trace", 0, ini.c_str() ));

        Log( "Config ResolutionScale %d Antialiasing %d Anisotropy %d WindowMode %d Window %dx%d", m_ConfigScale, m_ConfigSamples, m_ConfigAnisotropy,
            m_ConfigWindowMode, m_ConfigWindowWidth, m_ConfigWindowHeight );
        Log( "Config VSync %d MinimizeOnFocusLoss %d FogEnabled %d", m_ConfigVsync ? 1 : 0, m_ConfigMinimize ? 1 : 0, m_ConfigFogEnabled ? 1 : 0 );
    }

    void Driver::MakeDpiAware()
    {
        // The frame is presented at physical pixels, a DPI virtualized window would be
        // scaled up again by the compositor.
        using SetContextFn = BOOL( WINAPI * )(HANDLE);
        using SetThreadContextFn = HANDLE( WINAPI * )(HANDLE);
        const HMODULE user32 = ::GetModuleHandleA( "user32.dll" );
        const auto set_process = reinterpret_cast<SetContextFn>(reinterpret_cast<void *>(::GetProcAddress( user32, "SetProcessDpiAwarenessContext" )));
        const auto set_thread = reinterpret_cast<SetThreadContextFn>(reinterpret_cast<void *>(::GetProcAddress( user32, "SetThreadDpiAwarenessContext" )));
        const HANDLE per_monitor_v2 = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-4));

        if (set_process != nullptr)
        {
            set_process( per_monitor_v2 );
        }

        if (set_thread != nullptr)
        {
            set_thread( per_monitor_v2 );
        }
    }


    int Driver::Is()
    {
        return 0x55;
    }


    int Driver::SelectDisplay(
        int
    )
    {
        return 1;
    }


    int Driver::SetVideoMode(
        int mode,
        int,
        int depth
    )
    {
        if (!CreateDevice())
        {
            return 0;
        }

        if (m_Modes.empty())
        {
            BuildModeList();
        }

        if (mode < 1 || mode >= static_cast<int>( m_Modes.size() ))
        {
            Log( "setvideomode: mode %d out of range", mode );
            return 0;
        }

        const ModeRecord & record = m_Modes[static_cast<std::size_t>( mode )];
        Log( "setvideomode %d: %dx%d depth %d", mode, record.m_Width, record.m_Height, depth );

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        m_Width = record.m_Width;
        m_Height = record.m_Height;

        // Color depth as dx7z picks it, '1' means 16 bit, '2' means 32 bit, capped by the mode.
        int bits = depth == 1 ? 16 : (depth == 2 ? 32 : depth);
        if (record.m_Bpp < bits)
        {
            bits = 16;
        }

        m_FrameBits = bits >= 24 ? 32 : 16;
        FitWindow();

        if (!CreateSwapChain())
        {
            return 0;
        }

        EnterFullscreen();

        if (!CreateFrame( record.m_Width, record.m_Height ))
        {
            return 0;
        }

        // Defaults as dx7z applies them in 6000A630.
        m_D3D.Reset();
        ResetStateCache();
        SetState( 0x130, 0 );
        SetState( 1, 0 );
        SetState( 2, 1 );
        SetState( 7, 1 );
        SetState( 6, 1 );
        SetState( 10, 2 );
        SetState( 0x24, 0x10 );
        SetState( 0x0B, 1 );
        SetState( 3, 0 );
        SetState( 0x0C, 0 );
        SetState( 5, 1 );
        SetState( 0x14, 0 );
        SetState( 0x15, 0 );
        SetState( 0x0E, 0 );
        SetState( 0x16, 0 );
        SetState( 0x17, 0x3F800000 );
        SetState( 0x0F, 0xFFFFFFFF );
        SetState( 0x38, 0 );
        SetState( 0x0D, 0 );
        SetState( 0x2E, 0x3F800000 );
        SetState( 0x18, 0 );
        SetState( 8, 0 );
        SetState( 0x2A, 0 );
        SetState( 0x29, 0 );
        SetState( 0x10029, 0 );
        SetState( 0x2F, 0 );
        SetState( 0x3B, 0 );
        SetState( 0x43, 0x3F800000 );
        SetState( 0x3C, 1 );
        SetState( 4, depth >= 1 ? 2 : 0 );
        SetState( 0x28, 3 );

        // State '3E' counts mode changes.
        StoreState( 0x3E, static_cast<std::uint32_t>(GetState( 0x3E ) + 1) );

        // State '1D' points to the frame description taken from a lock of the back buffer, the
        // game reads the pixel format from it right after the mode change.
        m_FrameInfo = {};
        Window( 1 );

        if (const LockInfo * lock = LockWindow())
        {
            m_FrameInfo = *lock;
            UnlockWindow( nullptr );
        }

        Window( 0 );

        if (m_FrameInfo.m_Pitch == 0)
        {
            m_FrameInfo.m_Pitch = m_Width * (m_FrameBits / 8);
            m_FrameInfo.m_Format = m_FrameBits == 16 ? formats::RGB565 : formats::ARGB8888;
            m_FrameInfo.m_Width = m_Width;
            m_FrameInfo.m_Height = m_Height;
        }

        m_FrameInfo.m_Bits = nullptr;
        StoreState( 0x1D, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&m_FrameInfo)) );

        TextureReset();
        return 1;
    }


    int Driver::Restore()
    {
        Log( "THRASH_restore" );
        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        TextureReset();

        // The game unloads the driver next, everything is released here and not at unload.
        ReleaseAll();
        return 1;
    }


    void Driver::ReleaseAll()
    {
        if (m_Context)
        {
            m_Context->ClearState();
            m_Context->Flush();
        }

        ReleaseFrame();
        m_BackBufferView.Reset();

        if (m_SwapChain)
        {
            m_SwapChain->SetFullscreenState( FALSE, nullptr );
        }

        m_SwapChain.Reset();
        m_BackBufferWidth = 0;
        m_BackBufferHeight = 0;
        RestoreWindowProc();

        m_Samplers.clear();
        m_Blends.clear();
        m_DepthStates.clear();
        m_Rasters.clear();
        m_SceneVs.Reset();
        m_ScenePs.Reset();
        m_SceneLayout.Reset();
        m_PresentVs.Reset();
        m_PresentPs.Reset();
        m_MoviePs.Reset();
        m_MovieView.Reset();
        m_MovieTexture.Reset();
        m_MovieWidth = 0;
        m_MovieHeight = 0;
        m_ClearVs.Reset();
        m_ClearPs.Reset();
        m_DrawConstants.Reset();
        m_PresentConstants.Reset();
        m_ClearConstants.Reset();
        m_VertexBuffer.Reset();
        m_IndexBuffer.Reset();
        m_PresentSampler.Reset();
        m_PointSampler.Reset();
        m_OpaqueBlend.Reset();
        m_PlainRaster.Reset();
        m_ClearDepth.Reset();
        m_ClearColorOnly.Reset();
        m_ClearColorBlend.Reset();
        m_ClearDepthOnlyBlend.Reset();
        InvalidatePipeline();
        m_Context.Reset();
        m_Device.Reset();

        Log( "Device released" );
    }


    int Driver::Window(
        int window
    )
    {
        StoreState( 0x3B, static_cast<std::uint32_t>(window) );

        if (window != m_SelectedWindow)
        {
            Trace( "window %d", window );
        }

        m_SelectedWindow = window;
        return window != 0 && m_Frame;
    }


    void Driver::ClearWindow()
    {
        if (!m_FrameView)
            return;

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        const bool depth_only = m_SelectedWindow == 3 || m_SelectedWindow == 5;
        const bool full = m_Clip.left <= 0 && m_Clip.top <= 0 && m_Clip.right >= m_Width && m_Clip.bottom >= m_Height;

        float color[4];
        ColorToFloat4( m_BackgroundColor, color );

        if (full)
        {
            if (!depth_only)
            {
                m_Context->ClearRenderTargetView( m_FrameView.Get(), color );
            }

            m_Context->ClearDepthStencilView( m_DepthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, m_ClearDepthValue, 0 );
            return;
        }

        ClearConstants constants = {};
        const D3D11_RECT clip = ScaledClip();
        constants.m_Rect[0] = static_cast<float>(clip.left);
        constants.m_Rect[1] = static_cast<float>(clip.top);
        constants.m_Rect[2] = static_cast<float>(clip.right - clip.left);
        constants.m_Rect[3] = static_cast<float>(clip.bottom - clip.top);
        constants.m_Target[0] = static_cast<float>(m_RenderWidth);
        constants.m_Target[1] = static_cast<float>(m_RenderHeight);
        constants.m_Target[2] = m_ClearDepthValue;
        std::memcpy( constants.m_Color, color, sizeof( color ) );

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED( m_Context->Map( m_ClearConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            std::memcpy( mapped.pData, &constants, sizeof( constants ) );
            m_Context->Unmap( m_ClearConstants.Get(), 0 );
        }

        ID3D11RenderTargetView * target = m_FrameView.Get();
        m_Context->OMSetRenderTargets( 1, &target, m_DepthView.Get() );

        D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight), 0, 1 };
        m_Context->RSSetViewports( 1, &viewport );
        m_Context->RSSetState( m_PlainRaster.Get() );

        InvalidatePipeline();

        const float factor[4] = {};
        m_Context->OMSetBlendState( depth_only ? m_ClearDepthOnlyBlend.Get() : m_ClearColorBlend.Get(), factor, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( m_ClearDepth.Get(), 0 );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
        m_Context->VSSetShader( m_ClearVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_ClearPs.Get(), nullptr, 0 );
        ID3D11Buffer * buffer = m_ClearConstants.Get();
        m_Context->VSSetConstantBuffers( 0, 1, &buffer );
        m_Context->PSSetConstantBuffers( 0, 1, &buffer );
        m_Context->Draw( 4, 0 );
    }


    int Driver::Clip(
        int x0,
        int y0,
        int x1,
        int y1
    )
    {
        Trace( "clip %d %d %d %d", x0, y0, x1, y1 );
        Flush();

        m_Clip.left = std::clamp( x0, 0, std::max( m_Width, 0 ) );
        m_Clip.top = std::clamp( y0, 0, std::max( m_Height, 0 ) );
        m_Clip.right = std::clamp( x1, 0, std::max( m_Width, 0 ) );
        m_Clip.bottom = std::clamp( y1, 0, std::max( m_Height, 0 ) );
        return 1;
    }


    int Driver::FlushWindow()
    {
        Flush();
        return 1;
    }


    void Driver::PageFlip()
    {
        ++m_FlipCount;
        Flush();

        if (m_Locked)
            UnlockWindow( nullptr );

        if (!m_SwapChain || !m_ResolvedView)
            return;

        // DXGI leaves exclusive mode when the window loses focus, return once it is back.
        if (m_ReenterFullscreen && !m_Inactive && !::IsIconic( m_Window ))
        {
            m_ReenterFullscreen = false;
            EnterFullscreen();
        }

        ResolveFrame();
        m_Context->CopyResource( m_Front.Get(), m_Resolved.Get() );
        PresentFront();

        // Follow the window when something else resized it, the next frame renders at the new size.
        RECT client = {};
        ::GetClientRect( m_Window, &client );

        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;

        if (client_width > 0 && client_height > 0 && (client_width != m_BackBufferWidth || client_height != m_BackBufferHeight))
        {
            m_BackBufferWidth = client_width;
            m_BackBufferHeight = client_height;

            if (CreateSwapChain())
            {
                const int render_width = m_RenderWidth;
                const int render_height = m_RenderHeight;

                ComputeRenderSize();

                if (render_width != m_RenderWidth || render_height != m_RenderHeight)
                {
                    const D3D11_RECT clip = m_Clip;
                    CreateFrame( m_Width, m_Height );
                    m_Clip = clip;
                }
            }
        }
    }


    void Driver::PresentFront()
    {
        if (!m_SwapChain || !m_FrontView)
            return;

        // Fit the frame into the back buffer with its own aspect ratio, a frame rendered at
        // the fitted size is copied one to one.
        const float scale = std::min( static_cast<float>(m_BackBufferWidth) / static_cast<float>(m_RenderWidth),
            static_cast<float>(m_BackBufferHeight) / static_cast<float>(m_RenderHeight) );
        const float width = std::round( static_cast<float>(m_RenderWidth) * scale );
        const float height = std::round( static_cast<float>(m_RenderHeight) * scale );
        const float destination[4] = { std::floor( (static_cast<float>(m_BackBufferWidth) - width) * 0.5f ),
            std::floor( (static_cast<float>(m_BackBufferHeight) - height) * 0.5f ), width, height };
        const float source[4] = { 0, 0, 1, 1 };

        ID3D11RenderTargetView * target = m_BackBufferView.Get();

        const float black[4] = { 0, 0, 0, 1 };
        m_Context->ClearRenderTargetView( target, black );
        Blit( m_FrontView.Get(), target, m_BackBufferWidth, m_BackBufferHeight, destination, source, m_Brightness, m_PresentSampler.Get() );
        PresentBackBuffer();
    }


    void Driver::PresentBackBuffer()
    {
        if (!m_SwapChain)
            return;

        // Nothing to show while minimized, a short wait keeps the paused game from spinning.
        if (::IsIconic( m_Window ))
        {
            Trace( "present skipped, minimized" );
            ::Sleep( 16 );
            return;
        }

        BOOL fullscreen = FALSE;
        m_SwapChain->GetFullscreenState( &fullscreen, nullptr );
        const UINT flags = !m_ConfigVsync && m_AllowTearing && !fullscreen ? static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING) : 0u;
        m_SwapChain->Present( m_ConfigVsync ? 1 : 0, flags );
    }


    int Driver::Sync(
        int mode
    )
    {
        Flush();

        if (mode == 0 && m_Context)
        {
            m_Context->Flush();
        }

        return 0;
    }


    LockInfo * Driver::LockWindow()
    {
        if (!m_Frame || !m_Staging || m_SelectedWindow == 0 || m_SelectedWindow == 3 || m_SelectedWindow == 5)
        {
            Trace( "lockwindow refused, window %d", m_SelectedWindow );
            return nullptr;
        }

        Flush();

        if (m_Locked)
        {
            Trace( "lockwindow while locked" );
            return &m_Lock;
        }

        Trace( "lockwindow window %d bits %d", m_SelectedWindow, m_FrameBits );

        if (IsDirect())
        {
            m_Context->CopyResource( m_Staging.Get(), m_Frame.Get() );
        }
        else
        {
            DownsampleFrame( IsFrontSelected() );
            m_Context->CopyResource( m_Staging.Get(), m_GameFrame.Get() );
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_Staging.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped ) ))
        {
            return nullptr;
        }

        m_LockMapped = mapped.pData;
        m_LockMappedPitch = mapped.RowPitch;

        if (m_FrameBits == 16)
        {
            const std::uint32_t pitch = static_cast<std::uint32_t>(m_Width) * 2;
            m_LockPixels.resize( static_cast<std::size_t>(pitch) * static_cast<std::size_t>(m_Height) );
            ToRgb565( static_cast<const std::uint8_t *>(mapped.pData), mapped.RowPitch, m_LockPixels.data(), pitch, m_Width, m_Height );
            m_Lock.m_Bits = m_LockPixels.data();
            m_Lock.m_Pitch = static_cast<std::int32_t>(pitch);
            m_Lock.m_Format = formats::RGB565;
        }
        else
        {
            m_Lock.m_Bits = mapped.pData;
            m_Lock.m_Pitch = static_cast<std::int32_t>(mapped.RowPitch);
            m_Lock.m_Format = formats::ARGB8888;
        }

        m_Lock.m_Width = m_Width;
        m_Lock.m_Height = m_Height;
        m_Locked = true;

        // Kept to find out on unlock whether the game wrote anything.
        const auto * bits = static_cast<const std::uint8_t *>(m_Lock.m_Bits);
        m_LockSnapshot.assign( bits, bits + static_cast<std::size_t>(m_Lock.m_Pitch) * static_cast<std::size_t>(m_Height) );
        return &m_Lock;
    }


    bool Driver::MovieBlit(
        const void * pixels,
        int pitch,
        int width,
        int height,
        int x,
        int y,
        int scale
    )
    {
        // Only for a movie drawn into the front buffer.
        if (!m_ConfigMovie || !m_MoviePs || !m_Front || !IsFrontSelected() || pixels == nullptr || scale < 1 || width < 8 || height < 8 ||
            width > 2048 || height > 2048 || pitch < width * 2 || (width & 1) != 0)
        {
            return false;
        }

        Flush();

        if (m_MovieWidth != width || m_MovieHeight != height || !m_MovieTexture)
        {
            m_MovieView.Reset();
            m_MovieTexture.Reset();

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(width);
            desc.Height = static_cast<UINT>(height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_MovieTexture.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_MovieTexture.Get(), nullptr, m_MovieView.Put() ) ))
            {
                m_MovieView.Reset();
                m_MovieTexture.Reset();
                return false;
            }

            m_MovieWidth = width;
            m_MovieHeight = height;
            m_MovieBarTop = -1;
            m_MovieBarBottom = -1;
        }

        // A pause between frames means another movie, its bars are measured again.
        const DWORD now = ::GetTickCount();

        if (now - m_MovieTick > 500)
        {
            m_MovieBarTop = -1;
            m_MovieBarBottom = -1;
        }

        m_MovieTick = now;

        if (!m_MovieLogged)
        {
            m_MovieLogged = true;
            Log( "Movie frame %dx%d at %d,%d scale %d, bicubic sharpness %.2f", width, height, x, y, scale, m_MovieSharpness );
        }

        m_MoviePixels.resize( static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 );
        FromMovieYuv( static_cast<const std::uint8_t *>(pixels), static_cast<std::uint32_t>(pitch), m_MoviePixels.data(), static_cast<std::uint32_t>(width) * 4, width, height );
        m_Context->UpdateSubresource( m_MovieTexture.Get(), 0, nullptr, m_MoviePixels.data(), static_cast<UINT>(width) * 4, 0 );

        // A movie as wide as the screen fills the window, in the aspect ratio of what is left
        // after the black bars of the frame, the smallest bars seen so far.
        if (m_ConfigMovieFill && x == 0 && width * scale == m_Width && m_BackBufferView && !::IsIconic( m_Window ))
        {
            int top = 0;
            int bottom = 0;

            if (FindMovieBars( static_cast<const std::uint8_t *>(pixels), static_cast<std::uint32_t>(pitch), width, height, top, bottom ))
            {
                const int previous_top = m_MovieBarTop;
                const int previous_bottom = m_MovieBarBottom;
                m_MovieBarTop = m_MovieBarTop < 0 ? top : std::min( m_MovieBarTop, top );
                m_MovieBarBottom = m_MovieBarBottom < 0 ? bottom : std::min( m_MovieBarBottom, bottom );

                if (m_MovieBarTop != previous_top || m_MovieBarBottom != previous_bottom)
                {
                    Log( "Movie black bars %d at the top, %d at the bottom", m_MovieBarTop, m_MovieBarBottom );
                }
            }

            int cut_top = std::max( m_MovieBarTop, 0 );
            int cut_bottom = std::max( m_MovieBarBottom, 0 );

            // A frame that is mostly bars is not trusted.
            if (cut_top + cut_bottom > height / 2)
            {
                cut_top = 0;
                cut_bottom = 0;
            }

            const int visible = height - cut_top - cut_bottom;
            const float region[4] = { 0, static_cast<float>(cut_top) / static_cast<float>(height), 1, static_cast<float>(visible) / static_cast<float>(height) };

            const float target_width = static_cast<float>(m_BackBufferWidth);
            const float target_height = static_cast<float>(m_BackBufferHeight);
            const float movie_width = static_cast<float>(width * scale);
            const float movie_height = static_cast<float>(visible * scale);
            const float fit = std::min( target_width / movie_width, target_height / movie_height );
            const float fitted_width = std::round( movie_width * fit );
            const float fitted_height = std::round( movie_height * fit );
            const float place[4] = { std::floor( (target_width - fitted_width) * 0.5f ), std::floor( (target_height - fitted_height) * 0.5f ), fitted_width, fitted_height };

            const float black[4] = { 0, 0, 0, 1 };
            m_Context->ClearRenderTargetView( m_BackBufferView.Get(), black );
            Blit( m_MovieView.Get(), m_BackBufferView.Get(), m_BackBufferWidth, m_BackBufferHeight, place, region, m_Brightness, m_PointSampler.Get(), m_MoviePs.Get(),
                m_MovieSharpness );
            PresentBackBuffer();
            return true;
        }

        // Edges snap to whole pixels of the frame so the picture has no soft border.
        const float left = std::round( static_cast<float>(x) * m_ScaleX );
        const float top = std::round( static_cast<float>(y) * m_ScaleY );
        const float right = std::round( static_cast<float>(x + width * scale) * m_ScaleX );
        const float bottom = std::round( static_cast<float>(y + height * scale) * m_ScaleY );
        const float destination[4] = { left, top, right - left, bottom - top };
        const float source[4] = { 0, 0, 1, 1 };

        Blit( m_MovieView.Get(), m_FrontTarget.Get(), m_RenderWidth, m_RenderHeight, destination, source, 1.0f, m_PointSampler.Get(), m_MoviePs.Get(), m_MovieSharpness );
        PresentFront();
        return true;
    }


    int Driver::UnlockWindow(
        LockInfo *
    )
    {
        if (!m_Locked)
        {
            return 1;
        }

        m_Locked = false;

        const std::size_t size = static_cast<std::size_t>(m_Lock.m_Pitch) * static_cast<std::size_t>(m_Height);
        const bool written = m_LockSnapshot.size() != size || std::memcmp( m_LockSnapshot.data(), m_Lock.m_Bits, size ) != 0;
        Trace( "unlockwindow written %d", written ? 1 : 0 );

        if (written && m_FrameBits == 16)
        {
            FromRgb565( m_LockPixels.data(), static_cast<std::uint32_t>(m_Lock.m_Pitch), static_cast<std::uint8_t *>(m_LockMapped), m_LockMappedPitch,
                m_Width, m_Height );
        }

        m_Context->Unmap( m_Staging.Get(), 0 );
        m_LockMapped = nullptr;

        if (!written)
        {
            return 1;
        }

        if (IsDirect())
        {
            m_Context->CopyResource( m_Frame.Get(), m_Staging.Get() );
        }
        else
        {
            // Scale the game image back over the frame or the front buffer.
            m_Context->CopyResource( m_Upload.Get(), m_Staging.Get() );
            const float destination[4] = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight) };
            const float source[4] = { 0, 0, 1, 1 };
            const bool front = IsFrontSelected();
            Blit( m_UploadView.Get(), front ? m_FrontTarget.Get() : m_FrameView.Get(), m_RenderWidth, m_RenderHeight, destination, source, 1.0f,
                m_PresentSampler.Get() );

            if (front)
            {
                PresentFront();
            }
        }

        return 1;
    }


    int Driver::ReadRect(
        int x,
        int y,
        int width,
        int height,
        void * out
    )
    {
        Trace( "readrect %d %d %d %d", x, y, width, height );

        if (!m_Frame || out == nullptr || width <= 0 || height <= 0)
        {
            return 0;
        }

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        const int x0 = std::clamp( x, 0, m_Width );
        const int y0 = std::clamp( y, 0, m_Height );
        const int x1 = std::clamp( x + width, 0, m_Width );
        const int y1 = std::clamp( y + height, 0, m_Height );

        const std::size_t pixel = m_FrameBits == 16 ? 2 : 4;
        std::memset( out, 0, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * pixel );

        if (x1 <= x0 || y1 <= y0)
        {
            return 1;
        }

        // Small probes skip the downscale of the whole frame, the 4x4 sun probe may take the
        // pixels of its previous request.
        const bool sun_probe = width == 4 && height == 4;

        if ((x1 - x0) * (y1 - y0) <= 32 * 32 && ReadRectProbe( x0, y0, x1, y1, x, y, width, sun_probe, static_cast<std::uint8_t *>(out) ))
        {
            return 1;
        }

        ID3D11Texture2D * source_texture = m_Frame.Get();

        if (!IsDirect())
        {
            DownsampleFrame( IsFrontSelected() );
            source_texture = m_GameFrame.Get();
        }

        D3D11_BOX box = { static_cast<UINT>(x0), static_cast<UINT>(y0), 0, static_cast<UINT>(x1), static_cast<UINT>(y1), 1 };
        m_Context->CopySubresourceRegion( m_Staging.Get(), 0, static_cast<UINT>(x0), static_cast<UINT>(y0), 0, source_texture, 0, &box );
        D3D11_MAPPED_SUBRESOURCE mapped = {};

        if (FAILED( m_Context->Map( m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped ) ))
        {
            return 0;
        }

        auto * destination = static_cast<std::uint8_t *>(out);

        for (int row = y0; row < y1; ++row)
        {
            const auto * source = static_cast<const std::uint8_t *>( mapped.pData ) + static_cast<std::size_t>( row ) * mapped.RowPitch +
                static_cast<std::size_t>( x0 ) * 4;
            auto * line = destination + (static_cast<std::size_t>( row - y ) * static_cast<std::size_t>( width ) + static_cast<std::size_t>( x0 - x )) * pixel;

            if (pixel == 2)
            {
                ToRgb565( source, 0, line, 0, x1 - x0, 1 );
            }
            else
            {
                std::memcpy( line, source, static_cast<std::size_t>(x1 - x0) * 4 );
            }
        }

        m_Context->Unmap( m_Staging.Get(), 0 );
        return 1;
    }


    bool Driver::ReadRectProbe(
        int x0,
        int y0,
        int x1,
        int y1,
        int x,
        int y,
        int width,
        bool allow_previous,
        std::uint8_t * out
    )
    {
        if (IsFrontSelected() || !m_Resolved)
            return false;

        // Render pixels covered by the game pixels, the source of every game pixel is the render
        // pixel under its center like the linear downscale would pick it for small factors.
        ProbeRead request = {};
        request.m_X0 = x0;
        request.m_Y0 = y0;
        request.m_X1 = x1;
        request.m_Y1 = y1;
        request.m_X = x;
        request.m_Y = y;
        request.m_Width = width;
        request.m_ScaleX = m_ScaleX;
        request.m_ScaleY = m_ScaleY;
        request.m_FrameBits = m_FrameBits;
        request.m_RenderX0 = std::clamp( static_cast<int>(std::floor( static_cast<float>(x0) * m_ScaleX )), 0, m_RenderWidth - 1 );
        request.m_RenderY0 = std::clamp( static_cast<int>(std::floor( static_cast<float>(y0) * m_ScaleY )), 0, m_RenderHeight - 1 );
        request.m_RenderX1 = std::clamp( static_cast<int>(std::ceil( static_cast<float>(x1) * m_ScaleX )), request.m_RenderX0 + 1, m_RenderWidth );
        request.m_RenderY1 = std::clamp( static_cast<int>(std::ceil( static_cast<float>(y1) * m_ScaleY )), request.m_RenderY0 + 1, m_RenderHeight );

        if (request.m_RenderX1 - request.m_RenderX0 > PROBE_SIZE || request.m_RenderY1 - request.m_RenderY0 > PROBE_SIZE)
            return false;

        for (ComPtr<ID3D11Texture2D> & probe : m_Probes)
        {
            if (probe)
                continue;

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = PROBE_SIZE;
            desc.Height = PROBE_SIZE;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, probe.Put() ) ))
                return false;
        }

        // The current request is copied in any case, it serves the next sun read.
        const std::size_t slot = m_ProbeNext;
        const std::size_t previous = slot ^ 1;
        m_ProbeNext = previous;

        ResolveFrame();

        const D3D11_BOX box = { static_cast<UINT>(request.m_RenderX0), static_cast<UINT>(request.m_RenderY0), 0,
            static_cast<UINT>(request.m_RenderX1), static_cast<UINT>(request.m_RenderY1), 1 };
        m_Context->CopySubresourceRegion( m_Probes[slot].Get(), 0, 0, 0, 0, m_Resolved.Get(), 0, &box );

        request.m_Time = std::chrono::steady_clock::now();
        request.m_Valid = true;
        m_ProbeReads[slot] = request;

        // A recent read of the same size and frame layout is answered from the previous copy.
        std::size_t source = slot;
        const ProbeRead & last = m_ProbeReads[previous];

        if (allow_previous && last.m_Valid && request.m_Time - last.m_Time < PROBE_REUSE_WINDOW && last.m_Width == width &&
            last.m_X1 - last.m_X0 == x1 - x0 && last.m_Y1 - last.m_Y0 == y1 - y0 && last.m_ScaleX == m_ScaleX && last.m_ScaleY == m_ScaleY &&
            last.m_FrameBits == m_FrameBits)
        {
            source = previous;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};

        if (FAILED( m_Context->Map( m_Probes[source].Get(), 0, D3D11_MAP_READ, 0, &mapped ) ))
        {
            m_ProbeReads[slot].m_Valid = false;
            return false;
        }

        // The game gives up on the lens flare when reads average more than 5 ms.
        const auto waited = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - request.m_Time );

        if (waited.count() > 5000 && m_SlowProbeLogs < 10)
        {
            ++m_SlowProbeLogs;
            Log( "readrect %dx%d waited %.1f ms for the GPU, %s copy", x1 - x0, y1 - y0, static_cast<double>(waited.count()) / 1000.0,
                source == slot ? "current" : "previous" );
        }

        // A copy that answered is not used twice, the one just made answers the next read.
        const ProbeRead read = m_ProbeReads[source];

        if (source == previous)
        {
            m_ProbeReads[previous].m_Valid = false;
        }

        const std::size_t pixel = read.m_FrameBits == 16 ? 2 : 4;

        // The caller cleared the whole output, only the part inside the frame is written.
        for (int row = read.m_Y0; row < read.m_Y1; ++row)
        {
            const int render_row = std::clamp( static_cast<int>( (static_cast<float>(row) + 0.5f) * read.m_ScaleY ), read.m_RenderY0, read.m_RenderY1 - 1 ) -
                read.m_RenderY0;
            const auto * line_in = static_cast<const std::uint8_t *>( mapped.pData ) + static_cast<std::size_t>( render_row ) * mapped.RowPitch;
            auto * line_out = out + (static_cast<std::size_t>( row - read.m_Y ) * static_cast<std::size_t>( read.m_Width ) +
                static_cast<std::size_t>( read.m_X0 - read.m_X )) * pixel;

            for (int column = read.m_X0; column < read.m_X1; ++column)
            {
                const int render_column = std::clamp( static_cast<int>( (static_cast<float>(column) + 0.5f) * read.m_ScaleX ), read.m_RenderX0,
                    read.m_RenderX1 - 1 ) - read.m_RenderX0;
                const auto * source_pixel = line_in + static_cast<std::size_t>( render_column ) * 4;
                auto * destination = line_out + static_cast<std::size_t>( column - read.m_X0 ) * pixel;

                if (pixel == 2)
                {
                    ToRgb565( source_pixel, 0, destination, 0, 1, 1 );
                }
                else
                {
                    std::memcpy( destination, source_pixel, 4 );
                }
            }
        }

        m_Context->Unmap( m_Probes[source].Get(), 0 );
        return true;
    }


    void Driver::ResetProbes()
    {
        for (ComPtr<ID3D11Texture2D> & probe : m_Probes)
        {
            probe.Reset();
        }

        m_ProbeReads = {};
        m_ProbeNext = 0;
    }


    int Driver::WriteRect(
        int x,
        int y,
        int width,
        int height,
        const void * in
    )
    {
        Trace( "writerect %d %d %d %d", x, y, width, height );

        if (!m_Frame || in == nullptr || width <= 0 || height <= 0)
        {
            return 0;
        }

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        if (x < 0 || y < 0 || x + width > m_Width || y + height > m_Height)
        {
            return 0;
        }

        std::vector<std::uint8_t> converted;

        if (m_FrameBits == 16)
        {
            converted.resize( static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 );
            FromRgb565( static_cast<const std::uint8_t *>(in), static_cast<std::uint32_t>(width) * 2, converted.data(), static_cast<std::uint32_t>(width) * 4,
                width, height );
            in = converted.data();
        }

        D3D11_BOX box = { static_cast<UINT>(x), static_cast<UINT>(y), 0, static_cast<UINT>(x + width), static_cast<UINT>(y + height), 1 };

        if (IsDirect())
        {
            m_Context->UpdateSubresource( m_Frame.Get(), 0, &box, in, static_cast<UINT>(width) * 4, 0 );
            return 1;
        }

        m_Context->UpdateSubresource( m_Upload.Get(), 0, &box, in, static_cast<UINT>(width) * 4, 0 );

        const float destination[4] = { static_cast<float>(x) * m_ScaleX, static_cast<float>(y) * m_ScaleY, static_cast<float>(width) * m_ScaleX,
            static_cast<float>(height) * m_ScaleY };
        const float source[4] = { static_cast<float>(x) / static_cast<float>(m_Width), static_cast<float>(y) / static_cast<float>(m_Height),
            static_cast<float>(width) / static_cast<float>(m_Width), static_cast<float>(height) / static_cast<float>(m_Height) };

        const bool front = IsFrontSelected();
        Blit( m_UploadView.Get(), front ? m_FrontTarget.Get() : m_FrameView.Get(), m_RenderWidth, m_RenderHeight, destination, source, 1.0f,
            m_PresentSampler.Get() );

        if (front)
        {
            PresentFront();
        }

        return 1;
    }

    // Textures.

    Texture * Driver::TextureAlloc(
        int width,
        int height,
        int format,
        int,
        int mips
    )
    {
        if (!m_Device || width <= 0 || height <= 0)
            return nullptr;

        auto texture = std::make_unique<Texture>();
        texture->m_Width = width;
        texture->m_Height = height;
        texture->m_Format = format & 0xFF;
        texture->m_Stage = mips > 0 && (static_cast<std::uint32_t>(mips) & 0xFFFF0000u) == 0x10000u ? 1 : 0;
        texture->m_Levels = mips > 0 ? static_cast<int>(static_cast<std::uint32_t>(mips) & 0xFFFFu) + 1 : 1;

        // Never more levels than the size allows.
        int max_levels = 1;
        for (int size = std::max( width, height ); size > 1; size >>= 1)
        {
            ++max_levels;
        }

        texture->m_Levels = std::clamp( texture->m_Levels, 1, max_levels );

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = static_cast<UINT>(texture->m_Levels);
        desc.ArraySize = 1;
        desc.Format = formats::ToDxgi( texture->m_Format );
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, texture->m_Texture.Put() ) ))
        {
            Log( "talloc %dx%d format %d failed", width, height, format );
            return nullptr;
        }

        m_Device->CreateShaderResourceView( texture->m_Texture.Get(), nullptr, texture->m_View.Put() );

        Texture * raw = texture.get();
        m_Textures.emplace( raw, std::move( texture ) );
        return raw;
    }


    bool Driver::IsTextureHandle(
        std::uintptr_t handle
    ) const
    {
        return handle >= 0x10000 && m_Textures.find( reinterpret_cast<Texture *>(handle) ) != m_Textures.end();
    }


    int Driver::TextureUpdate(
        Texture * texture,
        const void * pixels,
        const std::uint32_t * palette
    )
    {
        if (texture != nullptr && !IsTextureHandle( reinterpret_cast<std::uintptr_t>(texture) ))
        {
            return 0;
        }

        if (texture == nullptr || pixels == nullptr || !texture->m_Texture)
        {
            return texture != nullptr;
        }

        if (m_Bound[0] == texture || m_Bound[1] == texture)
        {
            Flush();
        }

        // Compressed images carry an 8 byte header ahead of the blocks, dx7z skips it too.
        const auto * source = static_cast<const std::uint8_t *>(pixels);

        if (formats::IsBlockCompressed( texture->m_Format ))
        {
            source += 8;
        }

        std::vector<std::uint8_t> converted;
        int width = texture->m_Width;
        int height = texture->m_Height;

        for (int level = 0; level < texture->m_Levels; ++level)
        {
            const std::size_t pitch = formats::UploadPitch( texture->m_Format, width );
            const std::size_t rows = formats::IsBlockCompressed( texture->m_Format ) ? static_cast<std::size_t>( (std::max( height, 1 ) + 3) / 4 )
                : static_cast<std::size_t>( std::max( height, 1 ) );
            converted.resize( pitch * rows );
            formats::ConvertLevel( texture->m_Format, width, height, source, palette, converted.data() );
            m_Context->UpdateSubresource( texture->m_Texture.Get(), static_cast<UINT>( level ), nullptr, converted.data(), static_cast<UINT>( pitch ), 0 );

            source += formats::LevelSize( texture->m_Format, width, height );
            width = std::max( width / 2, 1 );
            height = std::max( height / 2, 1 );
        }
        return 1;
    }


    int Driver::TextureFree(
        Texture * texture
    )
    {
        const auto it = m_Textures.find( texture );

        if (it == m_Textures.end())
            return 0;

        Flush();

        for (auto & bound : m_Bound)
        {
            if (bound == texture)
            {
                bound = nullptr;
            }
        }

        it->second->m_Magic = 0;
        m_Textures.erase( it );
        return 1;
    }


    int Driver::TextureReset()
    {
        Flush();
        m_Bound.fill( nullptr );

        for (auto & entry : m_Textures)
        {
            entry.second->m_Magic = 0;
        }

        m_Textures.clear();

        // dx7z counts resets in state 37 and unbinds stage 0.
        StoreState( 0x37, static_cast<std::uint32_t>(GetState( 0x37 ) + 1) );
        SetTexture( 0 );
        return 1;
    }


    int Driver::SetTexture(
        std::uintptr_t handle
    )
    {
        if (handle < 16)
        {
            Flush();

            if (handle == 0)
            {
                m_Bound.fill( nullptr );
            }
            else if (handle == 1)
            {
                m_Bound[0] = nullptr;
            }
            else if (handle == 2)
            {
                m_Bound[1] = nullptr;
            }

            StoreState( 1, static_cast<std::uint32_t>(handle) );
            return 1;
        }

        if (!IsTextureHandle( handle ))
            return 0;

        auto * texture = reinterpret_cast<Texture *>(handle);

        if (m_Bound[static_cast<std::size_t>(texture->m_Stage)] != texture)
        {
            Flush();
            m_Bound[static_cast<std::size_t>(texture->m_Stage)] = texture;
        }

        StoreState( 1, static_cast<std::uint32_t>(handle) );
        return 1;
    }

    // States.

    void Driver::StoreState(
        std::uint32_t id,
        std::uint32_t value
    )
    {
        const std::uint32_t stage = (id >> 16) & 7;
        m_StateStore[id & 0xFFFF][stage] = value;
    }


    std::intptr_t Driver::GetState(
        std::uint32_t id
    )
    {
        const auto it = m_StateStore.find( id & 0xFFFF );

        if (it == m_StateStore.end())
            return 0;

        return static_cast<std::intptr_t>(it->second[(id >> 16) & 7]);
    }


    void Driver::SetTextureStageMode(
        int stage,
        std::uint32_t mode
    )
    {
        auto & s = m_D3D.m_Stage[static_cast<std::size_t>(stage)];
        const std::uint32_t current_or_diffuse = stage == 0 ? ta::DIFFUSE : ta::CURRENT;

        auto standard = [&] ( std::uint32_t color_op, std::uint32_t alpha_op )
        {
            s[tss::COLORARG1] = ta::TEXTURE;
            s[tss::COLORARG2] = ta::CURRENT;
            s[tss::COLOROP] = color_op;
            s[tss::ALPHAOP] = alpha_op;
            s[tss::ALPHAARG1] = ta::TEXTURE;
            s[tss::ALPHAARG2] = current_or_diffuse;
        };

        switch (mode)
        {
            case 0:
            case 8:
                if (stage == 0)
                {
                    s[tss::COLOROP] = top::MODULATE;
                    s[tss::COLORARG1] = ta::TEXTURE;
                    s[tss::COLORARG2] = ta::DIFFUSE;
                    s[tss::ALPHAOP] = top::MODULATE;
                    s[tss::ALPHAARG1] = ta::TEXTURE;
                    s[tss::ALPHAARG2] = ta::DIFFUSE;
                }
                else if (mode == 0)
                {
                    s[tss::COLOROP] = top::DISABLE;
                    s[tss::ALPHAOP] = top::DISABLE;
                }
                else
                {
                    s[tss::COLORARG1] = ta::TEXTURE;
                    s[tss::COLORARG2] = ta::CURRENT;
                    s[tss::ALPHAARG1] = ta::TEXTURE;
                    s[tss::ALPHAARG2] = ta::CURRENT;
                    s[tss::COLOROP] = top::BLENDTEXTUREALPHA;
                    s[tss::ALPHAOP] = top::DISABLE;
                }
                break;

            case 1:
                standard( top::ADD, top::ADD );
                break;

            case 2:
                standard( top::DISABLE, top::DISABLE );
                break;

            case 3:
                standard( top::MODULATE, top::MODULATE );
                break;

            case 4:
                standard( top::SUBTRACT, top::SELECTARG1 );
                break;

            case 5:
                standard( top::SELECTARG2, top::ADDSIGNED );
                s[tss::ALPHAARG1] = ta::TEXTURE | ta::COMPLEMENT;
                s[tss::ALPHAARG2] = ta::CURRENT;
                break;

            case 6:
                standard( top::MODULATE2X, top::MODULATE2X );
                break;

            case 7:
                standard( top::MODULATE4X, top::MODULATE4X );
                break;

            case 0x61:
                standard( top::ADDSIGNED, top::ADDSIGNED );
                break;

            case 0x62:
                standard( top::ADD, top::BLENDFACTORALPHA );
                s[tss::ALPHAARG2] = ta::TFACTOR;
                break;

            case 0x63:
                standard( top::BLENDFACTORALPHA, top::SELECTARG1 );
                break;

            default:
                break;
        }
    }

    std::intptr_t Driver::SetState(
        std::uint32_t id,
        std::uintptr_t value
    )
    {
        const std::uint32_t state = id & 0xFFFF;
        const int stage = std::min<int>( static_cast<int>((id >> 16) & 7), MAX_STAGES - 1 );
        const std::uint32_t v = static_cast<std::uint32_t>(value);

        auto & render = m_D3D.m_Render;
        auto & tex_stage = m_D3D.m_Stage[static_cast<std::size_t>(stage)];

        std::intptr_t result = 1;
        const bool cached = state < STATE_CACHE_STATES && IsCachedState( state );

        // Render states ignore the stage bits, texture stage states use the clamped stage, so two
        // ids that write the same value share one cache entry.
        const std::size_t cache_slot = static_cast<std::size_t>(state) * 8 + (IsStageState( state ) ? static_cast<std::size_t>(stage) : 0);

        // The game sets the same states again for every mesh. A value that is already in effect
        // changes nothing, so the batch goes on instead of ending in a separate draw.
        if (cached && m_StateCacheGeneration[cache_slot] == m_StateGeneration && m_StateCacheValue[cache_slot] == v)
        {
            return 1;
        }

        // State changes end the current batch, as dx7z does before every render state call.
        Flush();

        switch (state)
        {
            case 1:
                return SetTexture( value );

            case 2:
                if (v == 0)
                {
                    render[rs::CULLMODE] = 1;
                }
                else if (v == 2)
                {
                    render[rs::CULLMODE] = 2;
                }
                else if (v == 1)
                {
                    render[rs::CULLMODE] = 3;
                }
                else
                {
                    return 0;
                }
                break;

            case 3:
                m_BackgroundColor = v;
                break;

            case 4:
                if (v == 0)
                {
                    render[rs::ZWRITEENABLE] = 0;
                    render[rs::ZENABLE] = 0;
                    render[rs::ZFUNC] = 8;
                }
                else if (v == 1 || v == 2)
                {
                    render[rs::ZENABLE] = 1;
                    render[rs::ZWRITEENABLE] = 1;

                    const std::intptr_t stored = GetState( 0x28 );
                    render[rs::ZFUNC] = static_cast<std::uint32_t>(stored) + 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 5:
                render[rs::DITHERENABLE] = v != 0;
                break;

            case 6:
                if (v == 0)
                {
                    render[rs::SHADEMODE] = 1;
                    render[rs::SPECULARENABLE] = 0;
                    m_WhiteDiffuse = true;
                }
                else if (v == 1 || v == 2)
                {
                    render[rs::SHADEMODE] = 2;
                    render[rs::SPECULARENABLE] = v == 2;
                    m_WhiteDiffuse = false;
                }
                else
                {
                    return 0;
                }
                break;

            case 7:
                if (v == 0)
                {
                    tex_stage[tss::MAGFILTER] = 1;
                    tex_stage[tss::MINFILTER] = 1;
                }
                else if (v == 1)
                {
                    tex_stage[tss::MAGFILTER] = 2;
                    tex_stage[tss::MINFILTER] = 2;
                }
                else if (v == 2)
                {
                    tex_stage[tss::MAGFILTER] = 5;
                    tex_stage[tss::MINFILTER] = 3;
                }
                else
                {
                    return 0;
                }
                break;

            case 8:
                tex_stage[tss::MIPMAPLODBIAS] = v;
                break;

            case 9:
                render[rs::ANTIALIAS] = v >= 2 ? 2 : 0;
                break;

            case 0x0A:
                if (v == 0)
                {
                    render[rs::ALPHABLENDENABLE] = 0;
                }
                else if (v == 2)
                {
                    render[rs::ALPHABLENDENABLE] = 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x0B:
                if (v == 0)
                {
                    tex_stage[tss::MIPFILTER] = 1;
                }
                else if (v == 1)
                {
                    tex_stage[tss::MIPFILTER] = 2;
                }
                else if (v == 3)
                {
                    tex_stage[tss::MIPFILTER] = 3;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x0C:
            case 0x10:
            case 0x11:
            case 0x31:
            case 0x32:
            case 0x33:
                return 0;

            case 0x0D:
            {
                const std::uint32_t address = v == 0 ? 3u : (v == 1 ? 1u : (v == 2 ? 2u : 0u));

                if (address == 0)
                {
                    return 0;
                }

                tex_stage[tss::ADDRESSU] = address;
                tex_stage[tss::ADDRESSV] = address;
                break;
            }

            case 0x0E:
                if (v == 0 || (v & 0xFFFFF000u) != 0)
                {
                    render[rs::FOGDENSITY] = v;
                }
                else
                {
                    render[rs::FOGDENSITY] = AsBits( 1.0f / static_cast<float>(v) );
                }
                break;

            case 0x0F:
                render[rs::FOGCOLOR] = v & 0xFFFFFF;
                break;

            case 0x12:
                m_Windowed = v != 0;
                break;

            case 0x13:
                if (value == 0)
                {
                    m_Callbacks.fill( 0 );
                }
                else
                {
                    std::memcpy( m_Callbacks.data(), reinterpret_cast<const void *>(value), sizeof( m_Callbacks ) );
                }
                break;

            case 0x14:
            case 0x69:
                if (value != 0)
                {
                    // Linear interpolation of the game table over z, stored inverted as in dx7z
                    const auto * table = reinterpret_cast<const std::uint8_t *>(value);

                    for (int i = 0; i < 256; ++i)
                    {
                        m_FogTable[static_cast<std::size_t>( i )] = static_cast<std::uint8_t>( 255 - table[i] );
                    }

                    render[rs::FOGTABLEMODE] = 0;
                    m_FogTableActive = true;
                }
                break;

            case 0x15:
                if (v == 4 || v == 8 || v == 2)
                {
                    m_FogTableActive = false;
                    render[rs::FOGTABLEMODE] = v == 4 ? 1u : (v == 8 ? 2u : 3u);
                    render[rs::FOGVERTEXMODE] = 0;

                    if (v == 2)
                    {
                        render[rs::FOGSTART] = AsBits( 0.0f );
                        render[rs::FOGEND] = AsBits( 1.0f );
                    }
                }
                else if (v == 0)
                {
                    render[rs::FOGENABLE] = 0;
                }
                else if (v == 1)
                {
                    render[rs::FOGENABLE] = 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x16:
            case 0x17:
                break;

            case 0x18:
            case 0x66:
                m_DepthBias = static_cast<float>(static_cast<std::int32_t>(v)) * DEPTH_BIAS_UNIT;
                break;

            case 0x19:
                m_Window = reinterpret_cast<HWND>(value);
                break;

            case 0x23:
                result = 1;
                break;

            case 0x24:
                if (v == 0)
                {
                    render[rs::ALPHATESTENABLE] = 0;
                    render[rs::ALPHAFUNC] = 8;
                }
                else
                {
                    const std::intptr_t func = GetState( 0x40 );
                    render[rs::ALPHATESTENABLE] = 1;
                    render[rs::ALPHAFUNC] = m_StateStore.count( 0x40 ) != 0 ? static_cast<std::uint32_t>(func) + 1 : 5u;
                    render[rs::ALPHAREF] = v;
                }
                break;

            case 0x25:
            case 0x26:
            case 0x3F:
            case 0x193:
                // No callback timer, no DirectDraw object, no guard band, no D3D7 caps.
                return 0;

            case 0x28:
                if (v > 7)
                {
                    return 1;
                }
                render[rs::ZFUNC] = v + 1;
                break;

            case 0x29:
                SetTextureStageMode( stage, v );
                break;

            case 0x2A:
                m_TwoUvSets = v != 0;
                break;

            case 0x2E:
            case 0x65:
            {
                float brightness = AsFloat( v );

                if (!(brightness > 0.0f))
                    brightness = 0.0f;

                m_Brightness = std::min( brightness, 4.0f );
                break;
            }

            case 0x2F:
                render[rs::STENCILENABLE] = v != 0;
                break;

            case 0x30:
                if (v > 7)
                    return 1;

                render[rs::STENCILFUNC] = v + 1;
                break;

            case 0x34:
            case 0x35:
            case 0x36:
                if (v > 8 || v == 6)
                    return 1;

                render[state == 0x34 ? rs::STENCILFAIL : (state == 0x35 ? rs::STENCILZFAIL : rs::STENCILPASS)] = v + 1;
                break;

            case 0x38:
            case 0x68:
                if (v == 0)
                {
                    render[rs::SRCBLEND] = 5;
                    render[rs::DESTBLEND] = 6;
                }
                else if (v == 1)
                {
                    render[rs::SRCBLEND] = 5;
                    render[rs::DESTBLEND] = 2;
                }
                else if (v == 2)
                {
                    render[rs::SRCBLEND] = 1;
                    render[rs::DESTBLEND] = 6;
                }
                else if (v == 3)
                {
                    render[rs::SRCBLEND] = 9;
                    render[rs::DESTBLEND] = 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x39:
            case 0x6A:
                render[rs::ZWRITEENABLE] = v != 0;
                break;

            case 0x3B:
                m_SelectedWindow = static_cast<int>(v);
                break;

            case 0x3C:
            case 0x67:
                m_WaitVblank = v != 0;
                break;

            case 0x40:
                if (v > 7)
                {
                    return 1;
                }
                render[rs::ALPHAFUNC] = v + 1;
                break;

            case 0x41:
            case 0x42:
            {
                const std::uint32_t address = v == 0 ? 3u : (v == 1 ? 1u : (v == 2 ? 2u : 0u));

                if (address == 0)
                {
                    return 0;
                }
                tex_stage[state == 0x41 ? tss::ADDRESSU : tss::ADDRESSV] = address;
                break;
            }

            case 0x43:
                m_ClearDepthValue = std::clamp( AsFloat( v ), 0.0f, 1.0f );
                break;

            case 0x194:
            case 0x195:
            {
                static constexpr std::uint32_t BLENDS[10] = { 2, 1, 5, 6, 7, 8, 3, 9, 4, 10 };

                if (v < 10)
                {
                    render[state == 0x194 ? rs::SRCBLEND : rs::DESTBLEND] = BLENDS[v];
                }
                break;
            }

            case 0x19A:
                render[rs::TEXTUREFACTOR] = v;
                break;

            case 0x1B3:
                DrawRaw( reinterpret_cast<const std::uint32_t *>(value) );
                return 1;

            case 0x1C3:
                // Modulated texture alpha is supported.
                return 1;

            default:
                // Values only stored for 'getstate', like in dx7z [6000A430].
                break;
        }

        StoreState( id, v );

        // States that write the same values as this one no longer know what is in effect.
        ForgetRelatedStates( state );

        if (cached)
        {
            m_StateCacheValue[cache_slot] = v;
            m_StateCacheGeneration[cache_slot] = m_StateGeneration;
        }

        return result;
    }


    void Driver::ForgetRelatedStates(
        std::uint32_t state
    )
    {
        // Groups of states that write the same pipeline values.
        static constexpr std::uint32_t BLEND[] = { 0x38, 0x68, 0x194, 0x195 };
        static constexpr std::uint32_t DEPTH[] = { 0x04, 0x28, 0x39, 0x6A };
        static constexpr std::uint32_t BIAS[] = { 0x18, 0x66 };
        static constexpr std::uint32_t ADDRESS[] = { 0x0D, 0x41, 0x42 };
        static constexpr std::uint32_t ALPHA[] = { 0x24, 0x40 };
        static constexpr std::uint32_t FOG[] = { 0x14, 0x15, 0x69 };

        const auto forget = [&] ( const auto & group )
        {
            if (std::find( std::begin( group ), std::end( group ), state ) == std::end( group ))
                return;

            for (const std::uint32_t other : group)
            {
                if (other == state)
                    continue;

                for (std::size_t stage = 0; stage < 8; ++stage)
                {
                    m_StateCacheGeneration[static_cast<std::size_t>(other) * 8 + stage] = 0;
                }
            }
        };

        forget( BLEND );
        forget( DEPTH );
        forget( BIAS );
        forget( ADDRESS );
        forget( ALPHA );
        forget( FOG );
    }


    bool Driver::IsCachedState(
        std::uint32_t state
    )
    {
        // Only states that do nothing but set pipeline values from their own argument.
        switch (state)
        {
            case 0x02:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0D:
            case 0x0E:
            case 0x0F:
            case 0x15:
            case 0x18:
            case 0x24:
            case 0x28:
            case 0x29:
            case 0x2A:
            case 0x2F:
            case 0x30:
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x38:
            case 0x39:
            case 0x40:
            case 0x41:
            case 0x42:
            case 0x66:
            case 0x68:
            case 0x6A:
            case 0x194:
            case 0x195:
            case 0x19A:
                return true;

            default:
                return false;
        }
    }


    bool Driver::IsStageState(
        std::uint32_t state
    )
    {
        switch (state)
        {
            case 0x07:
            case 0x08:
            case 0x0B:
            case 0x0D:
            case 0x29:
            case 0x41:
            case 0x42:
                return true;

            default:
                return false;
        }
    }


    void Driver::ResetStateCache()
    {
        if (++m_StateGeneration == 0)
        {
            m_StateCacheGeneration.fill( 0 );
            m_StateGeneration = 1;
        }
    }


    void Driver::InvalidatePipeline()
    {
        m_SceneBound = false;
        m_ConstantsValid = false;
        m_BoundBlend = nullptr;
        m_BoundDepth = nullptr;
        m_BoundStencilRef = 0;
        m_BoundRaster = nullptr;
        m_ScissorValid = false;
        m_BoundTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        m_BoundViews.fill( nullptr );
        m_BoundSamplers.fill( nullptr );

        // The cached objects may have been released together with their maps.
        m_SamplerCache.fill( nullptr );
        m_BlendCache = nullptr;
        m_DepthCache = nullptr;
        m_RasterCache = nullptr;
    }

    // Drawing.

    void Driver::BeginBatch(
        Primitive primitive,
        std::size_t vertices,
        std::size_t indices
    )
    {
        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        if (m_BatchPrimitive != primitive || m_BatchVertices.size() + vertices > BATCH_VERTICES ||
            m_BatchIndices.size() + indices > BATCH_INDICES)
        {
            Flush();
        }

        m_BatchPrimitive = primitive;
    }


    void Driver::PushVertex(
        const std::uint8_t * source
    )
    {
        TlVertex vertex = {};
        std::memcpy( &vertex, source, VertexStride() );
        vertex.m_Z += m_DepthBias;

        if (m_WhiteDiffuse)
        {
            vertex.m_Diffuse = 0xFFFFFFFF;
        }
        if (m_FogTableActive)
        {
            const int index = std::clamp( static_cast<int>(std::lround( vertex.m_Z * 255.0f )), 0, 255 );
            vertex.m_Specular = static_cast<std::uint32_t>(m_FogTable[static_cast<std::size_t>(index)]) << 24;
        }

        m_BatchVertices.push_back( vertex );
    }


    void Driver::DrawTriangles(
        const std::uint8_t * vertices,
        const int * indices,
        int count,
        int stride_indices
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::TRIANGLES, 3, 3 );
            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );

            for (int corner = 0; corner < 3; ++corner)
            {
                const int index = indices != nullptr ? indices[i * stride_indices + corner] : i * 3 + corner;
                PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
                m_BatchIndices.push_back( static_cast<std::uint16_t>( base + corner ) );
            }
        }
    }


    void Driver::DrawPolygon(
        const std::uint8_t * const * corners,
        int count
    )
    {
        if (corners == nullptr || (count != 3 && count != 4))
            return;

        for (int corner = 0; corner < count; ++corner)
        {
            if (corners[corner] == nullptr)
            {
                return;
            }
        }

        BeginBatch( Primitive::TRIANGLES, static_cast<std::size_t>( count ), count == 4 ? 6 : 3 );
        const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );

        for (int corner = 0; corner < count; ++corner)
        {
            PushVertex( corners[corner] );
        }

        static constexpr std::uint16_t QUAD[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < (count == 4 ? 6 : 3); ++i)
        {
            m_BatchIndices.push_back( static_cast<std::uint16_t>( base + QUAD[i] ) );
        }
    }


    void Driver::DrawQuads(
        const std::uint8_t * vertices,
        const int * indices,
        int count
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::TRIANGLES, 4, 6 );
            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );

            for (int corner = 0; corner < 4; ++corner)
            {
                const int index = indices != nullptr ? indices[i * 4 + corner] : i * 4 + corner;
                PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
            }
            for (const int corner : { 0, 1, 2, 0, 2, 3 })
            {
                m_BatchIndices.push_back( static_cast<std::uint16_t>( base + corner ) );
            }
        }
    }


    void Driver::DrawStrip(
        const std::uint8_t * vertices,
        const int * indices,
        int triangles,
        bool fan
    )
    {
        if (vertices == nullptr || triangles <= 0)
        {
            return;
        }
        const std::uint32_t stride = VertexStride();
        const int count = triangles + 2;

        if (static_cast<std::size_t>(count) > BATCH_VERTICES)
            return;

        // Strips and fans bypass the per vertex processing in dx7z.
        const float bias = m_DepthBias;
        const bool white = m_WhiteDiffuse;
        const bool table = m_FogTableActive;

        m_DepthBias = 0.0f;
        m_WhiteDiffuse = false;
        m_FogTableActive = false;

        BeginBatch( Primitive::TRIANGLES, static_cast<std::size_t>(count), static_cast<std::size_t>(triangles) * 3 );
        const auto base = static_cast<std::uint16_t>(m_BatchVertices.size());

        for (int i = 0; i < count; ++i)
        {
            const int index = indices != nullptr ? indices[i] : i;
            PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
        }

        for (int i = 0; i < triangles; ++i)
        {
            std::uint16_t a;
            std::uint16_t b;
            std::uint16_t c;

            if (fan)
            {
                a = 0;
                b = static_cast<std::uint16_t>( i + 1 );
                c = static_cast<std::uint16_t>( i + 2 );
            }
            else if ((i & 1) == 0)
            {
                a = static_cast<std::uint16_t>(i);
                b = static_cast<std::uint16_t>(i + 1);
                c = static_cast<std::uint16_t>(i + 2);
            }
            else
            {
                a = static_cast<std::uint16_t>(i + 1);
                b = static_cast<std::uint16_t>(i);
                c = static_cast<std::uint16_t>(i + 2);
            }

            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + a) );
            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + b) );
            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + c) );
        }

        m_DepthBias = bias;
        m_WhiteDiffuse = white;
        m_FogTableActive = table;
    }


    void Driver::DrawLines(
        const std::uint8_t * vertices,
        const int * indices,
        int count,
        bool strip
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::LINES, 2, 2 );

            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );
            int a;
            int b;

            if (strip)
            {
                a = indices != nullptr ? indices[i] : i;
                b = indices != nullptr ? indices[i + 1] : i + 1;
            }
            else
            {
                a = indices != nullptr ? indices[i * 2] : i * 2;
                b = indices != nullptr ? indices[i * 2 + 1] : i * 2 + 1;
            }

            PushVertex( vertices + static_cast<std::size_t>(a) * stride );
            PushVertex( vertices + static_cast<std::size_t>(b) * stride );

            m_BatchIndices.push_back( base );
            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + 1) );
        }
    }


    void Driver::DrawPoints(
        const std::uint8_t * vertices,
        const int * indices,
        int count
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::POINTS, 1, 1 );

            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );
            const int index = indices != nullptr ? indices[i] : i;
            PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
            m_BatchIndices.push_back( base );
        }
    }


    void Driver::DrawRaw(
        const std::uint32_t * description
    )
    {
        if (description == nullptr || !m_FrameView)
            return;

        Flush();

        if (m_Locked)
            UnlockWindow( nullptr );

        // { primitive, fvf, vertices, vertex count (u16), indices, index count (u16) }.
        const std::uint32_t primitive = description[0];
        const std::uint32_t fvf = description[1];
        const auto * vertices = reinterpret_cast<const std::uint8_t *>(description[2]);
        const std::size_t vertex_count = description[3] & 0xFFFF;
        const auto * indices = reinterpret_cast<const std::uint16_t *>(description[4]);
        const std::size_t index_count = description[5] & 0xFFFF;
        const std::size_t stride = (fvf & 0xF00) == 0x200 ? 40 : 32;

        if (vertices == nullptr || vertex_count == 0 || vertex_count > RAW_VERTICES)
        {
            return;
        }

        // Expand strips and fans into lists so one topology per primitive class suffices.
        std::vector<std::uint16_t> & list = m_BatchIndices;
        list.clear();

        const std::size_t count = indices != nullptr ? index_count : vertex_count;
        auto at = [&] ( std::size_t i ) -> std::uint16_t
        {
            return indices != nullptr ? indices[i] : static_cast<std::uint16_t>(i);
        };

        Primitive kind = Primitive::TRIANGLES;
        switch (primitive)
        {
            case 1:
                kind = Primitive::POINTS;

                for (std::size_t i = 0; i < count; ++i)
                {
                    list.push_back( at( i ) );
                }
                break;

            case 2:
                kind = Primitive::LINES;

                for (std::size_t i = 0; i + 1 < count; i += 2)
                {
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                }
                break;

            case 3:
                kind = Primitive::LINES;

                for (std::size_t i = 0; i + 1 < count; ++i)
                {
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                }
                break;

            case 4:
                for (std::size_t i = 0; i + 2 < count; i += 3)
                {
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                    list.push_back( at( i + 2 ) );
                }
                break;

            case 5:
                for (std::size_t i = 0; i + 2 < count; ++i)
                {
                    const bool odd = (i & 1) != 0;
                    list.push_back( at( odd ? i + 1 : i ) );
                    list.push_back( at( odd ? i : i + 1 ) );
                    list.push_back( at( i + 2 ) );
                }
                break;

            case 6:
                for (std::size_t i = 1; i + 1 < count; ++i)
                {
                    list.push_back( at( 0 ) );
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                }
                break;

            default:
                list.clear();
                return;
        }

        if (list.empty() || list.size() > RAW_INDICES)
        {
            list.clear();
            return;
        }

        m_BatchVertices.resize( vertex_count );

        for (std::size_t i = 0; i < vertex_count; ++i)
        {
            TlVertex vertex = {};
            std::memcpy( &vertex, vertices + i * stride, stride );
            m_BatchVertices[i] = vertex;
        }

        m_BatchPrimitive = kind;
        Flush();
    }


    ID3D11SamplerState * Driver::GetSampler(
        int stage
    )
    {
        const auto & s = m_D3D.m_Stage[static_cast<std::size_t>(stage)];

        const std::uint64_t key = static_cast<std::uint64_t>(s[tss::MAGFILTER] & 7) | (static_cast<std::uint64_t>(s[tss::MINFILTER] & 7) << 3) |
            (static_cast<std::uint64_t>(s[tss::MIPFILTER] & 3) << 6) | (static_cast<std::uint64_t>(s[tss::ADDRESSU] & 7) << 8) |
            (static_cast<std::uint64_t>(s[tss::ADDRESSV] & 7) << 11) | (static_cast<std::uint64_t>(s[tss::MIPMAPLODBIAS]) << 32);

        const std::size_t index = static_cast<std::size_t>(stage);

        if (m_SamplerCache[index] != nullptr && m_SamplerKeys[index] == key)
        {
            return m_SamplerCache[index];
        }

        auto & cached = m_Samplers[key];

        if (!cached)
        {
            const bool anisotropic = s[tss::MAGFILTER] == 5 || s[tss::MINFILTER] == 3 || (m_ConfigAnisotropy > 1 && s[tss::MINFILTER] >= 2);
            const bool mag_linear = s[tss::MAGFILTER] >= 2;
            const bool min_linear = s[tss::MINFILTER] >= 2;
            const std::uint32_t mip = s[tss::MIPFILTER];

            D3D11_SAMPLER_DESC desc = {};
            if (anisotropic)
            {
                desc.Filter = D3D11_FILTER_ANISOTROPIC;
                desc.MaxAnisotropy = m_ConfigAnisotropy > 1 ? static_cast<UINT>(std::min( m_ConfigAnisotropy, 16 )) : 16u;
            }
            else
            {
                int filter = 0;

                if (mip == 3)
                {
                    filter |= 0x1;
                }

                if (mag_linear)
                {
                    filter |= 0x4;
                }

                if (min_linear)
                {
                    filter |= 0x10;
                }

                desc.Filter = static_cast<D3D11_FILTER>(filter);
                desc.MaxAnisotropy = 1;
            }

            desc.AddressU = ToAddress( s[tss::ADDRESSU] );
            desc.AddressV = ToAddress( s[tss::ADDRESSV] );
            desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.MipLODBias = AsFloat( s[tss::MIPMAPLODBIAS] );
            desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            desc.MinLOD = 0.0f;

            // Mip filter none samples the top level only.
            desc.MaxLOD = mip <= 1 ? 0.0f : D3D11_FLOAT32_MAX;
            m_Device->CreateSamplerState( &desc, cached.Put() );
        }

        m_SamplerKeys[index] = key;
        m_SamplerCache[index] = cached.Get();
        return cached.Get();
    }

    void Driver::ApplyPipeline()
    {
        const auto & render = m_D3D.m_Render;

        // Constants, uploaded only when they differ from the last upload.
        DrawConstants constants = {};
        constants.m_Target[0] = static_cast<float>(m_Width);
        constants.m_Target[1] = static_cast<float>(m_Height);
        constants.m_Target[2] = 1.0f / static_cast<float>(std::max( m_Width, 1 ));
        constants.m_Target[3] = 1.0f / static_cast<float>(std::max( m_Height, 1 ));

        for (int stage = 0; stage < MAX_STAGES; ++stage)
        {
            const auto & s = m_D3D.m_Stage[static_cast<std::size_t>( stage )];

            constants.m_ColorOp[stage][0] = static_cast<std::int32_t>( s[tss::COLOROP] );
            constants.m_ColorOp[stage][1] = static_cast<std::int32_t>( s[tss::COLORARG1] );
            constants.m_ColorOp[stage][2] = static_cast<std::int32_t>( s[tss::COLORARG2] );
            constants.m_AlphaOp[stage][0] = static_cast<std::int32_t>( s[tss::ALPHAOP] );
            constants.m_AlphaOp[stage][1] = static_cast<std::int32_t>( s[tss::ALPHAARG1] );
            constants.m_AlphaOp[stage][2] = static_cast<std::int32_t>( s[tss::ALPHAARG2] );
        }

        ColorToFloat4( render[rs::TEXTUREFACTOR], constants.m_Factor );
        ColorToFloat4( render[rs::FOGCOLOR] | 0xFF000000u, constants.m_FogColor );

        constants.m_FogParams[0] = AsFloat( render[rs::FOGSTART] );
        constants.m_FogParams[1] = AsFloat( render[rs::FOGEND] );
        constants.m_FogParams[2] = AsFloat( render[rs::FOGDENSITY] );
        constants.m_FogParams[3] = static_cast<float>(render[rs::FOGTABLEMODE]);
        constants.m_Flags[0] = m_ConfigFogEnabled && render[rs::FOGENABLE] != 0;
        constants.m_Flags[1] = render[rs::SPECULARENABLE] != 0;
        constants.m_Flags[2] = render[rs::ALPHATESTENABLE] != 0 ? static_cast<std::int32_t>(render[rs::ALPHAFUNC]) : 8;
        constants.m_Flags[3] = (m_Bound[0] != nullptr ? 1 : 0) | (m_Bound[1] != nullptr ? 2 : 0);
        constants.m_AlphaRef[0] = static_cast<float>(render[rs::ALPHAREF] & 0xFF) / 255.0f;

        if (!m_ConstantsValid || std::memcmp( &constants, &m_LastConstants, sizeof( constants ) ) != 0)
        {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED( m_Context->Map( m_DrawConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            {
                std::memcpy( mapped.pData, &constants, sizeof( constants ) );
                m_Context->Unmap( m_DrawConstants.Get(), 0 );
                m_LastConstants = constants;
                m_ConstantsValid = true;
            }
        }

        // Blend.
        const std::uint64_t blend_key = static_cast<std::uint64_t>(render[rs::ALPHABLENDENABLE] != 0) |
            (static_cast<std::uint64_t>(render[rs::SRCBLEND] & 0xFF) << 8) |
            (static_cast<std::uint64_t>(render[rs::DESTBLEND] & 0xFF) << 16);

        if (m_BlendCache == nullptr || m_BlendKey != blend_key)
        {
            auto & blend = m_Blends[blend_key];

            if (!blend)
            {
                D3D11_BLEND_DESC desc = {};
                auto & target = desc.RenderTarget[0];
                std::uint32_t source_blend = render[rs::SRCBLEND];
                std::uint32_t destination_blend = render[rs::DESTBLEND];

                // BOTHSRCALPHA and BOTHINVSRCALPHA set both factors
                if (source_blend == 12)
                {
                    source_blend = 5;
                    destination_blend = 6;
                }
                else if (source_blend == 13)
                {
                    source_blend = 6;
                    destination_blend = 5;
                }

                target.BlendEnable = render[rs::ALPHABLENDENABLE] != 0;
                target.SrcBlend = ToBlend( source_blend, false );
                target.DestBlend = ToBlend( destination_blend, false );
                target.BlendOp = D3D11_BLEND_OP_ADD;
                target.SrcBlendAlpha = ToBlend( source_blend, true );
                target.DestBlendAlpha = ToBlend( destination_blend, true );
                target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                m_Device->CreateBlendState( &desc, blend.Put() );
            }

            m_BlendKey = blend_key;
            m_BlendCache = blend.Get();
        }

        if (m_BoundBlend != m_BlendCache)
        {
            const float factor[4] = {};
            m_Context->OMSetBlendState( m_BlendCache, factor, 0xFFFFFFFF );
            m_BoundBlend = m_BlendCache;
        }

        // Depth and stencil.
        const bool depth_enable = render[rs::ZENABLE] != 0;
        const std::uint64_t depth_key = static_cast<std::uint64_t>(depth_enable) | (static_cast<std::uint64_t>(render[rs::ZWRITEENABLE] != 0) << 1) |
            (static_cast<std::uint64_t>(render[rs::ZFUNC] & 15) << 2) |
            (static_cast<std::uint64_t>(render[rs::STENCILENABLE] != 0) << 6) |
            (static_cast<std::uint64_t>(render[rs::STENCILFUNC] & 15) << 7) |
            (static_cast<std::uint64_t>(render[rs::STENCILFAIL] & 15) << 11) |
            (static_cast<std::uint64_t>(render[rs::STENCILZFAIL] & 15) << 15) |
            (static_cast<std::uint64_t>(render[rs::STENCILPASS] & 15) << 19) |
            (static_cast<std::uint64_t>(render[rs::STENCILMASK] & 0xFF) << 23) |
            (static_cast<std::uint64_t>(render[rs::STENCILWRITEMASK] & 0xFF) << 31);

        if (m_DepthCache == nullptr || m_DepthKey != depth_key)
        {
            auto & depth = m_DepthStates[depth_key];

            if (!depth)
            {
                D3D11_DEPTH_STENCIL_DESC desc = {};
                desc.DepthEnable = depth_enable;
                desc.DepthWriteMask = depth_enable && render[rs::ZWRITEENABLE] != 0 ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.DepthFunc = ToCompare( render[rs::ZFUNC] );
                desc.StencilEnable = render[rs::STENCILENABLE] != 0;
                desc.StencilReadMask = static_cast<UINT8>(render[rs::STENCILMASK] & 0xFF);
                desc.StencilWriteMask = static_cast<UINT8>(render[rs::STENCILWRITEMASK] & 0xFF);
                desc.FrontFace.StencilFunc = ToCompare( render[rs::STENCILFUNC] );
                desc.FrontFace.StencilFailOp = ToStencilOp( render[rs::STENCILFAIL] );
                desc.FrontFace.StencilDepthFailOp = ToStencilOp( render[rs::STENCILZFAIL] );
                desc.FrontFace.StencilPassOp = ToStencilOp( render[rs::STENCILPASS] );
                desc.BackFace = desc.FrontFace;
                m_Device->CreateDepthStencilState( &desc, depth.Put() );
            }

            m_DepthKey = depth_key;
            m_DepthCache = depth.Get();
        }

        const UINT stencil_ref = render[rs::STENCILREF] & 0xFF;

        if (m_BoundDepth != m_DepthCache || m_BoundStencilRef != stencil_ref)
        {
            m_Context->OMSetDepthStencilState( m_DepthCache, stencil_ref );
            m_BoundDepth = m_DepthCache;
            m_BoundStencilRef = stencil_ref;
        }

        // Rasterizer, D3D7 culls by screen winding like D3D11 with clockwise front faces.
        // Projected headlight overlays share depth with the underlying road geometry.
        const bool projected_candidate = render[rs::ZENABLE] != 0 &&
            render[rs::ZWRITEENABLE] != 0 && render[rs::ALPHABLENDENABLE] != 0 &&
            render[rs::ZFUNC] == 4 && render[rs::STENCILENABLE] == 0 &&
            m_Bound[0] != nullptr && m_Bound[1] == nullptr &&
            m_Bound[0]->m_Width == 64 && m_Bound[0]->m_Height == 64;
        const std::uint64_t raster_key = static_cast<std::uint64_t>(render[rs::CULLMODE] & 3) |
            (static_cast<std::uint64_t>(render[rs::FILLMODE] == 2) << 2) |
            (static_cast<std::uint64_t>(projected_candidate) << 3);
        if (m_RasterCache == nullptr || m_RasterKey != raster_key)
        {
            auto & raster = m_Rasters[raster_key];

            if (!raster)
            {
                D3D11_RASTERIZER_DESC desc = {};
                desc.FillMode = render[rs::FILLMODE] == 2 ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
                const std::uint32_t cull = render[rs::CULLMODE];
                desc.CullMode = cull == 2 ? D3D11_CULL_FRONT : (cull == 3 ? D3D11_CULL_BACK : D3D11_CULL_NONE);
                desc.FrontCounterClockwise = FALSE;
                desc.DepthClipEnable = TRUE;
                desc.DepthBias = projected_candidate ? -8 : 0;
                desc.ScissorEnable = TRUE;
                m_Device->CreateRasterizerState( &desc, raster.Put() );
            }

            m_RasterKey = raster_key;
            m_RasterCache = raster.Get();
        }

        if (m_BoundRaster != m_RasterCache)
        {
            m_Context->RSSetState( m_RasterCache );
            m_BoundRaster = m_RasterCache;
        }

        // Targets, shaders, buffers and layout, the same for every scene draw.
        if (!m_SceneBound)
        {
            D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight), 0, 1 };
            m_Context->RSSetViewports( 1, &viewport );

            ID3D11RenderTargetView * target = m_FrameView.Get();
            m_Context->OMSetRenderTargets( 1, &target, m_DepthView.Get() );

            m_Context->IASetInputLayout( m_SceneLayout.Get() );
            UINT stride = sizeof( TlVertex );
            UINT offset = 0;
            ID3D11Buffer * vertex_buffer = m_VertexBuffer.Get();
            m_Context->IASetVertexBuffers( 0, 1, &vertex_buffer, &stride, &offset );
            m_Context->IASetIndexBuffer( m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0 );
            m_Context->VSSetShader( m_SceneVs.Get(), nullptr, 0 );
            m_Context->PSSetShader( m_ScenePs.Get(), nullptr, 0 );
            ID3D11Buffer * constant_buffer = m_DrawConstants.Get();
            m_Context->VSSetConstantBuffers( 0, 1, &constant_buffer );
            m_Context->PSSetConstantBuffers( 0, 1, &constant_buffer );
            m_SceneBound = true;
        }

        const D3D11_RECT scissor = ScaledClip();

        if (!m_ScissorValid || std::memcmp( &scissor, &m_BoundScissor, sizeof( scissor ) ) != 0)
        {
            m_Context->RSSetScissorRects( 1, &scissor );
            m_BoundScissor = scissor;
            m_ScissorValid = true;
        }

        std::array<ID3D11ShaderResourceView *, MAX_STAGES> views = {};
        std::array<ID3D11SamplerState *, MAX_STAGES> samplers = {};

        for (int stage = 0; stage < MAX_STAGES; ++stage)
        {
            const auto index = static_cast<std::size_t>( stage );

            if (m_Bound[index] != nullptr)
            {
                views[index] = m_Bound[index]->m_View.Get();
            }

            samplers[index] = GetSampler( stage );
        }

        if (views != m_BoundViews)
        {
            m_Context->PSSetShaderResources( 0, MAX_STAGES, views.data() );
            m_BoundViews = views;
        }

        if (samplers != m_BoundSamplers)
        {
            m_Context->PSSetSamplers( 0, MAX_STAGES, samplers.data() );
            m_BoundSamplers = samplers;
        }
    }


    void Driver::Flush()
    {
        if (m_BatchIndices.empty() || m_BatchVertices.empty() || !m_FrameView || !m_VertexBuffer || !m_IndexBuffer)
        {
            m_BatchVertices.clear();
            m_BatchIndices.clear();
            return;
        }

        const std::size_t vertex_count = m_BatchVertices.size();
        const std::size_t index_count = m_BatchIndices.size();

        if (vertex_count > RAW_VERTICES || index_count > RAW_INDICES)
        {
            m_BatchVertices.clear();
            m_BatchIndices.clear();
            m_BatchPrimitive = Primitive::NONE;
            return;
        }

        ApplyPipeline();

        // Append behind the data of earlier draws, start over with a discard once full.
        D3D11_MAP map = D3D11_MAP_WRITE_NO_OVERWRITE;

        if (m_VertexCursor + vertex_count > RAW_VERTICES || m_IndexCursor + index_count > RAW_INDICES)
        {
            map = D3D11_MAP_WRITE_DISCARD;
            m_VertexCursor = 0;
            m_IndexCursor = 0;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_VertexBuffer.Get(), 0, map, 0, &mapped ) ))
        {
            m_BatchVertices.clear();
            m_BatchIndices.clear();
            return;
        }

        std::memcpy( static_cast<TlVertex *>(mapped.pData) + m_VertexCursor, m_BatchVertices.data(), vertex_count * sizeof( TlVertex ) );
        m_Context->Unmap( m_VertexBuffer.Get(), 0 );

        if (FAILED( m_Context->Map( m_IndexBuffer.Get(), 0, map, 0, &mapped ) ))
        {
            m_BatchVertices.clear();
            m_BatchIndices.clear();
            return;
        }

        std::memcpy( static_cast<std::uint16_t *>(mapped.pData) + m_IndexCursor, m_BatchIndices.data(), index_count * sizeof( std::uint16_t ) );
        m_Context->Unmap( m_IndexBuffer.Get(), 0 );

        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        if (m_BatchPrimitive == Primitive::LINES)
        {
            topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        }
        else if (m_BatchPrimitive == Primitive::POINTS)
        {
            topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        }

        if (topology != m_BoundTopology)
        {
            m_Context->IASetPrimitiveTopology( topology );
            m_BoundTopology = topology;
        }

        m_Context->DrawIndexed( static_cast<UINT>(index_count), static_cast<UINT>(m_IndexCursor), static_cast<INT>(m_VertexCursor) );

        m_VertexCursor += vertex_count;
        m_IndexCursor += index_count;

        m_BatchVertices.clear();
        m_BatchIndices.clear();
        m_BatchPrimitive = Primitive::NONE;
    }
}
