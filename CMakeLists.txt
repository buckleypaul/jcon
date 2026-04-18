cmake_minimum_required(VERSION 3.13)
project(jcon VERSION 0.1.0 LANGUAGES C DESCRIPTION "Streaming JSON emitter for bare-metal C")

option(JCON_ENABLE_FLOAT "Enable jcon_add_float / jcon_add_double" OFF)
set(JCON_MAX_DEPTH "16" CACHE STRING "Maximum JSON nesting depth")

add_library(jcon STATIC src/jcon.c)
add_library(jcon::jcon ALIAS jcon)

target_include_directories(jcon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(jcon PUBLIC c_std_11)

target_compile_definitions(jcon PUBLIC JCON_MAX_DEPTH=${JCON_MAX_DEPTH})

if(JCON_ENABLE_FLOAT)
    target_compile_definitions(jcon PUBLIC JCON_ENABLE_FLOAT)
endif()
