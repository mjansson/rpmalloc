cmake_minimum_required (VERSION 3.8)

project(RpMalloc)

option(BUILD_RPMALLOC_TEST "Build rpmalloc-test." ON)
option(RPMALLOC_ENABLE_STATISTICS "Build rpmalloc with Statics on." OFF)
option(RPMALLOC_ENABLE_ASSERTS "Build rpmalloc with Asserts on." OFF)
option(RPMALLOC_ENABLE_ARGUMENT_VALIDATION "Build RPMalloc with Argument Validation" OFF)


add_library(RpMallocStatic STATIC "rpmalloc/rpmalloc.c" "rpmalloc/rpmalloc.h")
target_include_directories(RpMallocStatic PUBLIC rpmalloc )

if(RPMALLOC_ENABLE_STATISTICS)
	target_compile_definitions(RpMallocStatic  PRIVATE "ENABLE_STATISTICS=1")
endif(RPMALLOC_ENABLE_STATISTICS)

if(RPMALLOC_ENABLE_ASSERTS)
	target_compile_definitions(RpMallocStatic  PRIVATE "ENABLE_ASSERTS=1")
endif(RPMALLOC_ENABLE_ASSERTS)

if(RPMALLOC_ENABLE_ARGUMENT_VALIDATION)
	target_compile_definitions(RpMallocStatic  PRIVATE "ENABLE_VALIDATE_ARGS=1")
endif(RPMALLOC_ENABLE_ARGUMENT_VALIDATION)

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
