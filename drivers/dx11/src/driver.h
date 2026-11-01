#pragma once

// THRASH driver on Direct3D 11.
// Keeps the D3D7 state dx7z.dll would have set, batches the pre transformed vertices
// and draws them with a fixed function emulation shader into an off screen target at
// the game resolution, which is scaled into a flip model swap chain on pageflip.

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "d3d7_state.h"

namespace dx11z
{
    template <typename T>
    class ComPtr
    {
    private:

        T * m_Pointer = nullptr;

    public:

        ComPtr() = default;

        ComPtr( const ComPtr & other ) : m_Pointer( other.m_Pointer )
        {
            if (m_Pointer)
            {
                m_Pointer->AddRef();
            }
        }

        ComPtr & operator=( const ComPtr & other )
        {
            if (this != &other)
            {
                Reset();
                m_Pointer = other.m_Pointer;

                if (m_Pointer)
                {
                    m_Pointer->AddRef();
                }
            }

            return *this;
        }

        ~ComPtr()
        {
            Reset();
        }

        void Reset()
        {
            if (m_Pointer)
            {
                m_Pointer->Release();
                m_Pointer = nullptr;
            }
        }

        T * Get() const
        {
            return m_Pointer;
        }

        T ** Put()
        {
            Reset();
            return &m_Pointer;
        }

        T * operator->() const
        {
            return m_Pointer;
        }

        explicit operator bool() const
        {
            return m_Pointer != nullptr;
        }
    };

    // Lock description returned by 'lockwindow', layout fixed by the game.
    struct LockInfo
    {
        void * m_Bits;
        std::int32_t m_Pitch;
        std::int32_t m_Format;
        std::int32_t m_Width;
        std::int32_t m_Height;
    };

    // Mode list entry, 40 bytes, layout fixed by the game.
    struct ModeRecord
    {
        std::int32_t m_Width;
        std::int32_t m_Height;
        std::int32_t m_Bpp;
        std::int32_t m_Format;
        std::int32_t m_Valid;
        std::int32_t m_Buffers;
        std::int32_t m_Capability;
        std::int32_t m_Reserved[3];
    };

    // About block, 208 bytes, layout fixed by the game.
    struct AboutBlock
    {
        std::uint32_t m_Signature; // +00
        std::uint32_t m_Size; // +04
        std::uint32_t m_Version; // +08
        std::uint32_t m_Flags; // +0C
        std::int32_t m_TextureWidthMin; // +10
        std::int32_t m_TextureWidthMax; // +14
        std::int32_t m_TextureWidthMultiple;
        std::int32_t m_TextureHeightMin; // +1C
        std::int32_t m_TextureHeightMax; // +20
        std::int32_t m_TextureHeightMultiple;
        std::int32_t m_ClipAlign; // +28
        std::int32_t m_FormatCount; // +2C
        const std::int32_t * m_Formats; // +30
        std::int32_t m_PaletteFormatCount; // +34
        const std::int32_t * m_PaletteFormats;
        std::int32_t m_ModeCount; // +3C
        ModeRecord * m_Modes; // +40
        std::int32_t m_Stages; // +44
        std::int32_t m_Unknown48;
        std::uint32_t m_Constants[3]; // +4C
        std::uint32_t m_Unknown58[5];
        std::int32_t m_Subtype; // +6C
        std::uint32_t m_TextureMemory; // +70
        std::int32_t m_TextureMemoryType; // +74
        const char * m_Author; // +78
        std::int32_t m_DxVersion; // +7C
        char m_Name[80]; // +80
    };

    // Vertex as the game passes it, the second UV pair only with state 2A.
    struct TlVertex
    {
        float m_X;
        float m_Y;
        float m_Z;
        float m_Rhw;
        std::uint32_t m_Diffuse;
        std::uint32_t m_Specular;
        float m_U0;
        float m_V0;
        float m_U1;
        float m_V1;
    };

    // Constant buffer of the scene shader, layout matches 'shaders::SCENE'.
    struct DrawConstants
    {
        float m_Target[4];
        std::int32_t m_ColorOp[2][4];
        std::int32_t m_AlphaOp[2][4];
        float m_Factor[4];
        float m_FogColor[4];
        float m_FogParams[4];
        std::int32_t m_Flags[4];
        float m_AlphaRef[4];
    };

    struct Texture
    {
        std::uint32_t m_Magic = 0x58544433;
        std::int32_t m_Width = 0;
        std::int32_t m_Height = 0;
        std::int32_t m_Format = 0;
        std::int32_t m_Levels = 1;
        std::int32_t m_Stage = 0;
        ComPtr<ID3D11Texture2D> m_Texture;
        ComPtr<ID3D11ShaderResourceView> m_View;
    };

    class Driver
    {
    private:

        static constexpr std::size_t BATCH_VERTICES = 16384;
        static constexpr std::size_t BATCH_INDICES = BATCH_VERTICES * 3;
        static constexpr int STATE_SLOTS = 8;

        enum WindowMode
        {
            WINDOW_BORDERLESS = 0,
            WINDOW_FULLSCREEN = 1,
            WINDOW_WINDOWED = 2
        };

        enum class Primitive
        {
            NONE,
            TRIANGLES,
            LINES,
            POINTS
        };

        // Window and swap chain.
        HWND m_Window = nullptr;
        bool m_Windowed = false;
        HMODULE m_Compiler = nullptr;

        ComPtr<ID3D11Device> m_Device;
        ComPtr<ID3D11DeviceContext> m_Context;
        ComPtr<IDXGISwapChain1> m_SwapChain;
        ComPtr<ID3D11RenderTargetView> m_BackBufferView;
        std::int32_t m_BackBufferWidth = 0;
        std::int32_t m_BackBufferHeight = 0;

        // Off screen frame, rendered at a scaled resolution and with multisampling. The
        // game sees 'm_Width' by 'm_Height', locks and rectangle copies go through game sized
        // copies of the frame.
        ComPtr<ID3D11Texture2D> m_Frame;
        ComPtr<ID3D11RenderTargetView> m_FrameView;
        ComPtr<ID3D11Texture2D> m_Resolved;
        ComPtr<ID3D11ShaderResourceView> m_ResolvedView;
        ComPtr<ID3D11Texture2D> m_Depth;
        ComPtr<ID3D11DepthStencilView> m_DepthView;

        // Front buffer, window 1. It holds the last presented frame, the movie player
        // writes into it through locks and expects the picture to show at once.
        ComPtr<ID3D11Texture2D> m_Front;
        ComPtr<ID3D11RenderTargetView> m_FrontTarget;
        ComPtr<ID3D11ShaderResourceView> m_FrontView;
        ComPtr<ID3D11Texture2D> m_GameFrame;
        ComPtr<ID3D11RenderTargetView> m_GameFrameView;
        ComPtr<ID3D11Texture2D> m_Upload;
        ComPtr<ID3D11ShaderResourceView> m_UploadView;
        ComPtr<ID3D11Texture2D> m_Staging;
        std::int32_t m_Width = 0;
        std::int32_t m_Height = 0;
        std::int32_t m_RenderWidth = 0;
        std::int32_t m_RenderHeight = 0;
        float m_ScaleX = 1.0f;
        float m_ScaleY = 1.0f;
        UINT m_Samples = 1;
        bool m_Resolve = false;

        // Settings from [Dx11] in 'nfs5_enhanced.ini'.
        bool m_ConfigLoaded = false;
        int m_ConfigScale = 0;
        int m_ConfigSamples = 4;
        int m_ConfigAnisotropy = 0;
        int m_ConfigWindowMode = WINDOW_BORDERLESS;
        int m_ConfigWindowWidth = 0;
        int m_ConfigWindowHeight = 0;
        bool m_WindowPlaced = false;
        bool m_ConfigVsync = true;
        bool m_ConfigMinimize = true;
        bool m_ConfigFogEnabled = true;
        bool m_AllowTearing = false;
        UINT m_SwapChainFlags = 0;
        WNDPROC m_OriginalWndProc = nullptr;

        // Set while another program is active, the game then may not take the focus back.
        volatile bool m_Inactive = false;

        // Set on activation, exclusive fullscreen is entered again on the next page flip.
        volatile bool m_ReenterFullscreen = false;
        bool m_FocusPatched = false;
        HWND m_SubclassedWindow = nullptr;
        int m_TraceLines = 0;
        std::uint32_t m_FlipCount = 0;

        // Pipeline objects.
        ComPtr<ID3D11VertexShader> m_SceneVs;
        ComPtr<ID3D11PixelShader> m_ScenePs;
        ComPtr<ID3D11InputLayout> m_SceneLayout;
        ComPtr<ID3D11VertexShader> m_PresentVs;
        ComPtr<ID3D11PixelShader> m_PresentPs;
        ComPtr<ID3D11PixelShader> m_MoviePs;
        ComPtr<ID3D11VertexShader> m_ClearVs;
        ComPtr<ID3D11PixelShader> m_ClearPs;
        ComPtr<ID3D11Buffer> m_DrawConstants;
        ComPtr<ID3D11Buffer> m_PresentConstants;
        ComPtr<ID3D11Buffer> m_ClearConstants;
        ComPtr<ID3D11Buffer> m_VertexBuffer;
        ComPtr<ID3D11Buffer> m_IndexBuffer;
        ComPtr<ID3D11SamplerState> m_PresentSampler;
        ComPtr<ID3D11SamplerState> m_PointSampler;
        ComPtr<ID3D11BlendState> m_OpaqueBlend;
        ComPtr<ID3D11RasterizerState> m_PlainRaster;
        ComPtr<ID3D11DepthStencilState> m_ClearDepth;
        ComPtr<ID3D11DepthStencilState> m_ClearColorOnly;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11SamplerState>> m_Samplers;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11BlendState>> m_Blends;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11DepthStencilState>> m_DepthStates;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11RasterizerState>> m_Rasters;

        // Batch of converted vertices and 16 bit indices.
        std::vector<TlVertex> m_BatchVertices;
        std::vector<std::uint16_t> m_BatchIndices;
        Primitive m_BatchPrimitive = Primitive::NONE;

        // Write positions in the dynamic vertex and index buffers. Batches are appended with
        // 'NO_OVERWRITE' and the buffers are only discarded when they are full, a discard per
        // draw made the driver rename megabytes of memory thousands of times per frame.
        std::size_t m_VertexCursor = 0;
        std::size_t m_IndexCursor = 0;

        // Pipeline objects and values bound last, nothing is set again while it is unchanged.
        // Cleared by 'InvalidatePipeline' whenever another path rebinds the context.
        bool m_SceneBound = false;
        bool m_ConstantsValid = false;
        DrawConstants m_LastConstants = {};
        ID3D11BlendState * m_BoundBlend = nullptr;
        ID3D11DepthStencilState * m_BoundDepth = nullptr;
        UINT m_BoundStencilRef = 0;
        ID3D11RasterizerState * m_BoundRaster = nullptr;
        D3D11_RECT m_BoundScissor = {};
        bool m_ScissorValid = false;
        D3D11_PRIMITIVE_TOPOLOGY m_BoundTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        std::array<ID3D11ShaderResourceView *, MAX_STAGES> m_BoundViews = {};
        std::array<ID3D11SamplerState *, MAX_STAGES> m_BoundSamplers = {};
        std::array<std::uint64_t, MAX_STAGES> m_SamplerKeys = {};
        std::array<ID3D11SamplerState *, MAX_STAGES> m_SamplerCache = {};
        std::uint64_t m_BlendKey = ~0ull;
        ID3D11BlendState * m_BlendCache = nullptr;
        std::uint64_t m_DepthKey = ~0ull;
        ID3D11DepthStencilState * m_DepthCache = nullptr;
        std::uint64_t m_RasterKey = ~0ull;
        ID3D11RasterizerState * m_RasterCache = nullptr;
        ComPtr<ID3D11BlendState> m_ClearColorBlend;
        ComPtr<ID3D11BlendState> m_ClearDepthOnlyBlend;

        // Values of pipeline states as last applied, a repeated identical call is skipped
        // without ending the batch. The generation is advanced whenever 'm_D3D' is reset.
        static constexpr std::size_t STATE_CACHE_STATES = 0x200;
        std::array<std::uint32_t, STATE_CACHE_STATES * 8> m_StateCacheValue = {};
        std::array<std::uint32_t, STATE_CACHE_STATES * 8> m_StateCacheGeneration = {};
        std::uint32_t m_StateGeneration = 1;

        // Small rectangle reads, the sun glare probe, copy only the pixels they need from the
        // render sized frame instead of scaling the whole frame down to the game size first.
        struct ProbeRead
        {
            int m_X0 = 0;
            int m_Y0 = 0;
            int m_X1 = 0;
            int m_Y1 = 0;
            int m_X = 0;
            int m_Y = 0;
            int m_Width = 0;
            int m_RenderX0 = 0;
            int m_RenderY0 = 0;
            int m_RenderX1 = 0;
            int m_RenderY1 = 0;
            float m_ScaleX = 1.0f;
            float m_ScaleY = 1.0f;
            int m_FrameBits = 32;
            std::chrono::steady_clock::time_point m_Time = {};
            bool m_Valid = false;
        };

        static constexpr int PROBE_SIZE = 256;
        static constexpr std::chrono::milliseconds PROBE_REUSE_WINDOW { 1000 };
        std::array<ComPtr<ID3D11Texture2D>, 2> m_Probes;
        std::array<ProbeRead, 2> m_ProbeReads = {};
        std::size_t m_ProbeNext = 0;
        int m_SlowProbeLogs = 0;

        // THRASH and D3D7 state.
        D3D7State m_D3D;
        std::array<Texture *, MAX_STAGES> m_Bound = {};
        std::unordered_map<std::uint32_t, std::array<std::uint32_t, STATE_SLOTS>> m_StateStore;
        std::uint32_t m_BackgroundColor = 0;
        float m_ClearDepthValue = 1.0f;
        float m_DepthBias = 0.0f;
        bool m_WhiteDiffuse = false;
        bool m_TwoUvSets = false;
        bool m_FogTableActive = false;
        std::array<std::uint8_t, 256> m_FogTable = {};
        bool m_WaitVblank = true;
        float m_Brightness = 1.0f;

        // Movie frame enlarged on the graphics card, see 'MovieBlit'.
        ComPtr<ID3D11Texture2D> m_MovieTexture;
        ComPtr<ID3D11ShaderResourceView> m_MovieView;
        std::vector<std::uint8_t> m_MoviePixels;
        std::int32_t m_MovieWidth = 0;
        std::int32_t m_MovieHeight = 0;
        float m_MovieSharpness = 0.33f;
        bool m_ConfigMovie = true;

        // Movies that span the screen width fill the window, cutting the black bars of the frame
        // and keeping its aspect ratio. The bars are measured on the frames, the smallest
        // seen in a movie counts, a pause of half a second starts a new movie.
        bool m_ConfigMovieFill = true;
        std::int32_t m_MovieBarTop = -1;
        std::int32_t m_MovieBarBottom = -1;
        DWORD m_MovieTick = 0;
        bool m_MovieLogged = false;
        D3D11_RECT m_Clip = {};
        int m_SelectedWindow = 0;

        // Lock emulation.
        std::vector<std::uint8_t> m_LockSnapshot;
        std::vector<std::uint8_t> m_LockPixels;
        void * m_LockMapped = nullptr;
        std::uint32_t m_LockMappedPitch = 0;

        // Color depth the game sees through locks and rectangle copies, 16 is RGB565 like
        // dx7z in 16 bit modes, the software movie player needs it.
        int m_FrameBits = 32;
        LockInfo m_Lock = {};
        LockInfo m_FrameInfo = {};
        bool m_Locked = false;

        // Display information.
        AboutBlock m_About = {};
        std::vector<ModeRecord> m_Modes;
        std::array<std::int32_t, 16> m_FormatFlags = {};
        std::array<std::int32_t, 4> m_PaletteFlags = {};
        std::array<std::uint32_t, 8> m_Callbacks = {};
        std::string m_LogPath;

        // Live textures by handle, the game may hand back a freed handle, it is looked up
        // here instead of reading the freed object.
        std::unordered_map<Texture *, std::unique_ptr<Texture>> m_Textures;

        Driver() = default;

        void LoadConfig();

        void MakeDpiAware();

        void ReleaseFrame();

        void ReleaseAll();

        // Returns 'true' when the frame matches the game resolution and is single sampled.
        [[nodiscard]] bool IsDirect() const
        {
            return m_RenderWidth == m_Width && m_RenderHeight == m_Height && m_Samples == 1 && !IsFrontSelected();
        }

        void ComputeRenderSize();

        void ResolveFrame();

        // Scales the frame or the front buffer down into 'm_GameFrame'.
        void DownsampleFrame(
            bool front
        );

        // Shows the front buffer.
        void PresentFront();

        // Shows what is in the back buffer.
        void PresentBackBuffer();

        [[nodiscard]] bool IsFrontSelected() const
        {
            return m_SelectedWindow == 1;
        }

        // 'pixel_shader' replaces the plain copy, 'shape' goes to the fourth parameter.
        void Blit(
            ID3D11ShaderResourceView * source,
            ID3D11RenderTargetView * target,
            int target_width,
            int target_height,
            const float * destination_rect,
            const float * source_rect,
            float brightness,
            ID3D11SamplerState * sampler,
            ID3D11PixelShader * pixel_shader = nullptr,
            float shape = 0.0f
        );

        [[nodiscard]] D3D11_RECT ScaledClip() const;

        bool CreateDevice();

        bool CreateShaders();

        bool CreateSwapChain();

        bool CreateFrame(
            int width,
            int height
        );

        void BuildModeList();

        void FitWindow();

        // Exclusive fullscreen at the desktop resolution, also called to return after alt tab.
        void EnterFullscreen();

        // Watches activation of the game window for alt tab.
        void SubclassWindow();

        void RestoreWindowProc();

        // The game window code calls 'SetForegroundWindow' on 'WM_PAINT', after input errors and
        // in its message loop, which pulls it back in front right after Alt+Tab.
        void PatchFocusImports();

        static BOOL WINAPI SetForegroundWindowFilter(
            HWND window
        );

        static HWND WINAPI SetActiveWindowFilter(
            HWND window
        );

        static LRESULT CALLBACK WindowProc(
            HWND window,
            UINT message,
            WPARAM wparam,
            LPARAM lparam
        );

        void ApplyPipeline();

        // Forgets every binding cached by 'ApplyPipeline'.
        void InvalidatePipeline();

        [[nodiscard]] bool IsTextureHandle(
            std::uintptr_t handle
        ) const;

        // Pipeline state whose repeated identical value can be skipped.
        [[nodiscard]] static bool IsCachedState(
            std::uint32_t state
        );

        // Texture stage state, the stage bits of the id select the stage.
        [[nodiscard]] static bool IsStageState(
            std::uint32_t state
        );

        void ResetStateCache();

        // Drops the cached values of states that write the same values as 'state'.
        void ForgetRelatedStates(
            std::uint32_t state
        );

        // Reads a small rectangle straight from the render sized frame, 'false' when it cannot.
        bool ReadRectProbe(
            int x0,
            int y0,
            int x1,
            int y1,
            int x,
            int y,
            int width,
            bool allow_previous,
            std::uint8_t * out
        );

        void ResetProbes();

        ID3D11SamplerState * GetSampler(
            int stage
        );

        void Flush();

        void BeginBatch(
            Primitive primitive,
            std::size_t vertices,
            std::size_t indices
        );

        void PushVertex(
            const std::uint8_t * source
        );

        void SetTextureStageMode(
            int stage,
            std::uint32_t mode
        );

        void StoreState(
            std::uint32_t id,
            std::uint32_t value
        );

    public:

        Driver( const Driver & ) = delete;
        Driver & operator=( const Driver & ) = delete;

        static Driver & GetInstance();

        void Log(
            const char * format,
            ...
        );

        // Call trace for diagnosing.
        void Trace(
            const char * format,
            ...
        );

        // THRASH entry points.
        AboutBlock * About();

        int Init();

        int Is();

        int SelectDisplay(
            int display
        );

        int SetVideoMode(
            int mode,
            int buffers,
            int depth
        );

        int Restore();

        int Window(
            int window
        );

        void ClearWindow();

        int Clip(
            int x0,
            int y0,
            int x1,
            int y1
        );

        int FlushWindow();

        void PageFlip();

        int Sync(
            int mode
        );

        LockInfo * LockWindow();

        // Draws a frame of the movie player, packed 4:2:2 with 7 bit components, into the front
        // buffer at 'scale' times its size and shows it. Returns 'false' when the game has to draw the frame itself.
        bool MovieBlit(
            const void * pixels,
            int pitch,
            int width,
            int height,
            int x,
            int y,
            int scale
        );

        int UnlockWindow(
            LockInfo * lock
        );

        int ReadRect(
            int x,
            int y,
            int width,
            int height,
            void * out
        );

        int WriteRect(
            int x,
            int y,
            int width,
            int height,
            const void * in
        );

        Texture * TextureAlloc(
            int width,
            int height,
            int format,
            int palette_format,
            int mips
        );

        int TextureUpdate(
            Texture * texture,
            const void * pixels,
            const std::uint32_t * palette
        );

        int TextureFree(
            Texture * texture
        );

        int TextureReset();

        int SetTexture(
            std::uintptr_t handle
        );

        std::intptr_t SetState(
            std::uint32_t id,
            std::uintptr_t value
        );

        std::intptr_t GetState(
            std::uint32_t id
        );

        void DrawTriangles(
            const std::uint8_t * vertices,
            const int * indices,
            int count,
            int stride_indices
        );

        // Single triangle or quad from separate vertex pointers, 'drawtri' and 'drawquad'.
        void DrawPolygon(
            const std::uint8_t * const * corners,
            int count
        );

        void DrawQuads(
            const std::uint8_t * vertices,
            const int * indices,
            int count
        );

        void DrawStrip(
            const std::uint8_t * vertices,
            const int * indices,
            int triangles,
            bool fan
        );

        void DrawLines(
            const std::uint8_t * vertices,
            const int * indices,
            int count,
            bool strip
        );

        void DrawPoints(
            const std::uint8_t * vertices,
            const int * indices,
            int count
        );

        void DrawRaw(
            const std::uint32_t * description
        );

        [[nodiscard]] std::uint32_t VertexStride() const
        {
            return m_TwoUvSets ? 40u : 32u;
        }
    };
}