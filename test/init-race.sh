#!/usr/bin/env bash
#
# Build and run the rpmalloc concurrent startup race test (test/init-race.c) under
# a sanitizer. Defaults to ThreadSanitizer, which is what surfaces the lazy-init
# data race on the unfixed allocator.
#
# Usage: test/init-race.sh [sanitizer] [rounds] [threads]
#   sanitizer : tsan (default) | asan | ubsan | asan+ubsan | none
#   rounds    : init/finalize rounds     (default: test default, sets RPMALLOC_INIT_RACE_ROUNDS)
#   threads   : threads per round        (default: 4x hardware, sets RPMALLOC_INIT_RACE_THREADS)
#
# Examples:
#   test/init-race.sh              # ThreadSanitizer, default rounds/threads
#   test/init-race.sh tsan 200     # ThreadSanitizer, 200 rounds
#   test/init-race.sh none 500 64  # no sanitizer, 500 rounds, 64 threads
#
# Override the compiler with the CC environment variable (defaults to clang).
#
# SPDX-FileCopyrightText: 2026 Mattias Jansson
# SPDX-License-Identifier: Unlicense OR MIT

set -euo pipefail

sanitizer="${1:-tsan}"
rounds="${2:-}"
threads="${3:-}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"

compiler="${CC:-clang}"
command -v "$compiler" >/dev/null 2>&1 || compiler=cc

case "$sanitizer" in
	tsan)       sanitize_flags="-fsanitize=thread" ;;
	asan)       sanitize_flags="-fsanitize=address" ;;
	ubsan)      sanitize_flags="-fsanitize=undefined" ;;
	asan+ubsan) sanitize_flags="-fsanitize=address,undefined" ;;
	none)       sanitize_flags="" ;;
	*)
		echo "unknown sanitizer '$sanitizer' (use: tsan | asan | ubsan | asan+ubsan | none)" >&2
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

output="rpmalloc-init-race-${sanitizer//+/-}"
echo "Compiling $output with $compiler (${sanitizer}) ..."
# shellcheck disable=SC2086
"$compiler" -g -O1 -fno-omit-frame-pointer $sanitize_flags -fno-sanitize-recover=all \
	$platform_defines $defines -Irpmalloc -Itest \
	rpmalloc/rpmalloc.c test/thread.c test/init-race.c $link_flags -o "$output"

[ -n "$rounds" ] && export RPMALLOC_INIT_RACE_ROUNDS="$rounds"
[ -n "$threads" ] && export RPMALLOC_INIT_RACE_THREADS="$threads"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1 suppressions=$repo_root/test/tsan-suppressions.txt}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-use_sigaltstack=0:detect_leaks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

echo "Running ./$output ..."
exec "./$output"
