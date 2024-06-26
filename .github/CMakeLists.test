include_directories(${CMAKE_SOURCE_DIR}/rpmalloc ${CMAKE_SOURCE_DIR}/test)

add_executable(main_test thread.c main.c)
add_executable(storage_test thread.c storage.c)
target_link_libraries(main_test rpmalloc)
target_link_libraries(storage_test rpmalloc)

add_test(NAME main_test COMMAND main_test)
add_test(NAME storage_test COMMAND storage_test)
