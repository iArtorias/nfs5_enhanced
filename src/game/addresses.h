#pragma once

// Verified addresses for 'Porsche.exe' [v3.5 patch applied over].

#include <cstdint>
#include <string_view>

#include "game_addr.h"

namespace nfs5::game
{
    inline constexpr std::uint32_t EXPECTED_TIMESTAMP = 0x393C2E8F;
    inline constexpr std::uint32_t EXPECTED_IMAGE_SIZE = 0x002E3000;
    inline constexpr std::uintptr_t EXPECTED_IMAGE_BASE = 0x00400000;

    struct Site
    {
        GameAddr m_Address;
        std::string_view m_Signature;
    };

    namespace fn
    {
        // EAGL camera 'SetPerspective'.
        inline constexpr Site CAMERA_SET_PERSPECTIVE =
        {
            0x005391B0, "56 8B F1 57 8D BE 48 01 00 00"
        };

        // Sky layer cull wedge, thiscall on the layer. Builds two horizontal planes at
        // 90 degrees plus the view half FOV (degrees at view +130h) around the view forward
        // vector; the 9 sky patches of a layer are only drawn inside that wedge.
        inline constexpr Site SKY_CULL_PLANES =
        {
            0x0045AAF0, "A1 30 81 62 00 83 EC 18 56 8B"
        };

        // World section visibility, thiscall (view, orientation, position, distance). Builds the
        // 2D view pyramid and the two side planes the track chunks are selected with, from the
        // view half FOV at +130h.
        inline constexpr Site WORLD_CULL_VOLUME =
        {
            0x0047F550, "81 EC FC 00 00 00 53 8B 9C 24 04 01 00 00"
        };

        // 'WM_KEYDOWN' and 'WM_SYSKEYDOWN' table handler stdcall 6 args.
        inline constexpr Site KEY_DOWN_HANDLER =
        {
            0x0053B450, "8B 44 24 04 53 56 57 85 C0 74 7C A1"
        };

        // Forces Caps, Num and Scroll Lock state through keybd_event.
        inline constexpr Site SET_LOCK_KEY_STATE =
        {
            0x005739B0, "51 8B 44 24 08 55 56 57 33 FF"
        };

        // Game window procedure registered in 'GRAPH_init'.
        inline constexpr Site GAME_WND_PROC =
        { 
            0x0053ABA0, "83 EC 0C A1 70 E5 69 00 53 55 56"
        };

        // 'TIMER_init' and 'TIMER_restore'.
        inline constexpr Site TIMER_INIT =
        {
            0x00565030, "83 EC 08 53 55 56 57 E8 64 FF FF FF"
        };

        inline constexpr Site TIMER_SHUTDOWN =
        {
            0x00564FA0, "A1 0C 5C 6A 00 56 33 F6 3B C6"
        };

        // CD check specific.
        inline constexpr Site CD_CHECK =
        {
            0x0059D81A, "85 C0 ? BB B9 20 00 00 00 BE B8 3C 5E 00"
        };

        inline constexpr std::size_t CD_CHECK_BRANCH = 2;

        // 'THRASH_opendll' returns nonzero on success.
        inline constexpr Site THRASH_OPEN_DLL =
        {
            0x00574FA0, "81 EC 08 04 00 00 53 55 8B AC 24 14"
        };

        // Video mode filter returns bool.
        inline constexpr Site IS_VIDEO_MODE_USABLE =
        {
            0x0044E780, "A1 30 81 62 00 8A 48 60 84 C9 74 0C"
        };

        // A call to resolution list count.
        inline constexpr Site RESOLUTION_LIST_COUNT_CALL =
        {
            0x004BC735, "E8 C6 1F F9 FF 8B E8 8D 3C AD 00 00 00 00"
        };

        // 'FEScreenResolutionAccessor' constructor, thiscall with the accessor name. The selection
        // starts at '0', the first mode of the driver, 640x480x16.
        inline constexpr Site RESOLUTION_ACCESSOR_CTOR =
        {
            0x004BC710, "83 EC 5C 8B 44 24 60 53 55 56 57 6A 00 6A 00 6A 00 6A 00 6A 01"
        };

        // 'FEPositionableAccessor' save and load of the selected position, thiscall with a pointer
        // to the stream cursor. Shared by every list accessor of the front end.
        inline constexpr Site POSITION_SAVE =
        {
            0x004BC460, "8B 44 24 04 8B 49 18 8B 10 89 0A 8B 08 83 C1 04 89 08 C2 04 00"
        };

        inline constexpr Site POSITION_LOAD =
        {
            0x004BC480, "8B 44 24 04 8B 10 8B 12 89 51 18 8B 10 83 C2 04 89 10 8B 51 18"
        };

        // 'FEScreenResolutionAccessor' layout. The mode arrays hold one entry per listed mode.
        inline constexpr GameAddr RESOLUTION_ACCESSOR_VTABLE = 0x005B5354;
        inline constexpr std::size_t ACCESSOR_COUNT = 0x14;
        inline constexpr std::size_t ACCESSOR_POSITION = 0x18;
        inline constexpr std::size_t RESOLUTION_WIDTHS = 0x38;
        inline constexpr std::size_t RESOLUTION_HEIGHTS = 0x3C;
        inline constexpr std::size_t RESOLUTION_DEPTHS = 0x40;

        // Render context 'SetVideoMode'.
        // The closest mode of the THRASH mode list is picked by 'sub_572A00'.
        inline constexpr Site SET_VIDEO_MODE =
        {
            0x00467FC0, "8B 44 24 04 8B 15 84 97 61 00 53 8B"
        };

        // Return addresses of the two race mode switches, both pass the race resolution globals
        // '4B6CB9' race start in the outer loop, '41562E' after leaving in race options.
        inline constexpr GameAddr SET_VIDEO_MODE_RACE_START_RETURN = 0x004B6CBE;
        inline constexpr GameAddr SET_VIDEO_MODE_RACE_OPTIONS_RETURN = 0x00415633;

        // EA file layer open.
        inline constexpr Site FILE_LAYER_OPEN =
        {
            0x005919A0, "83 EC 28 56 33 F6 57 89 74 24 10 89"
        };

        // EA thread create.
        inline constexpr GameAddr EA_THREAD_CREATE = 0x0055F420;
        inline constexpr std::string_view EA_THREAD_CREATE_SIG = "A1 DC 57 6A 00 83 EC 18 55 33";

        // CPU clock measurement returning MHz scaled, called by the CPU detection
        // 'sub_555BC0' which runs on every video mode change, profile load and race start
        // The RDTSC variant spins on 'QueryPerformanceCounter' for five 200 ms windows,
        // one full second per call, the other variant is used without RDTSC support.
        inline constexpr Site CPU_SPEED_RDTSC =
        {
            0x00555FA0, "55 8B EC 83 EC 38 53 56 8D 45 C8 57 33 DB"
        };

        inline constexpr Site CPU_SPEED_LEGACY =
        {
            0x005560B0, "55 8B EC 83 EC 14 53 56 33 F6 57 89 75 F8"
        };

        // Value reported instead of the measurement, the constant the executable returns from the RDTSC variant.
        // The largest clock that stays positive.
        inline constexpr std::uint32_t CPU_SPEED_CONSTANT = 0x7F000000;

        // One simulation step, advances 'SIM_STEP_COUNT' by one.
        // Called from the race loop once per pending half tick, twice in one replay mode
        inline constexpr Site SIM_STEP =
        {
            0x00414FF0, "E8 AB BF 09 00 A1 C0 6A 60 00 85 C0"
        };

        // Per frame race renderer used as race marker.
        inline constexpr Site RACE_RENDER_FRAME =
        {
            0x004689C0, "56 57 8B F9 8B 0D 30 81 62 00 E8 01"
        };
    }

    namespace site
    {
        // IDirectInput 'EnumDevices' call setup inside the controller enumerator.
        // Hook point is the push of the callback, 'ecx' already holds the vtable.
        inline constexpr GameAddr ENUM_DEVICES_BLOCK = 0x00532BCF;
        inline constexpr std::string_view ENUM_DEVICES_BLOCK_SIG = "56 52 89 74 24 0C 8B 08 68 40 2C 53 00 56 50 FF 51 10";
        inline constexpr GameAddr ENUM_DEVICES_HOOK = 0x00532BD7;
        inline constexpr std::string_view ENUM_DEVICES_HOOK_SIG = "68 40 2C 53 00";

        // Race loop render decision, 'cmp [RACE_SKIP_RENDER], ebx' then 'jne' over the render call
        // The flag is set when no new simulation tick arrived since the last frame.
        inline constexpr Site RACE_RENDER_CHECK = { 0x00415C6E, "39 1D AC 6A 60 00 75 09 8D 4C 24 24 E8 41 2D 05 00" };

        // First instruction after the render call, reached on both paths.
        inline constexpr Site RACE_AFTER_RENDER = 
        {
            0x00415C7F, "E8 ? ? ? ? 85 C0"
        };

        // 'fmul dword ptr [1/640]' inside the light flare sprite builder.
        inline constexpr Site FLARE_WIDTH_SCALE = 
        {
            0x00451824, "D8 0D 50 3D 5B 00"
        };

        inline constexpr std::size_t FLARE_WIDTH_SCALE_OPERAND = 2;

        // Car LOD selector 'sub_439F90' measures the projected car size and normalises it to a
        // 640 wide screen, fild [RACE_WIDTH] then fdivr [640.0]. The size shrinks with a wider
        // horizontal FOV, so Hor+ moves every LOD switch closer and far cars lose their wheels
        // [LOD 5 and 6 have no wheel parts].
        inline constexpr Site CAR_LOD_SIZE_BLOCK = 
        {
            0x0043A2D7, "DB 05 48 7A 65 00 D8 3D 8C 2A 5B 00 DE C9 D9 55 F4 D8 1D 00 26 5B 00 DF E0 F6 C4 41 74 10"
        };

        inline constexpr Site CAR_LOD_SIZE_DIVIDE =
        {
            0x0043A2DD, "D8 3D 8C 2A 5B 00"
        };

        inline constexpr std::size_t CAR_LOD_SIZE_DIVIDE_OPERAND = 2;
    }

    namespace var
    {
        // Current render resolution.
        inline constexpr GameAddr SCREEN_WIDTH = 0x00619784;
        inline constexpr GameAddr SCREEN_HEIGHT = 0x00619788;

        // Key state buffer, one byte per scan code without the extended bit, 128 bytes. The
        // window procedure writes 'scan & 7Fh' and clears the same 128 bytes itself.
        inline constexpr GameAddr KEY_STATE = 0x005DE028;
        inline constexpr std::size_t KEY_STATE_SIZE = 128;

        // TIMER module state [inittmr.c, lowtimer.c].
        inline constexpr GameAddr TIMER_CALLBACKS = 0x006B7C20;
        inline constexpr std::size_t TIMER_CALLBACK_COUNT = 8;
        inline constexpr GameAddr TIMER_TICKS_SECONDARY = 0x006B7C40;
        inline constexpr GameAddr TIMER_PIT_TICKS = 0x006B7C44;
        inline constexpr GameAddr TIMER_THREAD_RECORD = 0x006B7C60;
        inline constexpr GameAddr TIMER_TICKS = 0x006B7C7C;
        inline constexpr GameAddr TIMER_FREQUENCY = 0x005DEB48;
        inline constexpr GameAddr TIMER_PERIOD_FIXED = 0x006A5BF8;
        inline constexpr GameAddr TIMER_THREAD_EVENT = 0x006A5BFC;
        inline constexpr GameAddr TIMER_FRACTION = 0x006A5C00;
        inline constexpr GameAddr TIMER_NEXT_DUE = 0x006A5C04;
        inline constexpr GameAddr TIMER_RESOLUTION = 0x006A5C08;
        inline constexpr GameAddr TIMER_RUNNING = 0x006A5C0C;
        inline constexpr GameAddr TIMER_ID = 0x006A5C10;
        inline constexpr GameAddr TIMER_SUPPRESS_SIGNAL = 0x006A5C20;
        inline constexpr GameAddr TIMER_FRAME_EVENT = 0x0069E5DC;

        // THRASH driver function pointer table filled by 'THRASH_opendll'.
        inline constexpr GameAddr THRASH_ABOUT = 0x006BD934;
        inline constexpr GameAddr THRASH_PAGEFLIP = 0x006BD948;

        // char pointer to the drivers folder prefix, '.\drivers\' in 'sub_467470'.
        inline constexpr GameAddr DRIVER_FOLDER = 0x0065B304;

        // 'THRASH_readrect', copies a framebuffer rectangle.
        inline constexpr GameAddr THRASH_READRECT = 0x006BD96C;

        // Race resolution chosen in 'Options -> Graphics', written by the frontend.
        inline constexpr GameAddr RACE_WIDTH = 0x00657A48;
        inline constexpr GameAddr RACE_HEIGHT = 0x00657A4C;
        inline constexpr GameAddr RACE_BPP = 0x00657A50;

        // Global render context, +0x60 software flag, +0x70 display mode object.
        inline constexpr GameAddr RENDER_CONTEXT = 0x00628130;

        // Race loop state.
        inline constexpr GameAddr RACE_SKIP_RENDER = 0x00606AAC;
        inline constexpr GameAddr RACE_MENU_RETURN = 0x00606AD8;
        inline constexpr GameAddr RACE_PAUSED = 0x00606A90;
        inline constexpr GameAddr RACE_SUSPENDED = 0x00606A98;
        inline constexpr GameAddr RACE_EXIT = 0x00606A8C;
        inline constexpr GameAddr SIM_STEP_COUNT = 0x00606AC4;

        // Car pointer lists, the same car objects appear in several lists
        // Racers, 9 slots, count at 'CAR_LIST_RACERS_COUNT'.
        inline constexpr GameAddr CAR_LIST_RACERS = 0x005E8B34;
        inline constexpr GameAddr CAR_LIST_RACERS_COUNT = 0x005E8B58;

        // Pursuit and police list used by the helicopter spot light, 9 slots.
        inline constexpr GameAddr CAR_LIST_POLICE = 0x005E8AC4;
        inline constexpr GameAddr CAR_LIST_POLICE_COUNT = 0x005E8ABC;

        // Traffic, 128 slots.
        inline constexpr GameAddr CAR_LIST_TRAFFIC = 0x005E8C24;
        inline constexpr GameAddr CAR_LIST_TRAFFIC_COUNT = 0x005E8B5C;

        // Race camera blocks, one per view, 268 bytes each, first dword is the target car.
        inline constexpr GameAddr CAMERA_BLOCKS = 0x00627E80;
        inline constexpr std::size_t CAMERA_BLOCK_STRIDE = 268;
        inline constexpr std::size_t CAMERA_BLOCK_COUNT = 2;

        // EA file layer handle table, 32 byte entries, count at 'FILE_TABLE_COUNT'.
        inline constexpr GameAddr FILE_TABLE = 0x006AF084;
        inline constexpr GameAddr FILE_TABLE_COUNT = 0x006AF080;
    }

    // DirectInput controller layer, 'input.c' of the game.
    namespace input
    {
        // 'INPUT_init', creates DirectInput and fills the controller table through 'EnumDevices'.
        inline constexpr Site INIT =
        {
            0x00532B80, "51 8B 0D 08 CB 69 00 56 33 F6 B8 01 00 00 00 3B CE"
        };

        // 'EnumDevices' callback, stdcall instance and pointer to the running count.
        inline constexpr Site ENUM_CALLBACK =
        {
            0x00532C40, "51 8B 44 24 0C 53 55 56 57 8B 38 8B CF A1 08 CB"
        };

        // 'INPUT_restore', unacquires and releases every controller and DirectInput.
        inline constexpr Site SHUTDOWN =
        {
            0x00532D30, "57 E8 1A 03 00 00 A1 0C CB 69 00 33 FF"
        };

        // Default bindings for a controller, cdecl with the table index. Runs when the default
        // controller is picked in the options, when the controller screen is built and when
        // a saved controller is missing. Joysticks get accelerate and brake on Y.
        inline constexpr Site DEFAULT_BINDINGS =
        {
            0x0051D1D0, "83 EC 78 53 55 56 33 DB 57 53 68 88 D4 5D 00"
        };

        // Front end accessor by name, cdecl, '0' when there is none.
        inline constexpr Site FIND_ACCESSOR =
        {
            0x004B7AB0, "83 EC 0C 8B 54 24 10 56 57 8B FA 83 C9 FF"
        };

        // Unbinds every action that uses the given input, cdecl with the six binding fields.
        inline constexpr Site CLEAR_BINDING =
        {
            0x0051DCC0, "83 EC 50 53 55 56 57 6A 00 68 88 D4 5D 00"
        };

        // 'FEControllerInputAccessor', one per action, its 24 byte binding sits at +0x14.
        inline constexpr GameAddr INPUT_ACCESSOR_VTABLE = 0x005BACF4;
        inline constexpr std::size_t ACCESSOR_BINDING = 0x14;

        // 'IDirectInputA' pointer and controller table, 32 entries of 268 bytes. An entry holds
        // the device, flags, the device type and a 256 byte state buffer.
        inline constexpr GameAddr DIRECT_INPUT = 0x0069CB08;
        inline constexpr GameAddr DEVICE_COUNT = 0x0069CB0C;
        inline constexpr GameAddr DEVICE_TABLE = 0x006BE040;
        inline constexpr std::size_t DEVICE_STRIDE = 268;
        inline constexpr int MAX_DEVICES = 32;
    }

    // Race HUD scaling.
    namespace hud
    {
        enum class SizeAxis : std::uint8_t
        {
            WIDTH,
            HEIGHT,
        };

        // An instruction that reads screen width or screen height, the 32 bit operand at
        // 'm_OperandOffset' is the global address.
        struct SizeRead
        {
            GameAddr m_Address;
            std::string_view m_Signature;
            std::size_t m_OperandOffset;
            SizeAxis m_Axis;
        };

        // Every read in the HUD code range '417000' to '438000' and the two reads of the HUD
        // orthographic camera 'sub_467180'. None of these functions set a 3D viewport in pixels,
        // the map view in 'sub_432880' passes fractions to 'sub_4672F0' which uses the real size.
        inline constexpr SizeRead SIZE_READS[] =
        {
            { 0x00417A9B, "8B 0D 84 97 61 00", 2, SizeAxis::WIDTH }, // mov ecx, dword ptr [0x619784]
            { 0x00417AA7, "8B 15 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov edx, dword ptr [0x619788]
            { 0x00417AB3, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x00417AC3, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0041AE23, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0041AE3D, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0041B100, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0041B113, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0041B78E, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0041B7BC, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0041BA3B, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0041BA53, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0041BCC5, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0041BCE3, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0041CB2A, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0041CB32, "8B 0D 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov ecx, dword ptr [0x619788]
            { 0x0041CB3B, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0041CB4D, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0041DE2F, "DB 05 84 97 61 00", 2, SizeAxis::WIDTH }, // fild dword ptr [0x619784]
            { 0x0041DE5A, "DB 05 88 97 61 00", 2, SizeAxis::HEIGHT }, // fild dword ptr [0x619788]
            { 0x004218E2, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x00421906, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0042446D, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x00424472, "8B 0D 84 97 61 00", 2, SizeAxis::WIDTH }, // mov ecx, dword ptr [0x619784]
            { 0x00427B06, "DB 05 88 97 61 00", 2, SizeAxis::HEIGHT }, // fild dword ptr [0x619788]
            { 0x00427B0C, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x00427B2C, "8B 0D 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov ecx, dword ptr [0x619788]
            { 0x004289E4, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x00428A2F, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x00428A4C, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x00428B15, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x00428B38, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0042A482, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0042A4A6, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0042DF0D, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0042DF21, "8B 15 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov edx, dword ptr [0x619788]
            { 0x0042E082, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0042E0B0, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x0043296C, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x00432971, "8B 0D 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov ecx, dword ptr [0x619788]
            { 0x00432BB6, "DB 05 84 97 61 00", 2, SizeAxis::WIDTH }, // fild dword ptr [0x619784]
            { 0x00432BD9, "DB 05 88 97 61 00", 2, SizeAxis::HEIGHT }, // fild dword ptr [0x619788]
            { 0x00432C6B, "8B 15 84 97 61 00", 2, SizeAxis::WIDTH }, // mov edx, dword ptr [0x619784]
            { 0x00432CB0, "DB 05 88 97 61 00", 2, SizeAxis::HEIGHT }, // fild dword ptr [0x619788]
            { 0x00432CFF, "8B 35 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov esi, dword ptr [0x619788]
            { 0x00432D3A, "DB 05 84 97 61 00", 2, SizeAxis::WIDTH }, // fild dword ptr [0x619784]
            { 0x00434591, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x004345A4, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x00434630, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x0043463A, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x00434753, "8B 3D 84 97 61 00", 2, SizeAxis::WIDTH }, // mov edi, dword ptr [0x619784]
            { 0x00434811, "8B 1D 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov ebx, dword ptr [0x619788]
            { 0x004348C5, "8B 2D 84 97 61 00", 2, SizeAxis::WIDTH }, // mov ebp, dword ptr [0x619784]
            { 0x0043497E, "8B 2D 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov ebp, dword ptr [0x619788]
            { 0x00436DFD, "8B 35 88 97 61 00", 2, SizeAxis::HEIGHT }, // mov esi, dword ptr [0x619788]
            { 0x00437363, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x00437373, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x00437482, "A1 84 97 61 00", 1, SizeAxis::WIDTH }, // mov eax, dword ptr [0x619784]
            { 0x004374A8, "A1 88 97 61 00", 1, SizeAxis::HEIGHT }, // mov eax, dword ptr [0x619788]
            { 0x00467183, "DB 05 84 97 61 00", 2, SizeAxis::WIDTH }, // fild dword ptr [0x619784]
            { 0x00467198, "DB 05 88 97 61 00", 2, SizeAxis::HEIGHT }, // fild dword ptr [0x619788]
        };

        // HUD layout loader.
        // Layout records are stored per resolution in the car .hud files and the
        // 'Options -> Display' page edits the record of the selected race screen size.
        inline constexpr Site LAYOUT_INIT =
        {
            0x0041E170, "8B 44 24 0C 8B 4C 24 10 8B 54 24 08"
        };

        // 'Options -> Display' preview [sub_4CB970] scales the layout into its picture with
        // '640 / width' and '480 / height' of the selected race screen size, read through the
        // resolution accessor 'sub_4BAAF0' and 'sub_4BC840'.
        inline constexpr Site PREVIEW_WIDTH_CALL =
        {
            0x004CBBD9, "E8 12 EF FE FF 89 44 24 48"
        };

        inline constexpr Site PREVIEW_HEIGHT_CALL =
        {
            0x004CBBF4, "E8 47 0C FF FF 89 44 24 48"
        };

        inline constexpr GameAddr RESOLUTION_WIDTH = 0x004BAAF0;
        inline constexpr GameAddr RESOLUTION_HEIGHT = 0x004BC840;

        // Mode setters, first instruction after the screen size has been written.
        inline constexpr Site MODE_WRITE =
        {
            0x0044E324, "A3 8C 97 61 00 75 0A"
        };

        inline constexpr Site MODE_WRITE_ALT =
        {
            0x0044EC58, "A3 8C 97 61 00 5B 75 0A"
        };

        // HUD flush 'sub_41A330' text loop, esi is the text entry, edi its font holder
        // Begin is after the optional scale call and before the draw call 'sub_536A00'.
        inline constexpr Site TEXT_BEGIN =
        {
            0x0041A495, "8B 4E FC 8B 56 08 8B 46 04 51 8B 4F 04"
        };

        inline constexpr Site TEXT_END =
        {
            0x0041A4AE, "A1 EC C7 60 00"
        };

        // Race loading screen, composed into a 640x480 window and blitted centred at 1:1.
        inline constexpr Site LOADING_BLIT_CALL =
        {
            0x004A53A2, "E8 C9 81 09 00 83 C4 0C"
        };

        // Shape blit, shapes have a 16 byte header, type byte at +0,
        // int16 width at +4 and height at +6, pixels from +16.
        inline constexpr Site SHAPE_BLIT =
        {
            0x0053D570, "A0 30 E6 5D 00 84 C0 0F 84"
        };

        // Bits per pixel by shape type, indexed with type and 7Fh.
        inline constexpr GameAddr SHAPE_BITS_TABLE = 0x005DE890;

        // EAGL display size used by the loading screen blit.
        inline constexpr GameAddr DISPLAY_WIDTH = 0x005DEAC8;
        inline constexpr GameAddr DISPLAY_HEIGHT = 0x005DEACC;

        // Digital speed and rpm readouts, 'HSpeed' and 'HTachDigital' draw methods. Each asks
        // whether its gauge is enabled at all, so the digital value is also
        // drawn next to the analog gauge. Mode values are '0' [off], '1' [digital], '2' [analog].
        inline constexpr Site DIGITAL_SPEED_CHECK =
        {
            0x0042E577, "E8 A4 03 FF FF"
        };

        inline constexpr Site DIGITAL_RPM_CHECK =
        {
            0x0042F745, "E8 36 F2 FE FF"
        };

        // Active HUD layout record, pointer table at 'LAYOUT_TABLE' indexed by 'LAYOUT_INDEX'.
        inline constexpr GameAddr LAYOUT_TABLE = 0x0060C904;
        inline constexpr GameAddr LAYOUT_INDEX = 0x0060C95C;
        inline constexpr std::size_t LAYOUT_SPEEDOMETER = 0x30;
        inline constexpr std::size_t LAYOUT_TACHOMETER = 0x38;
        inline constexpr std::int32_t GAUGE_DIGITAL = 1;

        // Font set scale.
        inline constexpr GameAddr FONT_SET_SCALE = 0x00536CE0;
        inline constexpr std::string_view FONT_SET_SCALE_SIG = "8B 44 24 04 56 8B F1 50 6A 02";
    }

    namespace detail
    {
        // Race draw distance, stores 'factor * distance' with a minimum of 50 at 6197CC.
        // Factor is the view distance option at 5CE90C.
        inline constexpr Site DRAW_DISTANCE =
        {
            0x0044F1A0, "83 3D 24 74 65 00 02 7C 1F"
        };

        // Render context constructor, far clip of 700 that every view is created with.
        inline constexpr Site FAR_CLIP =
        {
            0x004677FC, "C7 46 08 00 00 2F 44"
        };

        inline constexpr std::size_t FAR_CLIP_OPERAND = 3;

        // A second pass resets the main view clip planes to 0.15 and 700 through 'sub_467120'.
        inline constexpr Site FAR_CLIP_RESET =
        {
            0x0045D501, "68 00 00 2F 44"
        };

        inline constexpr std::size_t FAR_CLIP_RESET_OPERAND = 1;

        // Scene object culling in 'sub_47F890'. Objects are drawn while their distance minus their
        // radius is within the draw distance, except two classes with fixed limits that ignore it.
        // Instance flag 400000h up to 600 [360000 squared] and flag 20h up to 700 [490000].
        // Below 700 those were drawn further than the rest, a longer draw distance turned that
        // around and left them missing under everything else.
        inline constexpr Site OBJECT_LIMIT_400000 =
        {
            0x0047FA08, "D8 1D A8 47 5B 00 DF E0 F6 C4 01"
        };

        inline constexpr Site OBJECT_LIMIT_20 =
        {
            0x0047FA3D, "D8 1D AC 47 5B 00 DF E0 F6 C4 01"
        };

        inline constexpr std::size_t OBJECT_LIMIT_OPERAND = 2;

        // Occlusion test of the same culling. 'sub_488B50' [thiscall on the track map at 628960]
        // tests the line from the camera to the object centre against the occlusion walls of the
        // track in the top view, without height. An object behind a wall is dropped even where the
        // camera looks over the wall, which leaves holes in the ground close to the car.
        // AL holds the result, 'test al, al' then 'jnz 47FAD1' drops the object.
        // At this point [esp+3Ch] is the object sphere [x, y, z, radius] and [esp+14h] points at
        // the camera position.
        inline constexpr Site OCCLUSION_RESULT =
        {
            0x0047F9AE, "84 C0 0F 85 1B 01 00 00"
        };

        inline constexpr std::size_t OCCLUSION_SPHERE = 0x3C;
        inline constexpr std::size_t OCCLUSION_CAMERA = 0x14;
        inline constexpr float OBJECT_LIMIT_400000_STOCK = 600.0f;
        inline constexpr float OBJECT_LIMIT_20_STOCK = 700.0f;

        // Wheel LOD per detail level and body LOD, int32 [3][7].
        inline constexpr GameAddr WHEEL_LOD_TABLE = 0x005CDB44;

        // Car LOD selection 'sub_439F90' keeps EBX as the first LOD a car may use.
		// It starts at '5CD5B8[car detail] = {3, 3, 2}', one lower on fast machines,
		// so even the player car never gets LOD 0, the model the garage shows.
		// Only traffic [flag 10h] starts at 0.
        // Texture detail detection, returns '0' to '3' from video memory, system memory and CPU
        // clock. 'sub_44DFB0' stores it at 619770 [lowered to 2 below 32 bpp], it selects the row
        // of the texture format table 5CE59C. Bit depth and which sizes are halved on load.
        // The 'Texture Detail' option [657D88] is never read back into it.
        inline constexpr Site TEXTURE_DETAIL_DETECT =
        {
            0x0044E370, "8B 0D 08 E9 5C 00 B8 83 DE 1B 43"
        };

        inline constexpr Site CAR_LOD_OFFSET =
        {
            0x0043A0DC, "8B BE B0 00 00 00 8B 86 B4 00 00 00"
        };

        // End of 'sub_439F90', the loop over the LODs below the chosen one calls 'sub_43E670',
        // which hands the suspension travel of the four wheels to the wheel transforms.
        // 'jle' skips it at LOD 0, the wheels then stay fixed to the rolling body.
        inline constexpr Site WHEEL_SUSPENSION_LOOP =
        {
            0x0043A3C4, "85 C0 7E 32 8B 07 6A 00"
        };

        // 'jle' to 'jl', the loop runs once at LOD 0 and is unchanged otherwise.
        inline constexpr std::uint8_t WHEEL_SUSPENSION_LOOP_JL[] = { 0x85, 0xC0, 0x7C };

        // Lamp glow sprite in 'sub_43CDD0' (headlights, tail lights): depth bias
        // round( -20000 * brightness * [[628130] + C] / distance^2 ) stored at [ebp-14] and
        // handed to the sprite state ('sub_537A60'). Beyond about 90 m it rounds to 0 and the
        // glow z-fights with the lamp lens of the car model, parts of it turn black.
        inline constexpr Site LAMP_GLOW_DEPTH_BIAS =
        {
            0x0043D452, "8B 45 EC 8D 8D A0 FE FF FF 50"
        };

        // Offset of the rounded bias from 'ebp'.
        inline constexpr std::ptrdiff_t LAMP_GLOW_DEPTH_BIAS_EBP = -0x14;
    }

    namespace text
    {
        // EAGL font texture path.
        // 'sub_571A10' rebuilds the 16 entry palette of a font and uploads its glyph sheet through
        // the font driver table at font +E8h.
        inline constexpr Site FONT_UPLOAD =
        {
            0x00571A10, "56 8B 74 24 08 57 33 FF 8B 46 44 85 C0"
        };

        // 'sub_5722C0' draws glyph quads of the selected font texture,
        // UVs arrive in texels and are normalised per tile in place.
        inline constexpr Site FONT_DRAW_QUADS =
        {
            0x005722C0, "55 8B EC 83 EC 24 A1 2C 64 6A 00"
        };

        // Tile texture selection inside 'sub_5722C0', 'sub_555440' [dtexture].
        inline constexpr Site FONT_SELECT_TILE_CALL =
        {
            0x005725A0, "E8 9B 2E FE FF 8B 4D 0C"
        };

        // 'sub_572230' frees a font texture object.
        inline constexpr Site FONT_TEXTURE_FREE =
        {
            0x00572230, "57 8B 7C 24 08 85 FF 74 4D"
        };

        // THRASH font driver table, font +E8h holds it when glyphs are drawn as textures.
        inline constexpr GameAddr FONT_DRIVER_TABLE = 0x005DFB60;

        // Font texture object selected for drawing.
        inline constexpr GameAddr CURRENT_FONT_TEXTURE = 0x006A642C;

        // Display color depth, 8 bit displays use the software font path.
        inline constexpr GameAddr DISPLAY_BPP = 0x005DEAD0;

        // Font object fields.
        inline constexpr std::size_t FONT_HEADER = 0x24;
        inline constexpr std::size_t FONT_SHAPE = 0x2C;
        inline constexpr std::size_t FONT_TEXTURE = 0x44;
        inline constexpr std::size_t FONT_PALETTE = 0x6C;
        inline constexpr std::size_t FONT_DRIVER = 0xE8;

        // FFN header flags at +0Ch, bit 1 selects a multi color outline palette.
        inline constexpr std::size_t HEADER_FLAGS = 0x0C;
        inline constexpr std::uint32_t FLAG_OUTLINE_PALETTE = 0x2;
        inline constexpr std::uint32_t FLAG_TWO_STAGE_PALETTE = 0x4;

        // THRASH driver entry points, filled by 'THRASH_opendll'.
        inline constexpr GameAddr THRASH_TUPDATE = 0x006BD940;
        inline constexpr GameAddr THRASH_TFREE = 0x006BD94C;
        inline constexpr GameAddr THRASH_SETTEXTURE = 0x006BD954;
        inline constexpr GameAddr THRASH_SETVIDEOMODE = 0x006BD958;
        inline constexpr GameAddr THRASH_TALLOC = 0x006BD968;
        inline constexpr GameAddr THRASH_TRESET = 0x006BD988;

        // 'THRASH_about' block, +2Ch format count, +30h pointer to the supported format flags.
        inline constexpr std::size_t ABOUT_FORMAT_COUNT = 0x2C;
        inline constexpr std::size_t ABOUT_FORMATS = 0x30;

        // EA texture formats, 6 is A8R8G8B8 and 7 is A4R4G4B4.
        inline constexpr int FORMAT_ARGB8888 = 6;
        inline constexpr int FORMAT_ARGB4444 = 7;
    }

    // Front end lists, scrolled by the mouse wheel and the arrow keys.
    // Lists are 'FEComboBox' [drop down, 'NFS5.ComboBoxSlider': cars, paints, tracks] or 'FEListBox'
    // [always open]. The game turns 'WM_MOUSEWHEEL' into events of type 9, but the control dispatcher
    // 'sub_525F80' has no case for them, and the key slot [+10h] of both classes is 'sub_4D1B40' [not handled].
    namespace menu
    {
        inline constexpr GameAddr COMBOBOX_VTABLE = 0x005B5C24;
        inline constexpr GameAddr LISTBOX_VTABLE = 0x005B611C;

        // 'FEEmbossedVerticalSlider_MainFunctionality', the scroll bar of a list.
        inline constexpr GameAddr LIST_SLIDER_VTABLE = 0x005B6A64;

        // 'EventPump@FERT', slot 5 [+14h] polls the input, slot 9 [+24h] dispatches an event.
        inline constexpr GameAddr EVENT_PUMP_VTABLE = 0x005BB150;
        inline constexpr std::size_t EVENT_PUMP_POLL_SLOT = 0x14;
        inline constexpr std::size_t EVENT_PUMP_DISPATCH_SLOT = 0x24;
        inline constexpr GameAddr EVENT_PUMP_POLL = 0x00529700;
        inline constexpr GameAddr EVENT_PUMP_DISPATCH = 0x00529990;

        // Slot 0 [+0h] destructor and slot 8 [+20h] mouse move, the same for both list classes.
        inline constexpr std::size_t LIST_DTOR_SLOT = 0x00;
        inline constexpr std::size_t LIST_MOVE_SLOT = 0x20;
        inline constexpr GameAddr LIST_DTOR = 0x004CCE70;
        inline constexpr GameAddr LIST_MOVE = 0x004C6F60;

        // 'WM_MOUSEWHEEL' handler registered by 'sub_536E50', stdcall with 6 arguments.
        inline constexpr Site WHEEL_MESSAGE =
        {
            0x00537020, "8B 44 24 04 85 C0"
        };

        inline constexpr GameAddr GAME_WINDOW = 0x006B7BF8;

        // Mouse position of the game, as carried by the mouse events.
        inline constexpr GameAddr MOUSE_X = 0x0069E07C;
        inline constexpr GameAddr MOUSE_Y = 0x0069E080;

        // Non zero while the front end runs.
        inline constexpr GameAddr FRONT_END = 0x0065B928;

        // Control rectangle [l, t, r, b] into '*out', thiscall.
        inline constexpr GameAddr CONTROL_RECT = 0x004C8670;
        // Control or one of its parents hidden, fastcall.
        inline constexpr GameAddr CONTROL_HIDDEN = 0x004C7E40;
        // Point [x, y] inside a part of a rectangle, thiscall.
        inline constexpr GameAddr PART_HIT = 0x004E2970;
        // Mouse over the scroll bar, fastcall.
        inline constexpr GameAddr SLIDER_HOVER = 0x004DBA50;
        // Front end sends the mouse move at the current spot again, fastcall.
        inline constexpr GameAddr REFRESH_MOUSE = 0x004D6140;

        // '__RTDynamicCast' and the type descriptors of the colour list check.
        inline constexpr GameAddr DYNAMIC_CAST = 0x005A134B;
        inline constexpr GameAddr TD_POSITIONABLE_ACCESSOR = 0x005D70E0;
        inline constexpr GameAddr TD_COLOUR_ACCESSOR = 0x005D7240;
        // 'FEColourAccessor': index is the 'own colour' entry, thiscall.
        inline constexpr GameAddr COLOUR_IS_SPECIAL = 0x004B94B0;

        // 'FEComboBox' / 'FEListBox' fields.
        inline constexpr std::size_t CONTROL_FLAGS = 0x10C;     // 40h = open
        inline constexpr std::size_t CONTROL_CAN_OPEN = 0x110;  // 40h = can be opened
        inline constexpr std::size_t LIST_DATA = 0x144;         // data accessor, entries and selection
        inline constexpr std::size_t LIST_BOX_PART = 0x178;     // part 'selectionbox'
        inline constexpr std::size_t LIST_LABEL_PART = 0x17C;   // part of the closed box
        inline constexpr std::size_t LIST_SLIDER = 0x1B8;
        inline constexpr std::size_t LIST_SCROLL = 0x1BC;       // '<Name>.Sliderbar' accessor, value = top row
        inline constexpr std::size_t LIST_MAX_HEIGHT = 0x1C0;   // height of the selection box
        inline constexpr std::size_t LIST_HAS_SLIDER = 0x1DC;
        inline constexpr std::size_t LIST_ALWAYS_OPEN = 0x1DD;  // 'FEListBox'
        inline constexpr std::size_t LIST_COLOUR_SPECIAL = 0x1DE; // colour list with an 'own colour' entry
        inline constexpr std::uint32_t CONTROL_OPEN = 0x40;

        // Key codes of the game ['sub_53B530'], the keypad gives the same.
        inline constexpr int KEY_HOME = 0x4700;
        inline constexpr int KEY_UP = 0x4800;
        inline constexpr int KEY_PAGE_UP = 0x4900;
        inline constexpr int KEY_END = 0x4F00;
        inline constexpr int KEY_DOWN = 0x5000;
        inline constexpr int KEY_PAGE_DOWN = 0x5100;
        inline constexpr int EVENT_KEY = 2;
    }

    namespace movie
    {
        // Software blitter of the movie player ['sub_56A920'], cdecl with 6 arguments: x, y, frame,
        // width, height and mode. The player decodes a frame at the size of the movie into a packed
        // 4:2:2 buffer with 7 bit components [see 'FromMovieYuv' in the driver] and this routine
        // converts it to the display format in the locked front buffer. Mode '0' is the movie size,
        // '1' and '2' double the pixels and '3' doubles with an average of neighbours.
        inline constexpr Site BLIT =
        {
            0x0056A920, "8B 44 24 18 81 EC 18 0A 00 00 85 C0 53 55 56 57"
        };
    }
}