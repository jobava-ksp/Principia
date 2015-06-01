#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "base/not_null.hpp"
#include "geometry/named_quantities.hpp"
#include "integrators/ordinary_differential_equations.hpp"
#include "physics/continuous_trajectory.hpp"
#include "physics/massive_body.hpp"
#include "physics/trajectory.hpp"

namespace principia {

using geometry::Position;
using integrators::AdaptiveStepSizeIntegrator;
using integrators::FixedStepSizeIntegrator;
using integrators::SpecialSecondOrderDifferentialEquation;

namespace physics {

template<typename Frame>
class Ephemeris {
  static_assert(Frame::is_inertial, "Frame must be inertial");

 public:
  // The equation describing the motion of the |bodies_|.
  using NewtonianMotionEquation =
      SpecialSecondOrderDifferentialEquation<Position<Frame>>;
  // We don't specify non-autonomy in NewtonianMotionEquation since there isn't
  // a type for that at this time, so time-dependent intrinsic acceleration
  // yields the same type of map.
  using TimedBurnMotion = NewtonianMotionEquation;

  // Constructs an Ephemeris that owns the |bodies|.  The elements of vectors
  // |bodies| and |initial_state| correspond to one another.
  Ephemeris(
      std::vector<not_null<std::unique_ptr<MassiveBody>>> bodies,
      std::vector<DegreesOfFreedom<Frame>> initial_state,
      Instant const& initial_time,
      FixedStepSizeIntegrator<NewtonianMotionEquation> const&
          planetary_integrator,
      Time const& step,
      Length const& low_fitting_tolerance,
      Length const& high_fitting_tolerance);

  ContinuousTrajectory<Frame> const& trajectory(
      not_null<MassiveBody const*>) const;

  // The maximum of the |t_min|s of the trajectories.
  Instant t_min() const;
  // The mimimum of the |t_max|s of the trajectories.
  Instant t_max() const;

  // Calls |ForgetBefore| on all trajectories.
  void ForgetBefore(Instant const& t);

  // Prolongs the ephemeris up to at least |t|.  After the call, |t_max() >= t|.
  void Prolong(Instant const& t);

  // Integrates, until exactly |t|, the |trajectory| followed by a massless body
  // in the gravitational potential described by |*this|, and subject to the
  // given |intrinsic_acceleration|.
  // If |t > t_max()|, calls |Prolong(t)| beforehand.
  // The |length_| and |speed_integration_tolerance|s are used to compute the
  // |tolerance_to_error_ratio| for step size control.
  //TODO(phl):Remove intrinsic_acceleration?
  void Flow(
      not_null<Trajectory<Frame>*> const trajectory,
      std::function<
          Vector<Acceleration, Frame>(
              Instant const&)> intrinsic_acceleration,
      Length const& length_integration_tolerance,
      Speed const& speed_integration_tolerance,
      AdaptiveStepSizeIntegrator<TimedBurnMotion> integrator,
      Instant const& t);

 private:
  void AppendState(typename NewtonianMotionEquation::SystemState const& state);

  // No transfer of ownership.
  static void ComputeGravitationalAccelerations(
      ReadonlyTrajectories const& massive_oblate_trajectories,
      ReadonlyTrajectories const& massive_spherical_trajectories,
      Instant const& reference_time,
      Time const& t,
      std::vector<Length> const& q,
      not_null<std::vector<Acceleration>*> const result);

  std::vector<std::pair<not_null<std::unique_ptr<MassiveBody>>,
                        ContinuousTrajectory<Frame>>> bodies_and_trajectories_;
  std::map<not_null<MassiveBody const*>,
           not_null<ContinuousTrajectory<Frame> const*>>
           bodies_to_trajectories_;

  // This will refer to a static object returned by a factory.
  FixedStepSizeIntegrator<NewtonianMotionEquation> const& planetary_integrator_;
  Time const step_;
  Length const low_fitting_tolerance_;
  Length const high_fitting_tolerance_;
  typename NewtonianMotionEquation::SystemState last_state_;

  NewtonianMotionEquation equation_;
};

}  // namespace physics
}  // namespace principia

#include "physics/ephemeris_body.hpp"
