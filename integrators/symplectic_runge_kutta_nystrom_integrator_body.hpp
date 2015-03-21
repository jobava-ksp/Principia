﻿#pragma once

#include "integrators/symplectic_runge_kutta_nystrom_integrator.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

#include "glog/logging.h"
#include "quantities/quantities.hpp"

#ifdef ADVANCE_ΔQSTAGE
#error ADVANCE_ΔQSTAGE already defined
#else
#define ADVANCE_ΔQSTAGE(step)                                              \
  do {                                                                     \
    Time const step_evaluated = (step);                                    \
    for (int k = 0; k < dimension; ++k) {                                  \
      Position const Δq = (*Δqstage_previous)[k] +                         \
                              step_evaluated * v_stage[k];                 \
      q_stage[k] = q_last[k].value + Δq;                                   \
      (*Δqstage_current)[k] = Δq;                                          \
    }                                                                      \
  } while (false)
#endif

#ifdef ADVANCE_ΔVSTAGE
#error ADVANCE_ΔVSTAGE already defined
#else
#define ADVANCE_ΔVSTAGE(step, q_clock)                                     \
  do {                                                                     \
    Time const step_evaluated = (step);                                    \
    compute_acceleration((q_clock), q_stage, &a);                          \
    for (int k = 0; k < dimension; ++k) {                                  \
      Velocity const Δv = (*Δvstage_previous)[k] + step_evaluated * a[k];  \
      v_stage[k] = v_last[k].value + Δv;                                   \
      (*Δvstage_current)[k] = Δv;                                          \
    }                                                                      \
  } while (false)
#endif

namespace principia {
namespace integrators {

inline SRKNIntegrator const& McLachlanAtela1992Order4Optimal() {
  static SRKNIntegrator const integrator({ 0.5153528374311229364,
                                          -0.085782019412973646,
                                           0.4415830236164665242,
                                           0.1288461583653841854},
                                         { 0.1344961992774310892,
                                          -0.2248198030794208058,
                                           0.7563200005156682911,
                                           0.3340036032863214255});
  return integrator;
}

inline SRKNIntegrator const& McLachlanAtela1992Order5Optimal() {
  static SRKNIntegrator const integrator({ 0.339839625839110000,
                                          -0.088601336903027329,
                                           0.5858564768259621188,
                                          -0.603039356536491888,
                                           0.3235807965546976394,
                                           0.4423637942197494587},
                                         { 0.1193900292875672758,
                                           0.6989273703824752308,
                                          -0.1713123582716007754,
                                           0.4012695022513534480,
                                           0.0107050818482359840,
                                          -0.0589796254980311632});
  return integrator;
}

inline SRKNIntegrator::SRKNIntegrator(std::vector<double> const& a,
                                      std::vector<double> const& b)
    : a_(a),
      b_(b) {
  if (b.front() == 0.0) {
    vanishing_coefficients_ = kFirstBVanishes;
    first_same_as_last_ = std::make_unique<FirstSameAsLast>();
    first_same_as_last_->first = a.front();
    first_same_as_last_->last = a.back();
    a_ = std::vector<double>(a.begin() + 1, a.end());
    b_ = std::vector<double>(b.begin() + 1, b.end());
    a_.back() += first_same_as_last_->first;
    stages_ = b_.size();
    CHECK_EQ(stages_, a_.size());
  } else if (a.back() == 0.0) {
    vanishing_coefficients_ = kLastAVanishes;
    first_same_as_last_ = std::make_unique<FirstSameAsLast>();
    first_same_as_last_->first = b.front();
    first_same_as_last_->last = b.back();
    a_ = std::vector<double>(a.begin(), a.end() - 1);
    b_ = std::vector<double>(b.begin(), b.end() - 1);
    b_.front() += first_same_as_last_->last;
    stages_ = b_.size();
    CHECK_EQ(stages_, a_.size());
  } else {
    vanishing_coefficients_ = kNone;
    first_same_as_last_.reset();
    a_ = a;
    b_ = b;
    stages_ = b_.size();
    CHECK_EQ(stages_, a_.size());
  }

  // Runge-Kutta time weights.
  c_.resize(stages_);
  if (vanishing_coefficients_ == kFirstBVanishes) {
    c_[0] = first_same_as_last_->first;
  } else {
    c_[0] = 0.0;
  }
  for (int j = 1; j < stages_; ++j) {
    c_[j] = c_[j - 1] + a_[j - 1];
  }
}

template<typename Position, typename RightHandSideComputation>
void SRKNIntegrator::SolveTrivialKineticEnergyIncrement(
    RightHandSideComputation compute_acceleration,
    Parameters<Position, Variation<Position>> const& parameters,
    not_null<Solution<Position, Variation<Position>>*> const solution) const {
  switch (vanishing_coefficients_) {
    case kNone:
      SolveTrivialKineticEnergyIncrementOptimized<kNone>(compute_acceleration,
                                                         parameters,
                                                         solution);
      break;
    case kFirstBVanishes:
      SolveTrivialKineticEnergyIncrementOptimized<kFirstBVanishes>(
          compute_acceleration,
          parameters,
          solution);
      break;
    case kLastAVanishes:
      SolveTrivialKineticEnergyIncrementOptimized<kLastAVanishes>(
          compute_acceleration,
          parameters,
          solution);
      break;
    default:
      LOG(FATAL) << "Invalid vanishing coefficients";
  }
}

template<SRKNIntegrator::VanishingCoefficients vanishing_coefficients,
         typename Position, typename RightHandSideComputation>
void SRKNIntegrator::SolveTrivialKineticEnergyIncrementOptimized(
    RightHandSideComputation compute_acceleration,
    Parameters<Position, Variation<Position>> const& parameters,
    not_null<Solution<Position, Variation<Position>>*> const solution) const {
  using Velocity = Variation<Position>;
  int const dimension = parameters.initial.positions.size();

  std::vector<Position> Δqstage0(dimension);
  std::vector<Position> Δqstage1(dimension);
  std::vector<Velocity> Δvstage0(dimension);
  std::vector<Velocity> Δvstage1(dimension);
  std::vector<Position>* Δqstage_current = &Δqstage1;
  std::vector<Position>* Δqstage_previous = &Δqstage0;
  std::vector<Velocity>* Δvstage_current = &Δvstage1;
  std::vector<Velocity>* Δvstage_previous = &Δvstage0;

  // Dimension the result.
  int const capacity = parameters.sampling_period == 0 ?
    1 :
    static_cast<int>(
        ceil((((parameters.tmax - parameters.initial.time.value) /
                    parameters.Δt) + 1) /
                parameters.sampling_period)) + 1;
  solution->clear();
  solution->reserve(capacity);

  std::vector<DoublePrecision<Position>> q_last(parameters.initial.positions);
  std::vector<DoublePrecision<Velocity>> v_last(parameters.initial.momenta);
  int sampling_phase = 0;

  std::vector<Position> q_stage(dimension);
  std::vector<Velocity> v_stage(dimension);
  std::vector<Quotient<Velocity, Time>> a(dimension);  // Current accelerations.

  // The following quantity is generally equal to |Δt|, but during the last
  // iteration, if |tmax_is_exact|, it may differ significantly from |Δt|.
  Time h = parameters.Δt;  // Constant for now.

  // During one iteration of the outer loop below we process the time interval
  // [|tn|, |tn| + |h|[.  |tn| is computed using compensated summation to make
  // sure that we don't have drifts.
  DoublePrecision<Time> tn = parameters.initial.time;

  // Whether position and velocity are synchronized between steps, relevant for
  // first-same-as-last (FSAL) integrators. Time is always synchronous with
  // position.
  bool q_and_v_are_synchronized = true;
  bool should_synchronize = false;

  // Integration.  For details see Wolfram Reference,
  // http://reference.wolfram.com/mathematica/tutorial/NDSolveSRKN.html#74387056
  bool at_end = !parameters.tmax_is_exact && parameters.tmax < tn.value + h;
  while (!at_end) {
    // Check if this is the last interval and if so process it appropriately.
    if (parameters.tmax_is_exact) {
      // If |tn| is getting close to |tmax|, use |tmax| as the upper bound of
      // the interval and update |h| accordingly.  The bound chosen here for
      // |tmax| ensures that we don't end up with a ridiculously small last
      // interval: we'd rather make the last interval a bit bigger.  More
      // precisely, the last interval generally has a length between 0.5 Δt and
      // 1.5 Δt, unless it is also the first interval.
      // NOTE(phl): This may lead to convergence as bad as (1.5 Δt)^5 rather
      // than Δt^5.
      if (parameters.tmax <= tn.value + 3 * h / 2) {
        at_end = true;
        h = (parameters.tmax - tn.value) - tn.error;
      }
    } else if (parameters.tmax < tn.value + 2 * h) {
      // If the next interval would overshoot, make this the last interval but
      // stick to the same step.
      at_end = true;
    }
    // Here |h| is the length of the current time interval and |tn| is its
    // start.

    // Increment SRKN step from "'SymplecticPartitionedRungeKutta' Method
    // for NDSolve", algorithm 3.
    for (int k = 0; k < dimension; ++k) {
      (*Δqstage_current)[k] = Position();
      (*Δvstage_current)[k] = Velocity();
      q_stage[k] = q_last[k].value;
    }

    if (vanishing_coefficients != kNone) {
      should_synchronize = at_end ||
                           (parameters.sampling_period != 0 &&
                            sampling_phase % parameters.sampling_period == 0);
    }

    if (vanishing_coefficients == kFirstBVanishes &&
        q_and_v_are_synchronized) {
      // Desynchronize.
      std::swap(Δqstage_current, Δqstage_previous);
      for (int k = 0; k < dimension; ++k) {
        v_stage[k] = v_last[k].value;
      }
      ADVANCE_ΔQSTAGE(first_same_as_last_->first * h);
      q_and_v_are_synchronized = false;
    }
    for (int i = 0; i < stages_; ++i) {
      std::swap(Δqstage_current, Δqstage_previous);
      std::swap(Δvstage_current, Δvstage_previous);

      // Beware, the p/q order matters here, the two computations depend on one
      // another.

      // By using |tn.error| below we get a time value which is possibly a wee
      // bit more precise.
      if (vanishing_coefficients == kLastAVanishes &&
          q_and_v_are_synchronized && i == 0) {
        ADVANCE_ΔVSTAGE(first_same_as_last_->first * h,
                        tn.value);
        q_and_v_are_synchronized = false;
      } else {
        ADVANCE_ΔVSTAGE(b_[i] * h, tn.value + (tn.error + c_[i] * h));
      }

      if (vanishing_coefficients == kFirstBVanishes &&
          should_synchronize && i == stages_ - 1) {
        ADVANCE_ΔQSTAGE(first_same_as_last_->last * h);
        q_and_v_are_synchronized = true;
      } else {
        ADVANCE_ΔQSTAGE(a_[i] * h);
      }
    }
    if (vanishing_coefficients == kLastAVanishes && should_synchronize) {
      std::swap(Δvstage_current, Δvstage_previous);
      // TODO(egg): the second parameter below is really just tn.value + h.
      ADVANCE_ΔVSTAGE(first_same_as_last_->last * h,
                      tn.value + h);
      q_and_v_are_synchronized = true;
    }
    // Compensated summation from "'SymplecticPartitionedRungeKutta' Method
    // for NDSolve", algorithm 2.
    for (int k = 0; k < dimension; ++k) {
      q_last[k].Increment((*Δqstage_current)[k]);
      v_last[k].Increment((*Δvstage_current)[k]);
      q_stage[k] = q_last[k].value;
      v_stage[k] = v_last[k].value;
    }
    tn.Increment(h);

    if (parameters.sampling_period != 0) {
      if (sampling_phase % parameters.sampling_period == 0) {
        solution->emplace_back();
        SystemState<Position, Velocity>* state = &solution->back();
        state->time = tn;
        state->positions.reserve(dimension);
        state->momenta.reserve(dimension);
        for (int k = 0; k < dimension; ++k) {
          state->positions.emplace_back(q_last[k]);
          state->momenta.emplace_back(v_last[k]);
        }
      }
      ++sampling_phase;
    }
  }

  if (parameters.sampling_period == 0) {
    solution->emplace_back();
    SystemState<Position, Velocity>* state = &solution->back();
    state->time = tn;
    state->positions.reserve(dimension);
    state->momenta.reserve(dimension);
    for (int k = 0; k < dimension; ++k) {
      state->positions.emplace_back(q_last[k]);
      state->momenta.emplace_back(v_last[k]);
    }
  }
}

}  // namespace integrators
}  // namespace principia

#undef ADVANCE_ΔQSTAGE
#undef ADVANCE_ΔVSTAGE