// -*- C++ -*-

#ifndef HL_MACINTERPOSE_H
#define HL_MACINTERPOSE_H

// The interposition data structure (just pairs of function pointers),
// used an interposition table like the following:
//

typedef struct interpose_s {
  void *new_func;
  void *orig_func;
} interpose_t;

#define MAC_INTERPOSE(newf,oldf) __attribute__((used)) \
  static const interpose_t macinterpose##newf##oldf \
  __attribute__ ((section("__DATA, __interpose"))) = \
    { (void *) newf, (void *) oldf }

#endif
