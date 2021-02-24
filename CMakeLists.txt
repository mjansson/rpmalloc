cmake_minimum_required(VERSION 3.14)

project(
    rpmalloc
    VERSION 1.4.1
    DESCRIPTION "General Purpose Memory Allocator"
    HOMEPAGE_URL "https://github.com/mjansson/rpmalloc"
    LANGUAGES C
)

# ---- Warning guard ----

# Protect dependents from this project's warnings if the guard isn't disabled
set(rpmalloc_warning_guard SYSTEM)
if(rpmalloc_INCLUDE_WITHOUT_SYSTEM)
  set(rpmalloc_warning_guard "")
endif()

# ---- Options ----

option(rpmalloc_BUILD_WRAP "Build the target with libc overrides enabled" OFF)

# PUBLIC options
option(RPMALLOC_ENABLE_STATISTICS "Enable statistics collection" OFF)
option(RPMALLOC_ENABLE_PRELOAD "Support preloading" OFF)
option(RPMALLOC_ENABLE_OVERRIDE "Override standard library malloc/free and new/delete entry points" OFF)
option(RPMALLOC_CONFIGURABLE "Enable configuring sizes (will introduce a very small overhead due to some size calculations not being compile time constants)" OFF)
option(RPMALLOC_FIRST_CLASS_HEAPS "Enable heap based API (rpmalloc_heap_* functions) (will introduce a very small overhead to track fully allocated spans in heaps)" OFF)

# PRIVATE options
option(RPMALLOC_ENABLE_THREAD_CACHE "Enable per-thread cache" ON)
option(RPMALLOC_ENABLE_VALIDATE_ARGS "Enable validation of args to public entry points" OFF)
option(RPMALLOC_ENABLE_ASSERTS "Enable asserts" OFF)
option(RPMALLOC_DISABLE_UNMAP "Disable unmapping memory pages (also enables unlimited cache)" OFF)
set(RPMALLOC_NO_PRESERVE 1 CACHE STRING "Flag to rpaligned_realloc to not preserve content in reallocation")
set(RPMALLOC_HEAP_ARRAY_SIZE 47 CACHE STRING "Size of heap hashmap")
set(RPMALLOC_DEFAULT_SPAN_MAP_COUNT 64 CACHE STRING "Default number of spans to map in call to map more virtual memory (default values yield 4MiB here)")
set(RPMALLOC_GLOBAL_CACHE_MULTIPLIER 8 CACHE STRING "Multiplier for global cache")
set(RPMALLOC_GROW_OR_FAIL 2 CACHE STRING "Flag to rpaligned_realloc to fail and return null pointer if grow cannot be done in-place, in which case the original pointer is still valid (just like a call to realloc which failes to allocate a new block).")

include(CMakeDependentOption)

# PUBLIC
cmake_dependent_option(RPMALLOC_ENABLE_GLOBAL_CACHE "Enable global cache shared between all threads, requires thread cache" ON RPMALLOC_ENABLE_THREAD_CACHE OFF)
if(RPMALLOC_DISABLE_UNMAP AND NOT RPMALLOC_ENABLE_GLOBAL_CACHE)
  message(FATAL_ERROR "Must use global cache if unmap is disabled")
endif()

# PRIVATE
cmake_dependent_option(RPMALLOC_ENABLE_ADAPTIVE_THREAD_CACHE "Enable adaptive thread cache size based on use heuristics" OFF ENABLE_THREAD_CACHE OFF)
set(reuc_doc "Enable unlimited global cache (no unmapping until finalization)")
cmake_dependent_option(RPMALLOC_ENABLE_UNLIMITED_CACHE "${reuc_doc}" OFF RPMALLOC_DISABLE_UNMAP ON)
if(NOT RPMALLOC_ENABLE_GLOBAL_CACHE)
  set(RPMALLOC_ENABLE_UNLIMITED_CACHE OFF CACHE INTERNAL "${reuc_doc}")
endif()

configure_file(
    cmake/rpmalloc_config_private.h.in
    include/rpmalloc_config_private.h
    @ONLY
)

configure_file(
    cmake/rpmalloc_config.h.in
    include/rpmalloc_config.h
    @ONLY
)

macro(rpmalloc_properties TARGET)
  set_target_properties(
      "rpmalloc_${TARGET}" PROPERTIES
      C_VISIBILITY_PRESET hidden
      VERSION "${PROJECT_VERSION}"
      SOVERSION "${PROJECT_VERSION_MAJOR}"
      EXPORT_NAME "${TARGET}"
      OUTPUT_NAME "${TARGET}"
      DEFINE_SYMBOL rpmalloc_EXPORTS
  )

  target_include_directories(
      "rpmalloc_${TARGET}" ${rpmalloc_warning_guard} PUBLIC
      "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/rpmalloc>"
      "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  )

  target_compile_definitions(
      "rpmalloc_${TARGET}" PRIVATE
      "PLATFORM_WINDOWS=$<PLATFORM_ID:Windows>"
      "PLATFORM_POSIX=$<NOT:$<PLATFORM_ID:Windows>>"
      "$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>"
  )
endmacro()

set(malloc 0)
if(RPMALLOC_ENABLE_OVERRIDE OR RPMALLOC_ENABLE_PRELOAD)
  set(malloc 1)
endif()

# ---- Declare library ----

include(GenerateExportHeader)

set(headers rpmalloc/rpmalloc.h)

set(sources rpmalloc/rpmalloc.c)

add_library(rpmalloc_rpmalloc ${sources} ${headers})
add_library(rpmalloc::rpmalloc ALIAS rpmalloc_rpmalloc)

rpmalloc_properties(rpmalloc)

generate_export_header(
    rpmalloc_rpmalloc
    BASE_NAME rpmalloc
    EXPORT_FILE_NAME "include/rpmalloc_export.h"
)

find_package(Threads REQUIRED)
target_link_libraries(
    rpmalloc_rpmalloc PRIVATE
    Threads::Threads
    "$<$<AND:$<PLATFORM_ID:Linux>,${malloc}>:dl>"
)

if(rpmalloc_BUILD_WRAP)
  add_library(rpmalloc_rpmallocwrap ${sources} ${headers})
  add_library(rpmalloc::rpmallocwrap ALIAS rpmalloc_rpmallocwrap)

  rpmalloc_properties(rpmallocwrap)

  target_link_libraries(
      rpmalloc_rpmallocwrap PRIVATE
      Threads::Threads
      "$<$<PLATFORM_ID:Linux>:dl>"
  )
  target_compile_definitions(
      rpmalloc_rpmallocwrap
      PUBLIC RPMALLOC_WRAP
      PRIVATE "$<$<AND:$<PLATFORM_ID:Linux>,$<C_COMPILER_ID:GNU>>:_GNU_SOURCE>"
  )
endif()

# ---- Install ----

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(rpmalloc_include_directory "${CMAKE_INSTALL_INCLUDEDIR}/rpmalloc")

install(
    DIRECTORY "${PROJECT_BINARY_DIR}/include/"
    DESTINATION "${rpmalloc_include_directory}"
    COMPONENT rpmalloc_Development
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*_private.h" EXCLUDE
)

install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/rpmalloc/"
    DESTINATION "${rpmalloc_include_directory}"
    COMPONENT rpmalloc_Development
    FILES_MATCHING PATTERN "*.h"
)

install(
    TARGETS rpmalloc_rpmalloc
    EXPORT rpmallocTargets
    RUNTIME #
    COMPONENT rpmalloc_Runtime
    LIBRARY #
    COMPONENT rpmalloc_Runtime
    NAMELINK_COMPONENT rpmalloc_Development
    ARCHIVE #
    COMPONENT rpmalloc_Development
    INCLUDES #
    DESTINATION "${rpmalloc_include_directory}"
)

if(rpmalloc_BUILD_WRAP)
  install(
      TARGETS rpmalloc_rpmallocwrap
      EXPORT rpmallocTargets
      RUNTIME #
      COMPONENT rpmalloc_Runtime
      LIBRARY #
      COMPONENT rpmalloc_Runtime
      NAMELINK_COMPONENT rpmalloc_Development
      ARCHIVE #
      COMPONENT rpmalloc_Development
      INCLUDES #
      DESTINATION "${rpmalloc_include_directory}"
  )
endif()

write_basic_package_version_file(
    rpmallocConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)

set(
    rpmalloc_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/rpmalloc"
    CACHE STRING "CMake package config location relative to the install prefix"
)
mark_as_advanced(rpmalloc_INSTALL_CMAKEDIR)

install(
    FILES
    "${PROJECT_SOURCE_DIR}/cmake/rpmallocConfig.cmake"
    "${PROJECT_BINARY_DIR}/rpmallocConfigVersion.cmake"
    DESTINATION "${rpmalloc_INSTALL_CMAKEDIR}"
    COMPONENT rpmalloc_Development
)

install(
    EXPORT rpmallocTargets
    NAMESPACE rpmalloc::
    DESTINATION "${rpmalloc_INSTALL_CMAKEDIR}"
    COMPONENT rpmalloc_Development
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CPack)
endif()
