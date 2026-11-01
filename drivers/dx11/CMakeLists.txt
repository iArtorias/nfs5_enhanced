# dx11z.dll, a THRASH driver on Direct3D 11.
if ( MSVC )
    set( DX11Z_EXPORTS exports_msvc.def )
else()
    set( DX11Z_EXPORTS exports.def )
endif()

add_library( dx11z SHARED
    src/driver.cpp
    src/formats.cpp
    src/thrash_exports.cpp
    ${DX11Z_EXPORTS}
)

set_target_properties( dx11z PROPERTIES
    PREFIX ""
    OUTPUT_NAME "dx11z"
)

target_compile_definitions( dx11z PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 )
target_link_libraries( dx11z PRIVATE d3d11 dxgi uuid )

if ( MSVC )
    set_property( TARGET dx11z PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )
    target_compile_definitions( dx11z PRIVATE _CRT_SECURE_NO_WARNINGS )
    target_compile_options( dx11z PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /WX )
    target_link_options( dx11z PRIVATE /SAFESEH:NO )
else()
    target_compile_options( dx11z PRIVATE -mstackrealign -msse2 -mfpmath=sse )
    target_compile_options( dx11z PRIVATE -Wall -Wextra -Wno-cast-function-type -Wno-missing-field-initializers )
    target_link_options( dx11z PRIVATE -static -static-libgcc -static-libstdc++ -s )
endif()