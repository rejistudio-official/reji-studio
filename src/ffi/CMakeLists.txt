cmake_minimum_required(VERSION 3.20)
project(reji_ffi)

# ffi_bridge.c kaldırıldı — implementasyon Zig build sistemi üzerinden gelir.
# Önkoşul: `zig build ffi` çalıştırılmış olmalı → zig-out/lib/reji_ffi.lib
set(REJI_FFI_LIB "${CMAKE_SOURCE_DIR}/zig-out/lib/reji_ffi.lib")

if(NOT EXISTS "${REJI_FFI_LIB}")
    message(FATAL_ERROR
        "reji_ffi.lib bulunamadı: ${REJI_FFI_LIB}\n"
        "Önce çalıştırın: zig build ffi")
endif()

add_library(reji_ffi STATIC IMPORTED GLOBAL)
set_target_properties(reji_ffi PROPERTIES
    IMPORTED_LOCATION "${REJI_FFI_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_SOURCE_DIR}"
)
