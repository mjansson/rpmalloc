cmake_minimum_required(VERSION 3.14)

project(rpmalloc_main_test C)

set(rpmalloc_INCLUDE_WITHOUT_SYSTEM YES)
set(RPMALLOC_ENABLE_ASSERTS YES)
set(RPMALLOC_ENABLE_STATISTICS YES)
set(RPMALLOC_FIRST_CLASS_HEAPS YES)
set(RPMALLOC_CONFIGURABLE YES)

add_subdirectory(../.. root)

find_package(Threads REQUIRED)

enable_testing()

add_executable(main_test ../thread.c main.c)
target_link_libraries(main_test PRIVATE rpmalloc::rpmalloc Threads::Threads)
target_include_directories(main_test PRIVATE "${PROJECT_SOURCE_DIR}/../include")
target_compile_definitions(
    main_test PRIVATE
    "$<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>"
    "$<$<PLATFORM_ID:Linux>:_GNU_SOURCE>"
)

add_test(NAME main_test COMMAND main_test)
set_property(
    TEST main_test PROPERTY
    ENVIRONMENT "PATH=$<TARGET_FILE_DIR:rpmalloc::rpmalloc>"
)
