cmake_minimum_required(VERSION 3.14)

project(rpmalloc_override_test)

set(rpmalloc_INCLUDE_WITHOUT_SYSTEM YES)
set(rpmalloc_BUILD_WRAP YES)
set(RPMALLOC_ENABLE_ASSERTS YES)
set(RPMALLOC_ENABLE_STATISTICS YES)

add_subdirectory(../.. root)

find_package(Threads REQUIRED)

enable_testing()

add_executable(override_test ../thread.c main-override.cc)
target_link_libraries(override_test PRIVATE rpmalloc::rpmallocwrap Threads::Threads)
target_include_directories(override_test PRIVATE "${PROJECT_SOURCE_DIR}/../include")

add_test(NAME override_test COMMAND override_test)

string(REGEX REPLACE "\n *" "" env_prop [[
$<IF:$<PLATFORM_ID:Windows>,
  PATH=$<TARGET_FILE_DIR:rpmalloc::rpmallocwrap>
,
  $<IF:$<PLATFORM_ID:Linux>,LD_PRELOAD,DYLD_INSERT_LIBRARIES>
  =
  $<TARGET_FILE:rpmalloc::rpmallocwrap>
>
]])
set_property(TEST override_test PROPERTY ENVIRONMENT "${env_prop}")
