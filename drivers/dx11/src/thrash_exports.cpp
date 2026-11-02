// THRASH entry points.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "driver.h"

HMODULE g_Module = nullptr;

namespace
{
    using dx11z::Driver;

    constexpr std::uint32_t DEFAULT_SPRITE_STRIDE = 32;
    constexpr std::uint32_t STATE_SPRITE_STRIDE = 0x1E;

    Driver & Get()
    {
        return Driver::GetInstance();
    }


    const std::uint8_t * Bytes(
        const void * pointer
    )
    {
        return static_cast<const std::uint8_t *>(pointer);
    }


    // 'drawsprite' builds a quad from the top left and bottom right corners.
    void DrawSprite(
        const void * first,
        const void * second
    )
    {
        if (first == nullptr || second == nullptr)
            return;

        const Driver & driver = Get();
        const std::uint32_t stride = driver.VertexStride();

        std::uint8_t top_right[40];
        std::uint8_t bottom_left[40];
        std::memcpy( top_right, second, stride );
        std::memcpy( bottom_left, second, stride );

        const auto * a = static_cast<const float *>(first);
        auto * tr = reinterpret_cast<float *>(top_right);
        auto * bl = reinterpret_cast<float *>(bottom_left);

        tr[1] = a[1];
        tr[7] = a[7];
        bl[0] = a[0];
        bl[6] = a[6];

        const std::uint8_t * corners[4] = { Bytes( first ), top_right, Bytes( second ), bottom_left };
        Get().DrawPolygon( corners, 4 );
    }
}

extern "C"
{
    void * __stdcall THRASH_about()
    {
        return Get().About();
    }


    int __stdcall THRASH_init()
    {
        return Get().Init();
    }


    int __stdcall THRASH_is()
    {
        return Get().Is();
    }


    int __stdcall THRASH_selectdisplay(
        int display
    )
    {
        return Get().SelectDisplay( display );
    }


    int __stdcall THRASH_setvideomode(
        int mode,
        int buffers,
        int depth
    )
    {
        return Get().SetVideoMode( mode, buffers, depth );
    }


    // Not a THRASH function. The ASI plug-in offers the frames of the movie player to the
    // driver so they are enlarged at the final resolution. Returns 1 when the frame is drawn.
    int __stdcall NFS5E_MovieBlit(
        const void * pixels,
        int pitch,
        int width,
        int height,
        int x,
        int y,
        int scale
    )
    {
        return Get().MovieBlit( pixels, pitch, width, height, x, y, scale ) ? 1 : 0;
    }


    int __stdcall THRASH_restore()
    {
        return Get().Restore();
    }


    int __stdcall THRASH_window(
        int window
    )
    {
        return Get().Window( window );
    }


    void __stdcall THRASH_clearwindow()
    {
        Get().ClearWindow();
    }


    int __stdcall THRASH_clip(
        int x0,
        int y0,
        int x1,
        int y1
    )
    {
        return Get().Clip( x0, y0, x1, y1 );
    }


    int __stdcall THRASH_flushwindow()
    {
        return Get().FlushWindow();
    }


    void __stdcall THRASH_pageflip()
    {
        Get().PageFlip();
    }


    int __stdcall THRASH_sync(
        int mode
    )
    {
        return Get().Sync( mode );
    }


    void __stdcall THRASH_idle()
    {}


    void * __stdcall THRASH_lockwindow()
    {
        return Get().LockWindow();
    }


    int __stdcall THRASH_unlockwindow(
        void * lock
    )
    {
        return Get().UnlockWindow( static_cast<dx11z::LockInfo *>(lock) );
    }


    int __stdcall THRASH_readrect(
        int x,
        int y,
        int width,
        int height,
        void * out
    )
    {
        return Get().ReadRect( x, y, width, height, out );
    }


    int __stdcall THRASH_writerect(
        int x,
        int y,
        int width,
        int height,
        const void * in
    )
    {
        return Get().WriteRect( x, y, width, height, in );
    }


    void * __stdcall THRASH_talloc(
        int width,
        int height,
        int format,
        int palette_format,
        int mips
    )
    {
        return Get().TextureAlloc( width, height, format, palette_format, mips );
    }


    void * __stdcall THRASH_tupdate(
        void * texture,
        const void * pixels,
        const std::uint32_t * palette
    )
    {
        if (texture == nullptr)
        {
            return nullptr;
        }
        return Get().TextureUpdate( static_cast<dx11z::Texture *>(texture), pixels, palette ) != 0 ? texture : nullptr;
    }


    int __stdcall THRASH_tfree(
        void * texture
    )
    {
        return Get().TextureFree( static_cast<dx11z::Texture *>(texture) );
    }


    int __stdcall THRASH_treset()
    {
        return Get().TextureReset();
    }


    int __stdcall THRASH_settexture(
        std::uintptr_t handle
    )
    {
        return Get().SetTexture( handle );
    }


    std::intptr_t __stdcall THRASH_setstate(
        std::uint32_t id,
        std::uintptr_t value
    )
    {
        const std::intptr_t result = Get().SetState( id, value );

        if (result == 0)
        {
            Get().Trace( "setstate %X %X returned 0", id, static_cast<unsigned>(value) );
        }

        return result;
    }


    std::intptr_t __stdcall THRASH_getstate(
        std::uint32_t id
    )
    {
        return Get().GetState( id );
    }

    // Triangles.

    void __stdcall THRASH_drawtri(
        const void * a,
        const void * b,
        const void * c
    )
    {
        const std::uint8_t * corners[3] = { Bytes( a ), Bytes( b ), Bytes( c ) };
        Get().DrawPolygon( corners, 3 );
    }


    void __stdcall THRASH_drawtrimesh(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        Get().DrawTriangles( Bytes( vertices ), indices, count, 3 );
    }


    void __stdcall THRASH_drawtristrip_indexed(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        Get().DrawStrip( Bytes( vertices ), indices, count, false );
    }


    void __stdcall THRASH_drawtristrip_plain(
        int count,
        const void * vertices
    )
    {
        Get().DrawStrip( Bytes( vertices ), nullptr, count, false );
    }


    void __stdcall THRASH_drawtrifan_indexed(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        Get().DrawStrip( Bytes( vertices ), indices, count, true );
    }


    void __stdcall THRASH_drawtrifan_plain(
        int count,
        const void * vertices
    )
    {
        Get().DrawStrip( Bytes( vertices ), nullptr, count, true );
    }

    // Quads and sprites.

    void __stdcall THRASH_drawquad(
        const void * a,
        const void * b,
        const void * c,
        const void * d
    )
    {
        const std::uint8_t * corners[4] = { Bytes( a ), Bytes( b ), Bytes( c ), Bytes( d ) };
        Get().DrawPolygon( corners, 4 );
    }


    void __stdcall THRASH_drawquadmesh(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        Get().DrawQuads( Bytes( vertices ), indices, count );
    }


    void __stdcall THRASH_drawsprite(
        const void * first,
        const void * second
    )
    {
        DrawSprite( first, second );
    }


    void __stdcall THRASH_drawspritemesh(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        if (vertices == nullptr || indices == nullptr)
            return;

        std::intptr_t stride = Get().GetState( STATE_SPRITE_STRIDE );

        if (stride <= 0)
        {
            stride = DEFAULT_SPRITE_STRIDE;
        }

        for (int i = 0; i < count; ++i)
        {
            DrawSprite( Bytes( vertices ) + indices[i * 2] * stride, Bytes( vertices ) + indices[i * 2 + 1] * stride );
        }
    }

    // Lines and points.

    void __stdcall THRASH_drawline(
        const void * a,
        const void * b
    )
    {
        if (a == nullptr || b == nullptr)
            return;

        // Two vertices in a scratch array so the line path can index them.
        std::uint8_t pair[80];
        const std::uint32_t stride = Get().VertexStride();
        std::memcpy( pair, a, stride );
        std::memcpy( pair + stride, b, stride );
        Get().DrawLines( pair, nullptr, 1, false );
    }


    void __stdcall THRASH_drawlinemesh(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        Get().DrawLines( Bytes( vertices ), indices, count, false );
    }


    void __stdcall THRASH_drawlinestrip_indexed(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        Get().DrawLines( Bytes( vertices ), indices, count, true );
    }


    void __stdcall THRASH_drawlinestrip_plain(
        int count,
        const void * vertices
    )
    {
        Get().DrawLines( Bytes( vertices ), nullptr, count, true );
    }


    void __stdcall THRASH_drawpoint(
        const void * vertex
    )
    {
        Get().DrawPoints( Bytes( vertex ), nullptr, 1 );
    }


    void __stdcall THRASH_drawpointmesh(
        int count,
        const void * vertices,
        const int * indices
    )
    {
        Get().DrawPoints( Bytes( vertices ), indices, count );
    }


    void __stdcall THRASH_drawpointstrip(
        int count,
        const void * vertices
    )
    {
        Get().DrawPoints( Bytes( vertices ), nullptr, count );
    }
}


BOOL WINAPI DllMain(
    HINSTANCE instance,
    DWORD reason,
    LPVOID
)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_Module = instance;
        ::DisableThreadLibraryCalls( instance );
    }

    return TRUE;
}