
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include <thread.h>
#include <test.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _MSC_VER
#  define PRIsize "Iu"
#else
#  define PRIsize "zu"
#endif

int
test_run(int argc, char** argv) {
	return 0;
}

#if ( defined( __APPLE__ ) && __APPLE__ )
#  include <TargetConditionals.h>
#  if defined( __IPHONE__ ) || ( defined( TARGET_OS_IPHONE ) && TARGET_OS_IPHONE ) || ( defined( TARGET_IPHONE_SIMULATOR ) && TARGET_IPHONE_SIMULATOR )
#    define NO_MAIN 1
#  endif
#endif

#if !defined(NO_MAIN)

int
main(int argc, char** argv) {
	return test_run(argc, argv);
}

#endif
