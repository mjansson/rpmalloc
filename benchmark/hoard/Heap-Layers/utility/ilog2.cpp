#include "ilog2.h"

#include <cassert>
#include <iostream>
using namespace std;

int
main()
{
  for (int i = 1; i < 1048577; i++) {
    int l2 = HL::ilog2(i);
    cout << i << "\t" << l2 << "\t" << (1 << l2) << endl;
    // Make sure that 2^ceil(log2(i)) is at least i.
    assert ((1 << l2) >= i);
    if (l2 > 0) {
      assert (HL::ilog2(1 << l2-1) + 1 == l2);
    }
  }
  return 0;
}
