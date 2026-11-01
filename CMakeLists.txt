cmake_minimum_required( VERSION 3.24 )

project( nfs5_enhanced LANGUAGES C CXX )

if ( NOT CMAKE_SIZEOF_VOID_P EQUAL 4 )
    message( FATAL_ERROR "'Porsche.exe' is a 32 bit process, configure with -A Win32" )
endif()

set( CMAKE_CXX_STANDARD 23 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

add_library( Zydis STATIC external/zydis/Zydis.c )
target_include_directories( Zydis PUBLIC external/zydis )
target_compile_definitions( Zydis PUBLIC ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD )

add_library( safetyhook STATIC external/safetyhook/safetyhook.cpp )
target_include_directories( safetyhook PUBLIC external/safetyhook )
target_link_libraries( safetyhook PUBLIC Zydis )

add_library( nfs5_enhanced SHARED
    src/main.cpp
    src/core/config.cpp
    src/core/crash_handler.cpp
    src/core/hook_util.cpp
    src/core/log.cpp
    src/game/thrash.cpp
    src/subsystems/patch_silent_compat/affinity.cpp
    src/subsystems/patch_silent_compat/qpc_timer.cpp
    src/subsystems/patch_silent_compat/silent_compat.cpp
    src/subsystems/widescreen/widescreen.cpp
    src/subsystems/detail/detail.cpp
    src/subsystems/framerate/framerate.cpp
    src/subsystems/framerate/interpolation.cpp
    src/subsystems/hud_scaling/hud_scaling.cpp
    src/subsystems/hud_scaling/loading_screen.cpp
    src/subsystems/text/text_quality.cpp
    src/subsystems/io_hooks/io_hooks.cpp
    src/subsystems/gamepad/gamepad.cpp
    src/subsystems/gamepad/sdl3_api.cpp
    src/subsystems/gamepad/virtual_gamepad.cpp
    src/subsystems/menu/menu_scroll.cpp
    src/subsystems/camera/gt1_camera.cpp
    src/resources/version.rc
)

set_target_properties( nfs5_enhanced PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "nfs5_enhanced"
)

target_include_directories( nfs5_enhanced PRIVATE src )

# SDL3 headers only, 'SDL3.dll' is loaded at run time and is optional.
target_include_directories( nfs5_enhanced SYSTEM PRIVATE external/SDL3/include )
target_compile_definitions( nfs5_enhanced PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 )
target_link_libraries( nfs5_enhanced PRIVATE safetyhook winmm )

if ( MSVC )
    set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
    set_property( TARGET nfs5_enhanced Zydis safetyhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
    target_compile_options( nfs5_enhanced PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /WX )
    target_link_options( nfs5_enhanced PRIVATE /SAFESEH:NO )
    # Vendored SafetyHook drops the [[nodiscard]] result of 'vm_protect' in a destructor.
    target_compile_options( safetyhook PRIVATE /wd4834 )
else()
    # The game only guarantees 4 byte stack alignment on entry to hooks.
    target_compile_options( Zydis PRIVATE -mstackrealign )
    target_compile_options( safetyhook PRIVATE -mstackrealign )
    target_compile_options( nfs5_enhanced PRIVATE -mstackrealign )
    target_compile_options( nfs5_enhanced PRIVATE -msse2 -mfpmath=sse )
    target_compile_options( nfs5_enhanced PRIVATE -Wall -Wextra -Wno-cast-function-type -Wno-missing-field-initializers )
    target_link_options( nfs5_enhanced PRIVATE -static -static-libgcc -static-libstdc++ -s -Wl,--enable-stdcall-fixup -Wl,--exclude-all-symbols )
endif()

add_subdirectory( drivers/dx11 )

# The 32 bit 'SDL3.dll' is copied next to the ASI so a build folder can be dropped into the game.
add_custom_command( TARGET nfs5_enhanced POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/external/SDL3/bin/x86/SDL3.dll $<TARGET_FILE_DIR:nfs5_enhanced>/SDL3.dll )