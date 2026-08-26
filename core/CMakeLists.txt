add_library(PacerCore SHARED
    dllmain.cpp
    engine_context.cpp
    engine_context.h
    hook_dxgi.cpp
    hook_dxgi.h
    hook_dx9.cpp
    hook_dx9.h
    hook_ddraw.cpp
    hook_ddraw.h
    hook_exports.cpp
    hook_exports.h
    loader_watch.cpp
    loader_watch.h
    display_clock.cpp
    display_clock.h
    pacing.cpp
    pacing.h
    shm.cpp
    shm.h
    log.h
)

target_link_libraries(PacerCore PRIVATE
    pacer_shared
    minhook
    d3d11.lib
    dxgi.lib
    d3d9.lib
    ddraw.lib
    dxguid.lib
    user32.lib
    dwmapi.lib
)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(PacerCore PROPERTIES OUTPUT_NAME "PacerCore86")
else()
    set_target_properties(PacerCore PROPERTIES OUTPUT_NAME "PacerCore")
endif()

