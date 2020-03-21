cmake_minimum_required (VERSION 3.8)

project(RpMalloc)

option(BUILD_RPMALLOC_TEST "Build rpmalloc-test." ON)

add_library(RpMallocStatic STATIC "rpmalloc/rpmalloc.c" "rpmalloc/rpmalloc.h")
target_include_directories(RpMallocStatic PUBLIC rpmalloc )


if(BUILD_RPMALLOC_TEST)
	set(RPMALLOC_TEST_SOURCES 
		"test/main.c"
		"test/test.h"
		"test/thread.c"
		"test/thread.h"
	)


	add_executable (rpmalloc_test ${RPMALLOC_TEST_SOURCES})
	target_include_directories(rpmalloc_test  PUBLIC test )

	target_link_libraries(rpmalloc_test RpMallocStatic)
endif(BUILD_RPMALLOC_TEST)