#include <iostream>

using namespace std;

#include "align.h"

int
main(int argc, char * argv[])
{
  for (int i = 0; i < 100; i++) {
    cout << i << "\t" << HL::align<4>(i) << endl;
  }
  return 0;
}
