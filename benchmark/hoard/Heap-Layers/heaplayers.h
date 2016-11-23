// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2012 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/**
 * @file heaplayers.h
 * @brief The master Heap Layers include file.
 *
 * Heap Layers is an extensible memory allocator infrastructure.  For
 * more information, read the PLDI 2001 paper "Composing
 * High-Performance Memory Allocators", by Emery D. Berger, Benjamin
 * G. Zorn, and Kathryn S. McKinley.
 * (http://citeseer.ist.psu.edu/berger01composing.html)
 */

#ifndef HL_HEAPLAYERS_H
#define HL_HEAPLAYERS_H

#include <assert.h>

namespace HL {}

// Define HL_EXECUTABLE_HEAP as 1 if you want that (i.e., you're doing
// dynamic code generation).

#define HL_EXECUTABLE_HEAP 0

#if defined(_MSC_VER)

// Microsoft Visual Studio
#pragma inline_depth(255)
#define INLINE __forceinline
//#define inline __forceinline
#define NO_INLINE __declspec(noinline)
#pragma warning(disable: 4530)
#define MALLOC_FUNCTION
#define RESTRICT

#elif defined(__GNUC__)

// GNU C

#define NO_INLINE       __attribute__ ((noinline))
#define INLINE          inline
#define MALLOC_FUNCTION __attribute__((malloc))
#define RESTRICT        __restrict__

#else

// All others

#define NO_INLINE
#define INLINE inline
#define MALLOC_FUNCTION
#define RESTRICT

#endif


/**
 * @def ALLOCATION_STATS
 *
 * Defining ALLOCATION_STATS below as 1 enables tracking of allocation
 * statistics in a variety of layers. You then must link in
 * definitions of the extern variables used therein; stats.cpp
 * contains these definitions.
 *
 * This should be undefined for all but experimental use.
 *
 */

#ifndef ALLOCATION_STATS
#define ALLOCATION_STATS 0
#endif

#ifdef _MSC_VER
// 4786: Disable warnings about long (> 255 chars) identifiers.
// 4512: Disable warnings about assignment operators.
#pragma warning( push )
#pragma warning( disable:4786 4512 )
#endif

#include "utility/all.h"
#include "heaps/all.h"
#include "locks/all.h"
#include "threads/all.h"
#include "wrappers/all.h"

#ifdef _MSC_VER
#pragma warning( pop )
#endif

#endif // _HEAPLAYERS_H_
