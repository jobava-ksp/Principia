#pragma once

#include "geometry/named_quantities.hpp"
#include "quantities/quantities.hpp"

using principia::geometry::Position;
using principia::geometry::Velocity;
using principia::quantities::Mass;

namespace principia {
namespace ksp_plugin {

// Corresponds to KSP's |Part.flightID|, *not* to |Part.uid|.  C#'s |uint|
// corresponds to |uint32_t|.
using PartID = uint32_t;

// Represents a KSP part.
template<typename Frame>
struct Part {
  explicit Part(DegreesOfFreedom<Frame> const& degrees_of_freedom,
                Mass const& mass,
                Vector<Acceleration, Frame> const& expected_ksp_gravity);

  DegreesOfFreedom<Frame> degrees_of_freedom;
  Mass mass;
  Vector<Acceleration, Frame> expected_ksp_gravity;
  // TODO(egg): we may want to keep track of the moment of inertia, angular
  // momentum, etc.
};

}  // namespace ksp_plugin
}  // namespace principia

#include "part_body.hpp"
