
#pragma once

#if defined(__clang__)
#if __has_warning("-Wreserved-identifier")
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif
#endif

extern int
test_run(int argc, char** argv);
