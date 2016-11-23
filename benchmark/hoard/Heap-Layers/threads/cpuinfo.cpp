// Unit test for CPUInfo.

#include "cpuinfo.h"
#include "fred.h"

#include <iostream>

const int NUMTHREADS = 256;

/// A counter array. We use this to check that CPUInfo is evenly
/// distributing thread ids.
int counter[NUMTHREADS];

using namespace HL;
using namespace std;

void * fn (void *) {
  counter[CPUInfo::getThreadId() % NUMTHREADS]++;
  return NULL;
}

int
main()
{
  // Clear the counter array.
  for (int i = 0; i < NUMTHREADS; i++) {
    counter[i] = 0;
  }

  Fred t[NUMTHREADS];
  for (int i = 0; i < NUMTHREADS; i++) {
    t[i].create (fn, NULL);
  }
  for (int i = 0; i < NUMTHREADS; i++) {
    t[i].join();
  }

  // Now check the counter array.
  int maxCount = 0;
  for (int i = 0; i < NUMTHREADS; i++) {
    if (counter[i] > maxCount) {
      maxCount = counter[i];
    }
  }

  cout << "Maximum entries (should be near 1): " << maxCount << endl;
  return 0;
}
