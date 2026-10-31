#include "gt1_camera.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "game/addresses.h"

namespace nfs5
{
    namespace
    {
        // USA v3.5, the single call from mode 0's switch branch. Mode 1 and
        // the other camera dispatch cases are not redirected.
        constexpr std::uintptr_t MODE0_CALL = 0x00463E7D;
        constexpr std::uintptr_t VANILLA_MODE0 = 0x00461D50;
        constexpr std::uintptr_t CAMERA_STYLE0 = 0x005CF620;

        // Known GT1 drivercamposstyle0, read from gt1.tpg and confirmed in
        // running-game samples. Conservative data signature: unknown/modified
        // car definitions are left on the original path.
        constexpr std::array<std::uint8_t, 12> GT1_STYLE0_POS = {
            0x25, 0xAE, 0x93, 0x3E,
            0x86, 0x56, 0x8F, 0x3E,
            0x55, 0x84, 0x6B, 0x3E
        };

        [[nodiscard]] bool HasGt1CameraData()
        {
            return std::memcmp(
                reinterpret_cast<const void *>(CAMERA_STYLE0),
                GT1_STYLE0_POS.data(), GT1_STYLE0_POS.size()) == 0;
        }

        // The 2003 Enhanced patch replaced the original mode-0 prologue with
        // a jump to mode 1. The unmodified v3.5 executable still contains the
        // original mode-0 BODY starting at 0x461D94. Restore its setup and
        // resume at the corresponding existing code path, without reading
        // or bundling bytes from any other game executable.
        //
        // This entry is called with one cdecl int, and must keep the game
        // routine's x86 stack/register layout exactly: it deliberately jumps
        // into the existing epilogue rather than returning here.
#if defined(_MSC_VER) && defined(_M_IX86)
        __declspec(naked) void __cdecl RestoredMode0(int)
        {
            __asm {
                sub esp, 018h
                push ebx
                push ebp
                mov ebp, dword ptr [esp + 024h]
                mov eax, ebp
                shl eax, 5
                add eax, ebp
                lea ebx, [ebp + eax * 2]
                shl ebx, 2
                movsx ecx, word ptr [ebx + 0627F3Ch]
                shl ecx, 4
                add ecx, 05CF610h
                mov edx, dword ptr [ecx]
                mov dword ptr [esp + 014h], edx
                mov eax, dword ptr [ecx + 4]
                mov dword ptr [esp + 018h], eax
                mov eax, dword ptr ds:[0606A98h]
                mov ecx, dword ptr [ecx + 8]
                test eax, eax
                mov dword ptr [esp + 01Ch], ecx
                jne fallthrough
                mov eax, 0461DA1h
                jmp eax
            fallthrough:
                mov eax, 0461D94h
                jmp eax
            }
        }
#elif defined(__GNUC__) && defined(__i386__)
        __attribute__((naked)) void __cdecl RestoredMode0(int)
        {
            __asm__ __volatile__(
                ".intel_syntax noprefix\n\t"
                "sub esp, 0x18\n\t"
                "push ebx\n\t"
                "push ebp\n\t"
                "mov ebp, DWORD PTR [esp + 0x24]\n\t"
                "mov eax, ebp\n\t"
                "shl eax, 5\n\t"
                "add eax, ebp\n\t"
                "lea ebx, [ebp + eax * 2]\n\t"
                "shl ebx, 2\n\t"
                "movsx ecx, WORD PTR [ebx + 0x627f3c]\n\t"
                "shl ecx, 4\n\t"
                "add ecx, 0x5cf610\n\t"
                "mov edx, DWORD PTR [ecx]\n\t"
                "mov DWORD PTR [esp + 0x14], edx\n\t"
                "mov eax, DWORD PTR [ecx + 4]\n\t"
                "mov DWORD PTR [esp + 0x18], eax\n\t"
                "mov eax, DWORD PTR ds:0x606a98\n\t"
                "mov ecx, DWORD PTR [ecx + 8]\n\t"
                "test eax, eax\n\t"
                "mov DWORD PTR [esp + 0x1c], ecx\n\t"
                "jnz 1f\n\t"
                "mov eax, 0x461da1\n\t"
                "jmp eax\n\t"
                "1:\n\t"
                "mov eax, 0x461d94\n\t"
                "jmp eax\n\t"
                ".att_syntax prefix\n\t");
        }
#else
#error GT1 camera restoration requires an x86 compiler supporting naked functions
#endif

        using CameraFn = void(__cdecl *)(int);
    }

    Gt1Camera & Gt1Camera::GetInstance()
    {
        static Gt1Camera camera;
        return camera;
    }

    void __cdecl Gt1Camera::Mode0Dispatch(int slot)
    {
        Gt1Camera & self = GetInstance();
        if (slot == 0 && HasGt1CameraData())
        {
            RestoredMode0(slot);
        }
        else
        {
            const auto original = reinterpret_cast<CameraFn>(self.m_Mode0Call.GetOriginalTarget());
            if (original != nullptr)
                original(slot);
        }
    }

    void Gt1Camera::Install()
    {
        // PE layout verified by main.cpp before subsystems are initialized.
        // Avoid patching unexpected executables or conflicting hooks.
        if (!hook::MatchBytes(MODE0_CALL, "E8 CE DE FF FF") ||
            !hook::MatchBytes(VANILLA_MODE0, "33 C9 EB 1D") ||
            !hook::MatchBytes(0x00461D94, "A1 70 68 60 00 85 C0") ||
            !hook::MatchBytes(0x00461DA1, "A1 D4 6A 60 00 85 C0"))
        {
            LOG_WARN("GT1 camera: unsupported camera code or another camera patch; skipped");
            return;
        }

        if (m_Mode0Call.Apply("GT1CameraMode0", MODE0_CALL,
                reinterpret_cast<void *>(&Mode0Dispatch)))
        {
            LOG_INFO("GT1 camera: original mode-0 transform enabled for GT1 data signature");
        }
    }

    void Gt1Camera::Uninstall()
    {
        m_Mode0Call.Restore();
    }
}
