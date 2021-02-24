cmake_minimum_required(VERSION 3.14)

project(rpmalloc_test_superbuild NONE)

# ---- Collect variables to forward ----

set(
    cmake_args
    -G "${CMAKE_GENERATOR}"
    -D "BUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}"
)

if(CMAKE_GENERATOR_PLATFORM)
  list(APPEND cmake_args -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
  list(APPEND cmake_args -T "${CMAKE_GENERATOR_TOOLSET}")
endif()

get_cmake_property(is_multi_config GENERATOR_IS_MULTI_CONFIG)
if(NOT is_multi_config)
  list(APPEND cmake_args -D "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

# ---- Tests ----

foreach(dir IN ITEMS main override)
  add_custom_target(
      "${dir}_build"
      COMMENT "Building artifacts for '${dir}' dir"
      COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_SOURCE_DIR}/${dir}" -B "${PROJECT_BINARY_DIR}/${dir}" ${cmake_args}
      COMMAND "${CMAKE_COMMAND}" --build "${PROJECT_BINARY_DIR}/${dir}" --config "$<CONFIG>" -v
  )

  add_custom_target(
      "${dir}_test"
      COMMENT "Running tests for '${dir}' dir"
      WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/${dir}"
      COMMAND "${CMAKE_CTEST_COMMAND}" -C "$<CONFIG>" --output-on-failure
  )

  add_dependencies("${dir}_test" "${dir}_build")
endforeach()

add_custom_target(tests ALL)
add_dependencies(
    tests
    main_test
    # override_test
)
