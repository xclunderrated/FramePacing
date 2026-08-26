add_executable(PacerUI WIN32
    ui_main.cpp
    ui_theme.cpp
    ui_graph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../service/service_core.cpp
)
target_link_libraries(PacerUI PRIVATE
    pacer_shared
    comctl32.lib
    shell32.lib
    ole32.lib
    user32.lib
    gdi32.lib
    gdiplus.lib
    dwmapi.lib
    uxtheme.lib
)
set_target_properties(PacerUI PROPERTIES OUTPUT_NAME "PacerUI")
