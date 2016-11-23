#ifndef X86JUMP_H
#define X86JUMP_H

/**

   Jump code to permit detouring of functions.
   Use by instantiating into the original start of the function
   as follows:

   new (originalFunctionStart) x86Jump (newFunctionStart);

   Based on code contributed by Charlie Curtsinger <charlie@cs.umass.edu>

   @author Emery Berger <emery@cs.umass.edu>

 **/

#include <new>
#include <stdint.h>

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop) )
#else
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif


PACK(
struct X86Jump32 {
  volatile uint8_t jmp_opcode;
  volatile uint32_t jmp_offset;
  
  // Install a direct 32-bit jump.
  X86Jump32 (void *target)
  {
    jmp_opcode = 0xE9;
    jmp_offset = (uint32_t)((intptr_t)target - (intptr_t)this) - sizeof(struct X86Jump32);
  }
});

PACK(
struct X86Jump64 {

  volatile uint16_t farjmp;
  volatile uint32_t offset;
  volatile uint64_t addr;

  // Install a 64-bit jump (a relative jump to the target with a 0 offset).
  X86Jump64 (void *target)
  {
    farjmp = 0x25ff;
    offset = 0x00000000;
    addr   = (uint64_t) target;
  }
});


PACK(
struct X86_64Jump {
    union {
        uint8_t jmp32[sizeof(X86Jump32)];
        uint8_t jmp64[sizeof(X86Jump64)];
    };
    
    X86_64Jump (void *target) {
      // When the target is close enough, use a 32-bit jump;
      // otherwise, use the full 64-bit jump.
      if ((uintptr_t)target - (uintptr_t)this <= 0x00000000FFFFFFFFu ||
	  (uintptr_t)this - (uintptr_t)target <= 0x00000000FFFFFFFFu) 
	{
	  new (this) X86Jump32(target);
	} else {
	new (this) X86Jump64(target);
      }
    }
});

#if defined(__LP64__) || defined(_LP64) || defined(__APPLE__) || defined(_WIN64) || defined(__x86_64__)
typedef X86_64Jump X86Jump;
#else 
typedef X86Jump32 X86Jump;
#endif

#endif
