#pragma once

#include "core/hook_util.h"

namespace nfs5
{
    // Faces of dented body parts vanish depending on the camera angle. The game culls the faces of
    // a car on the CPU against face planes stored in the model, damage moves only the corners, so
    // the planes are those of the undamaged car. The game itself unbinds the planes of a dented
    // part, but only in the menus. '[CarFixes] DamageCullFix':
    //   1  the game's own rule also in races (part flag, zones over 25 damage only),
    //   2  unbind them for every deformable part of a car with any applied damage.
    // Without the planes the graphics card culls back faces by their real, dented position.
    class DamageCull
    {
    public:
        static DamageCull & GetInstance();
        void Install( int mode );
        void Uninstall();

    private:
        hook::BytePatch m_Patch;
    };
}
