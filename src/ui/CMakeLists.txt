cmake_minimum_required(VERSION 3.20)
project(reji_ui CXX)

# ---------------------------------------------------------------------------
# Qt6 discovery — requires Widgets + OpenGLWidgets (pulls in OpenGL).
# To enable: cmake -DQt6_DIR=<Qt6 cmake dir> or set QT_ROOT / Qt6_ROOT env.
# ---------------------------------------------------------------------------
find_package(Qt6 COMPONENTS Widgets OpenGL OpenGLWidgets QUIET)

add_library(reji_ui STATIC main_window.cpp)

if(Qt6_FOUND)
    message(STATUS "Qt6 ${Qt6_VERSION} bulundu — tam UI etkinleştirildi")

    # AUTOMOC/AUTOUIC per-target — avoids polluting parent scope.
    set_target_properties(reji_ui PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )

    target_compile_definitions(reji_ui PUBLIC QT6_AVAILABLE)

    target_link_libraries(reji_ui PUBLIC
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        reji_pipeline
    )
else()
    message(STATUS "Qt6 bulunamadı — reji_ui stub olarak derleniyor. "
                   "Etkinleştirmek için: cmake -DQt6_DIR=<yol> veya Qt6_ROOT env ayarla.")

    target_link_libraries(reji_ui PUBLIC reji_pipeline)
endif()

target_include_directories(reji_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}              # main_window.h için
    ${CMAKE_SOURCE_DIR}/src/pipeline/include # pipeline.h için
    ${CMAKE_SOURCE_DIR}/src/ffi              # ffi_bridge.h için (v0.2)
)

if(MSVC)
    set_property(TARGET reji_ui PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(reji_ui PRIVATE /W4 /WX-)
endif()
