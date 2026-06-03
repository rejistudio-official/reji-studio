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

    # Widget + bridge sources (only compile when Qt6 is available)
    target_sources(reji_ui PRIVATE
        preview_widget.cpp
        program_widget.cpp
        rust_bridge.cpp
        healing_overlay.cpp
        settings_dialog.cpp
        render_capability.cpp
        shader_cache.cpp
        vulkan_render_path.cpp
        qrhi_window_wrapper.cpp
    )

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
        dwmapi
    )

    if(Vulkan_FOUND)
        target_link_libraries(reji_ui PUBLIC Vulkan::Vulkan)
    endif()
else()
    message(STATUS "Qt6 bulunamadı — reji_ui stub olarak derleniyor. "
                   "Etkinleştirmek için: cmake -DQt6_DIR=<yol> veya Qt6_ROOT env ayarla.")

    target_link_libraries(reji_ui PUBLIC reji_pipeline)
endif()

target_include_directories(reji_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}              # main_window.h, widget headers için
    ${CMAKE_SOURCE_DIR}/src/pipeline/include # pipeline.h için
    ${CMAKE_SOURCE_DIR}/src/ffi              # ffi_bridge.h için
)

if(MSVC)
    set_property(TARGET reji_ui PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(reji_ui PRIVATE /W4 /WX-)
endif()

# ---------------------------------------------------------------------------
# reji_app — executable entry point
# ---------------------------------------------------------------------------
add_executable(reji_app main.cpp)

# ---------------------------------------------------------------------------
# Rust orchestrator static library (rj_command_drain, rj_metrics_push, …)
# Cargo build tipi: -DRUST_BUILD_TYPE=debug (varsayılan) veya release.
# Önce cmake -DRUST_BUILD_TYPE=release ile release lib kullanılabilir.
# ---------------------------------------------------------------------------
if(NOT DEFINED RUST_BUILD_TYPE)
    set(RUST_BUILD_TYPE "debug")
endif()

set(RUST_LIB "${CMAKE_SOURCE_DIR}/target/${RUST_BUILD_TYPE}/reji_orchestrator.lib")

if(NOT EXISTS "${RUST_LIB}")
    message(WARNING
        "Rust kütüphanesi bulunamadı: ${RUST_LIB}\n"
        "  Önce çalıştırın: cargo build  (proje kökünde)\n"
        "  Release için:    cargo build --release  ve -DRUST_BUILD_TYPE=release")
endif()

# Import as an IMPORTED static library so CMake handles it correctly.
add_library(reji_orchestrator STATIC IMPORTED)
set_target_properties(reji_orchestrator PROPERTIES
    IMPORTED_LOCATION         "${RUST_LIB}"
    IMPORTED_LOCATION_DEBUG   "${CMAKE_SOURCE_DIR}/target/debug/reji_orchestrator.lib"
    IMPORTED_LOCATION_RELEASE "${CMAKE_SOURCE_DIR}/target/release/reji_orchestrator.lib"
)

target_link_libraries(reji_app PRIVATE
    reji_ui
    reji_ffi
    reji_orchestrator
    # Rust runtime dependencies on Windows
    ws2_32 bcrypt userenv ntdll
)

if(Qt6_FOUND)
    set_target_properties(reji_app PROPERTIES
        AUTOMOC ON
        WIN32_EXECUTABLE ON
    )
    target_link_libraries(reji_app PRIVATE Qt6::Widgets)
    target_compile_definitions(reji_app PRIVATE QT6_AVAILABLE)
endif()

if(MSVC)
    set_property(TARGET reji_app PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
