#include "movie_upscale.h"

#include "../../core/hook_util.h"
#include "../../core/log.h"
#include "../../game/addresses.h"
#include "../../game/thrash.h"

namespace nfs5
{
    MovieUpscale & MovieUpscale::GetInstance()
    {
        static MovieUpscale instance;
        return instance;
    }


    int __cdecl MovieUpscale::BlitHook(
        int arg1,
        int arg2,
        int arg3,
        int arg4,
        int arg5,
        int arg6
    )
    {
        auto & self = GetInstance();

        // Mode '0' copies at the movie size, '1' to '3' double it. Arguments are x, y, frame, width,
        // height and mode, the frame is packed YUV, a row is 'width' * 2 bytes.
        const auto driver_blit = self.m_DriverBlit.load();
        const bool eligible = driver_blit != nullptr && arg6 >= 1 && arg6 <= 3 && arg3 != 0 && arg4 > 0 && (arg4 & 1) == 0;

        if (eligible && driver_blit( reinterpret_cast<const void *>(arg3), arg4 * 2, arg4, arg5, arg1, arg2, 2 ) != 0)
        {
            return 0;
        }

        return self.m_BlitHook->ccall< int >( arg1, arg2, arg3, arg4, arg5, arg6 );
    }


    void MovieUpscale::OnThrashDriverLoaded(
        HMODULE module
    )
    {
        // Drivers are looked up by the exported name, the stock one has no such function.
        const auto blit = reinterpret_cast<DriverBlitFn>(reinterpret_cast<void *>(::GetProcAddress( module, "NFS5E_MovieBlit" )));
        m_DriverBlit.store( blit );
        LOG_INFO( "Movie driver {}", blit != nullptr ? "draws movie frames, they are enlarged by the driver" : "has no movie support, the game enlarges the frames" );
    }


    bool MovieUpscale::Install(
        const MovieSettings & settings
    )
    {
        m_Settings = settings;

        if (!m_Settings.m_Upscale)
        {
            return true;
        }

        game::ThrashDriver::GetInstance().AddLoadListener( [this] ( HMODULE module )
        {
            OnThrashDriverLoaded( module );
        } );

        m_BlitHook = hook::CreateInline( "Movie blit", game::movie::BLIT.m_Address, game::movie::BLIT.m_Signature, reinterpret_cast<void *>(&BlitHook) );

        if (!m_BlitHook)
        {
            LOG_ERROR( "Movie hook failed, movies stay as they are" );
            return false;
        }

        m_Installed = true;
        LOG_INFO( "Movie installed" );
        return true;
    }


    void MovieUpscale::Uninstall()
    {
        m_DriverBlit.store( nullptr );
        m_BlitHook.reset();
        m_Installed = false;
    }
}
