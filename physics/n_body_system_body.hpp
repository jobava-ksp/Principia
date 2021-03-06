﻿#include "physics/n_body_system.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <vector>

#include "base/not_null.hpp"
#include "base/macros.hpp"
#include "geometry/named_quantities.hpp"
#include "geometry/r3_element.hpp"
#include "glog/logging.h"
#include "integrators/sprk_integrator.hpp"
#include "physics/oblate_body.hpp"
#include "quantities/quantities.hpp"

namespace principia {

using geometry::InnerProduct;
using geometry::Instant;
using geometry::R3Element;
using integrators::SPRKIntegrator;
using integrators::MotionIntegrator;
using quantities::Acceleration;
using quantities::Exponentiation;
using quantities::GravitationalParameter;
using quantities::Length;
using quantities::Speed;

namespace physics {

namespace {

// If j is a unit vector along the axis of rotation, and r is the separation
// between the bodies, the acceleration computed here is:
//
//   -(J2 / |r|^5) (3 j (r.j) + r (3 - 15 (r.j)^2 / |r|^2) / 2)
//
// Where |r| is the norm of r and r.j is the inner product.
template<typename Frame>
FORCE_INLINE Vector<Acceleration, Frame>
    Order2ZonalAccelerationLegacy(
        OblateBody<Frame> const& body,
        Vector<Length, Frame> const& r,
        Exponentiation<Length, -2> const& one_over_r_squared,
        Exponentiation<Length, -3> const& one_over_r_cubed) {
  Vector<double, Frame> const& axis = body.axis();
  Length const r_axis_projection = InnerProduct(axis, r);
  auto const j2_over_r_fifth =
      body.j2() * one_over_r_cubed * one_over_r_squared;
  Vector<Acceleration, Frame> const& axis_acceleration =
      (-3 * j2_over_r_fifth * r_axis_projection) * axis;
  Vector<Acceleration, Frame> const& radial_acceleration =
      (j2_over_r_fifth *
           (-1.5 +
            7.5 * r_axis_projection *
                  r_axis_projection * one_over_r_squared)) * r;
  return axis_acceleration + radial_acceleration;
}

}  // namespace

template<typename Frame>
void NBodySystem<Frame>::Integrate(SRKNIntegrator const& integrator,
                                   Instant const& tmax,
                                   Time const& Δt,
                                   int const sampling_period,
                                   bool const tmax_is_exact,
                                   Trajectories const& trajectories) const {
  SRKNIntegrator::Parameters<Length, Speed> parameters;
  SRKNIntegrator::Solution<Length, Speed> solution;

  // TODO(phl): Use a position based on the first mantissa bits of the
  // centre-of-mass referential and a time in the middle of the integration
  // interval.  In the integrator itself, all quantities are "vectors" relative
  // to these references.
  Position<Frame> const reference_position;
  Instant const reference_time;

  // These objects are for checking the consistency of the parameters.
  std::set<Instant> times_in_trajectories;
  std::set<Body const*> bodies_in_trajectories;

  // Prepare the initial state of the integrator.  For efficiently computing the
  // accelerations, we need to separate the trajectories of oblate massive
  // bodies from of spherical massive bodies and those of massless bodies.  They
  // are put in this order in |reordered_trajectories|.
  Trajectories reordered_trajectories;
  ReadonlyTrajectories massive_oblate_trajectories;
  ReadonlyTrajectories massive_spherical_trajectories;
  ReadonlyTrajectories massless_trajectories;
  // This loop ensures that the massive bodies precede the massless bodies in
  // the vectors representing the initial data.
  for (bool is_massless : {false, true}) {
    for (bool is_oblate : {true, false}) {
      for (auto const& trajectory : trajectories) {
        // See if this trajectory should be processed in this iteration and
        // update the appropriate vector.
        not_null<Body const*> const body = trajectory->template body<Body>();
        if (body->is_massless() != is_massless ||
            body->is_oblate() != is_oblate) {
          continue;
        }
        if (is_massless) {
          CHECK(!is_oblate);
          massless_trajectories.push_back(trajectory);
        } else if (is_oblate) {
          massive_oblate_trajectories.push_back(trajectory);
        } else {
          massive_spherical_trajectories.push_back(trajectory);
        }
        reordered_trajectories.push_back(trajectory);

        // Fill the initial position/velocity/time.
        // NOTE(phl): Using |const&| below doesn't work, even though 12.2/5
        // seems to indicate that it should.  A bug in Visual Studio 2013?
        R3Element<Length> const position =
            (trajectory->last().degrees_of_freedom().position() -
             reference_position).coordinates();
        R3Element<Speed> const& velocity =
            trajectory->last().degrees_of_freedom().velocity().coordinates();
        Instant const& time = trajectory->last().time();
        for (int i = 0; i < 3; ++i) {
          parameters.initial.positions.emplace_back(position[i]);
        }
        for (int i = 0; i < 3; ++i) {
          parameters.initial.momenta.emplace_back(velocity[i]);
        }

        // Check that all trajectories are for different bodies.
        auto const inserted = bodies_in_trajectories.emplace(body);
        CHECK(inserted.second) << "Multiple trajectories for the same body";
        // The final points of all trajectories must all be for the same time.
        times_in_trajectories.emplace(time);
        CHECK_GE(1U, times_in_trajectories.size())
            << "Inconsistent last time in trajectories";
      }
    }
  }

  // If |tmax_is_exact| and the trajectories already end at |tmax|, do not call
  // the integrator: it would want to overwrite the last point of each
  // trajectory, which is not something we allow.  It is better to handle this
  // case here than in all the callers.
  CHECK_LE(*times_in_trajectories.cbegin(), tmax);
  if (tmax_is_exact && *times_in_trajectories.cbegin() == tmax) {
    return;
  }

  {
    // Beyond this point we must not use the |trajectories| parameter as it is
    // in the wrong order with respect to the data passed to the integrator.  We
    // use this block to hide it.
    Trajectories const& trajectories = reordered_trajectories;

    parameters.initial.time = *times_in_trajectories.cbegin() - reference_time;
    parameters.tmax = tmax - reference_time;
    parameters.Δt = Δt;
    parameters.sampling_period = sampling_period;
    parameters.tmax_is_exact = tmax_is_exact;
    integrator.SolveTrivialKineticEnergyIncrement<Length>(
        std::bind(&NBodySystem::ComputeGravitationalAccelerations,
                  massive_oblate_trajectories,
                  massive_spherical_trajectories,
                  massless_trajectories,
                  reference_time,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3),
        parameters, &solution);

    // TODO(phl): Ignoring errors for now.
    // Loop over the time steps.
    for (std::size_t i = 0; i < solution.size(); ++i) {
      SRKNIntegrator::SystemState<Length, Speed> const& state = solution[i];
      Instant const time = state.time.value + reference_time;
      CHECK_EQ(state.positions.size(), state.momenta.size());
      // Loop over the dimensions.
      for (std::size_t k = 0, t = 0; k < state.positions.size(); k += 3, ++t) {
        Vector<Length, Frame> const position(
            R3Element<Length>(state.positions[k].value,
                              state.positions[k + 1].value,
                              state.positions[k + 2].value));
        Velocity<Frame> const velocity(
            R3Element<Speed>(state.momenta[k].value,
                             state.momenta[k + 1].value,
                             state.momenta[k + 2].value));
        trajectories[t]->Append(
            time,
            DegreesOfFreedom<Frame>(position + reference_position,
                                            velocity));
      }
    }
  }
}

template<typename Frame>
template<bool body1_is_oblate,
         bool body2_is_oblate,
         bool body2_is_massive>
inline void NBodySystem<Frame>::ComputeOneBodyGravitationalAcceleration(
    MassiveBody const& body1,
    size_t const b1,
    ReadonlyTrajectories const& body2_trajectories,
    size_t const b2_begin,
    size_t const b2_end,
    std::vector<Length> const& q,
    not_null<std::vector<Acceleration>*> const result) {
  // NOTE(phl): Declaring variables for values like 3 * b1 + 1, 3 * b2 + 1, etc.
  // in the code below brings no performance advantage as it seems that the
  // compiler is smart enough to figure common subexpressions.
  GravitationalParameter const& body1_gravitational_parameter =
      body1.gravitational_parameter();
  std::size_t const three_b1 = 3 * b1;
  for (std::size_t b2 = std::max(b1 + 1, b2_begin); b2 < b2_end; ++b2) {
    std::size_t const three_b2 = 3 * b2;
    Length const Δq0 = q[three_b1] - q[three_b2];
    Length const Δq1 = q[three_b1 + 1] - q[three_b2 + 1];
    Length const Δq2 = q[three_b1 + 2] - q[three_b2 + 2];

    Exponentiation<Length, 2> const r_squared =
        Δq0 * Δq0 + Δq1 * Δq1 + Δq2 * Δq2;
    // NOTE(phl): Don't try to compute one_over_r_squared here, it makes the
    // non-oblate path slower.
    Exponentiation<Length, -3> const one_over_r_cubed =
        Sqrt(r_squared) / (r_squared * r_squared);

    auto const μ1_over_r_cubed =
        body1_gravitational_parameter * one_over_r_cubed;
    (*result)[three_b2] += Δq0 * μ1_over_r_cubed;
    (*result)[three_b2 + 1] += Δq1 * μ1_over_r_cubed;
    (*result)[three_b2 + 2] += Δq2 * μ1_over_r_cubed;

    MassiveBody const* body2 = nullptr;
    if (body2_is_massive) {
      // Lex. III. Actioni contrariam semper & æqualem esse reactionem:
      // sive corporum duorum actiones in se mutuo semper esse æquales &
      // in partes contrarias dirigi.
      body2 = body2_trajectories[b2 - b2_begin]->template body<MassiveBody>();
      GravitationalParameter const& body2_gravitational_parameter =
          body2->gravitational_parameter();
      auto const μ2_over_r_cubed =
          body2_gravitational_parameter * one_over_r_cubed;
      (*result)[three_b1] -= Δq0 * μ2_over_r_cubed;
      (*result)[three_b1 + 1] -= Δq1 * μ2_over_r_cubed;
      (*result)[three_b1 + 2] -= Δq2 * μ2_over_r_cubed;
    }

    if (body1_is_oblate || body2_is_oblate) {
      Exponentiation<Length, -2> const one_over_r_squared = 1 / r_squared;
      Vector<Length, Frame> const Δq({Δq0, Δq1, Δq2});
      if (body1_is_oblate) {
        R3Element<Acceleration> const order_2_zonal_acceleration1 =
            Order2ZonalAccelerationLegacy<Frame>(
                static_cast<OblateBody<Frame> const &>(body1),
                Δq,
                one_over_r_squared,
                one_over_r_cubed).coordinates();
        (*result)[three_b2] += order_2_zonal_acceleration1.x;
        (*result)[three_b2 + 1] += order_2_zonal_acceleration1.y;
        (*result)[three_b2 + 2] += order_2_zonal_acceleration1.z;
      }
      if (body2_is_oblate) {
        // |body2| was set in the |body2_is_massive| branch above.
        R3Element<Acceleration> const order_2_zonal_acceleration2 =
            Order2ZonalAccelerationLegacy<Frame>(
                *CHECK_NOTNULL(static_cast<OblateBody<Frame> const*>(body2)),
                Δq,
                one_over_r_squared,
                one_over_r_cubed).coordinates();
        (*result)[three_b1] -= order_2_zonal_acceleration2.x;
        (*result)[three_b1 + 1] -= order_2_zonal_acceleration2.y;
        (*result)[three_b1 + 2] -= order_2_zonal_acceleration2.z;
      }
    }
  }
}

template<typename Frame>
void NBodySystem<Frame>::ComputeGravitationalAccelerations(
    ReadonlyTrajectories const& massive_oblate_trajectories,
    ReadonlyTrajectories const& massive_spherical_trajectories,
    ReadonlyTrajectories const& massless_trajectories,
    Instant const& reference_time,
    Time const& t,
    std::vector<Length> const& q,
    not_null<std::vector<Acceleration>*> const result) {
  result->assign(result->size(), Acceleration());
  size_t const number_of_massive_oblate_trajectories =
      massive_oblate_trajectories.size();
  size_t const number_of_massive_spherical_trajectories =
      massive_spherical_trajectories.size();
  size_t const number_of_massless_trajectories = massless_trajectories.size();

  for (std::size_t b1 = 0; b1 < number_of_massive_oblate_trajectories; ++b1) {
    OblateBody<Frame> const& body1 =
        *massive_oblate_trajectories[b1]->template body<OblateBody<Frame>>();
    ComputeOneBodyGravitationalAcceleration<true /*body1_is_oblate*/,
                                            true /*body2_is_oblate*/,
                                            true /*body2_is_massive*/>(
        body1, b1,
        massive_oblate_trajectories /*body2_trajectories*/,
        0 /*b2_begin*/,
        number_of_massive_oblate_trajectories /*b2_end*/,
        q,
        result);
    ComputeOneBodyGravitationalAcceleration<true /*body1_is_oblate*/,
                                            false /*body2_is_oblate*/,
                                            true /*body2_is_massive*/>(
        body1, b1,
        massive_spherical_trajectories /*body2_trajectories*/,
        number_of_massive_oblate_trajectories /*b2_begin*/,
        number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories /*b2_end*/,
        q,
        result);
    ComputeOneBodyGravitationalAcceleration<true /*body1_is_oblate*/,
                                            false /*body2_is_oblate*/,
                                            false /*body2_is_massive*/>(
        body1, b1,
        massless_trajectories /*body2_trajectories*/,
        number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories /*b2_begin*/,
        number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories +
            number_of_massless_trajectories /*b2_end*/,
        q,
        result);
  }
  for (std::size_t b1 = number_of_massive_oblate_trajectories;
       b1 < number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories;
       ++b1) {
    MassiveBody const& body1 =
        *massive_spherical_trajectories[
            b1 - number_of_massive_oblate_trajectories]->
                template body<MassiveBody>();
    ComputeOneBodyGravitationalAcceleration<false /*body1_is_oblate*/,
                                            false /*body2_is_oblate*/,
                                            true /*body2_is_massive*/>(
        body1, b1,
        massive_spherical_trajectories /*body2_trajectories*/,
        number_of_massive_oblate_trajectories /*b2_begin*/,
        number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories /*b2_end*/,
        q,
        result);
    ComputeOneBodyGravitationalAcceleration<false /*body1_is_oblate*/,
                                            false /*body2_is_oblate*/,
                                            false /*body2_is_massive*/>(
        body1, b1,
        massless_trajectories /*body2_trajectories*/,
        number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories /*b2_begin*/,
        number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories +
            number_of_massless_trajectories /*b2_end*/,
        q,
        result);
  }
  // Finally, take into account the intrinsic accelerations.
  for (size_t b2 = number_of_massive_oblate_trajectories +
                   number_of_massive_spherical_trajectories;
       b2 < number_of_massive_oblate_trajectories +
            number_of_massive_spherical_trajectories +
            number_of_massless_trajectories;
       ++b2) {
    std::size_t const three_b2 = 3 * b2;
    Trajectory<Frame> const* trajectory =
        massless_trajectories[b2 - number_of_massive_oblate_trajectories -
                                   number_of_massive_spherical_trajectories];
    if (trajectory->has_intrinsic_acceleration()) {
      R3Element<Acceleration> const acceleration =
          trajectory->evaluate_intrinsic_acceleration(
              t + reference_time).coordinates();
      (*result)[three_b2] += acceleration.x;
      (*result)[three_b2 + 1] += acceleration.y;
      (*result)[three_b2 + 2] += acceleration.z;
    }
  }
}

}  // namespace physics
}  // namespace principia
