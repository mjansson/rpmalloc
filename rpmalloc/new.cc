/* new.cc  -  Memory allocator  -  Public Domain  -  2017 Mattias Jansson
 *
 * This library provides a cross-platform lock free thread caching malloc implementation in C11.
 * The latest source code is always available at
 *
 * https://github.com/mjansson/rpmalloc
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <new>
#include <cstdint>
#include <cstdlib>

#include "rpmalloc.h"

using namespace std;

#ifdef __clang__
#pragma clang diagnostic ignored "-Wc++98-compat"
#endif

extern void*
operator new(size_t size) noexcept(false);

extern void*
operator new[](size_t size) noexcept(false);

extern void
operator delete(void* ptr) noexcept;

extern void
operator delete[](void* ptr) noexcept;

extern void*
operator new(size_t size, const std::nothrow_t&) noexcept;

extern void*
operator new[](size_t size, const std::nothrow_t&) noexcept;

extern void
operator delete(void* ptr, const std::nothrow_t&) noexcept;

extern void
operator delete[](void* ptr, const std::nothrow_t&) noexcept;

extern void
operator delete(void* ptr, size_t) noexcept;

extern void
operator delete[](void* ptr, size_t) noexcept;

#if (__cplusplus >= 201703L)

extern void*
operator new(size_t size, std::align_val_t align) noexcept(false);

extern void*
operator new[](size_t size, std::align_val_t align) noexcept(false);

extern void*
operator new(size_t size, std::align_val_t align, const std::nothrow_t&) noexcept;

extern void*
operator new[](size_t size, std::align_val_t align, const std::nothrow_t&) noexcept;

#endif

void*
operator new(size_t size) noexcept(false) {
	return rpmalloc(size);
}

void
operator delete(void* ptr) noexcept {
	rpfree(ptr);
}

void*
operator new[](size_t size) noexcept(false) {
	return rpmalloc(size);
}

void
operator delete[](void* ptr) noexcept {
	rpfree(ptr);
}

void*
operator new(size_t size, const std::nothrow_t&) noexcept {
	return rpmalloc(size);
}

void*
operator new[](size_t size, const std::nothrow_t&) noexcept {
	return rpmalloc(size);
}

void
operator delete(void* ptr, const std::nothrow_t&) noexcept {
	rpfree(ptr);
}

void
operator delete[](void* ptr, const std::nothrow_t&) noexcept {
	rpfree(ptr);
}

void
operator delete(void* ptr, size_t) noexcept {
	rpfree(ptr);
}

void
operator delete[](void* ptr, size_t) noexcept {
	rpfree(ptr);
}

#if (__cplusplus >= 201703L)

void*
operator new(size_t size, std::align_val_t align) noexcept(false) {
	return rpaligned_alloc(align, size);
}

void*
operator new[](size_t size, std::align_val_t align) noexcept(false) {
	return rpaligned_alloc(align, size);
}

void*
operator new(size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
	return rpaligned_alloc(align, size);
}

void*
operator new[](size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
	return rpaligned_alloc(align, size);
}

#endif
