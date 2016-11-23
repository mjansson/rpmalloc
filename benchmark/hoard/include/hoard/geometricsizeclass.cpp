#include "geometricsizeclass.h"

int
main()
{
  Hoard::GeometricSizeClass<12, 16> gs1;
  Hoard::GeometricSizeClass<14, 16> gs2;
  Hoard::GeometricSizeClass<16, 8> gs3;
  Hoard::GeometricSizeClass<18, 8> gs4;
  Hoard::GeometricSizeClass<20, 16> gs5;
  return 0;
}
