#!/usr/bin/env bash
#
# Build and run the rpmalloc stress test (test/stress.c) under a sanitizer.
#
# Usage: test/stress.sh [sanitizer] [seconds] [threads]
#   sanitizer : asan | ubsan | asan+ubsan (default) | tsan | none
#   seconds   : run duration            (default 60,  sets RPMALLOC_STRESS_SECONDS)
#   threads   : worker thread count      (default: hardware, sets RPMALLOC_STRESS_THREADS)
#
# Examples:
#   test/stress.sh                 # AddressSanitizer + UBSan, 60s
#   test/stress.sh tsan 3600       # ThreadSanitizer soak, 1 hour
#   test/stress.sh asan 300 8      # AddressSanitizer, 5 min, 8 threads
#
# Override the compiler with the CC environment variable (defaults to clang).
#
# SPDX-FileCopyrightText: 2026 Mattias Jansson
# SPDX-License-Identifier: Unlicense OR MIT

set -euo pipefail

sanitizer="${1:-asan+ubsan}"
seconds="${2:-60}"
threads="${3:-}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"

compiler="${CC:-clang}"
command -v "$compiler" >/dev/null 2>&1 || compiler=cc

case "$sanitizer" in
	asan)       sanitize_flags="-fsanitize=address" ;;
	ubsan)      sanitize_flags="-fsanitize=undefined" ;;
	asan+ubsan) sanitize_flags="-fsanitize=address,undefined" ;;
	tsan)       sanitize_flags="-fsanitize=thread" ;;
	none)       sanitize_flags="" ;;
	*)
		echo "unknown sanitizer '$sanitizer' (use: asan | ubsan | asan+ubsan | tsan | none)" >&2
		exit 2
		;;
esac

defines="-DENABLE_OVERRIDE=0 -DENABLE_STATISTICS=1 -DENABLE_ASSERTS=1 -DRPMALLOC_FIRST_CLASS_HEAPS=1"
platform_defines=""
link_flags=""
case "$(uname -s)" in
	Linux*) platform_defines="-D_GNU_SOURCE"; link_flags="-lpthread" ;;
	Darwin*) ;;  # pthread is in libc; _GNU_SOURCE not applicable
esac

output="rpmalloc-stress-${sanitizer//+/-}"
echo "Compiling $output with $compiler (${sanitizer}) ..."
# shellcheck disable=SC2086
"$compiler" -g -O1 -fno-omit-frame-pointer $sanitize_flags -fno-sanitize-recover=all \
	$platform_defines $defines -Irpmalloc -Itest \
	rpmalloc/rpmalloc.c test/thread.c test/stress.c $link_flags -o "$output"

export RPMALLOC_STRESS_SECONDS="$seconds"
[ -n "$threads" ] && export RPMALLOC_STRESS_THREADS="$threads"
# ASan trips an internal stacktrace CHECK with the test's custom thread stacks
# unless the alternate signal stack is disabled.
export ASAN_OPTIONS="${ASAN_OPTIONS:-use_sigaltstack=0:detect_leaks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"

echo "Running ./$output for ${seconds}s${threads:+ with $threads threads} ..."
exec "./$output"
