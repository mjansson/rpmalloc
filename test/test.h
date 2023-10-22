
#pragma once

#if defined(__clang__)
#if __has_warning("-Wreserved-identifier")
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif
#endif

extern int
test_run(int argc, char** argv);

extern int
test_fail_cb(const char* reason, const char* file, int line);

#define test_fail(msg) test_fail_cb(msg, __FILE__, __LINE__)
