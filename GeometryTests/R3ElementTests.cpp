﻿#include "stdafx.hpp"

#include <float.h>

#include <CppUnitTest.h>

#include "Geometry/Grassmann.hpp"
#include "Geometry/R3Element.hpp"
#include "Quantities/Astronomy.hpp"
#include "Quantities/BIPM.hpp"
#include "Quantities/Constants.hpp"
#include "Quantities/Dimensionless.hpp"
#include "Quantities/ElementaryFunctions.hpp"
#include "Quantities/Quantities.hpp"
#include "Quantities/SI.hpp"
#include "Quantities/UK.hpp"
#include "TestUtilities/Algebra.hpp"
#include "TestUtilities/ExplicitOperators.hpp"
#include "TestUtilities/GeometryComparisons.hpp"
#include "TestUtilities/QuantityComparisons.hpp"
#include "TestUtilities/TestUtilities.hpp"


namespace principia {
namespace geometry {

using namespace astronomy;
using namespace bipm;
using namespace constants;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace quantities;
using namespace si;
using namespace test_utilities;
using namespace uk;

TEST_CLASS(R3ElementTests) {
  R3Element<Speed> const null_velocity_ =
      R3Element<Speed>(0 * Knot, 0 * Knot, 0 * Knot);
  R3Element<Speed> const u_ =
      R3Element<Speed>(3 * Knot, -42 * Parsec / JulianYear, 0 * Knot);
  R3Element<Speed> const v_ =
      R3Element<Speed>(-π * SpeedOfLight, -e * Kilo(Metre) / Hour, -1 * Knot);
  R3Element<Speed> const w_ =
      R3Element<Speed>(2 * Mile / Hour, 2 * Furlong / Day, 2 * Rod / Minute);
  R3Element<Speed> const a_ =
      R3Element<Speed>(88 * Mile / Hour, 300 * Metre / Second, 46 * Knot);

 public:
  TEST_METHOD(Dumb3Vector) {
    AssertEqual((e * Dimensionless(42)) * v_, e * (Dimensionless(42) * v_));
    TestVectorSpace<R3Element<Speed>, Dimensionless>(null_velocity_, u_, v_,
                                                     w_, Dimensionless(0),
                                                     Dimensionless(1), e,
                                                     Dimensionless(42),
                                                     2 * DBL_EPSILON);
    TestAlternatingBilinearMap(Cross<Speed, Speed>, u_, v_, w_, a_,
                               Dimensionless(42), 2 * DBL_EPSILON);
    TestSymmetricPositiveDefiniteBilinearMap(Dot<Speed, Speed>, u_, v_, w_, a_,
                                             Dimensionless(42),
                                             2 * DBL_EPSILON);
  }

  TEST_METHOD(MixedProduct) {
    TestBilinearMap(Times<R3Element<Length>, Time, R3Element<Speed>>,
                    1 * Second, 1 * JulianYear, u_, v_, Dimensionless(42),
                    2 * DBL_EPSILON);
    TestBilinearMap(Times<R3Element<Length>, R3Element<Speed>, Time>, w_, a_,
                    -1 * Day, 1 * Parsec / SpeedOfLight, Dimensionless(-π),
                    2 * DBL_EPSILON);
    Time const t = -3 * Second;
    AssertEqual(t * u_, u_ * t);
    AssertEqual((u_ * t) / t, u_, 2 * DBL_EPSILON);
  }
};

}  // namespace geometry
}  // namespace principia
