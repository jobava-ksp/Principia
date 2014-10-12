#pragma once

#include "ksp_plugin/vessel.hpp"

namespace principia {
namespace ksp_plugin {

template<typename Frame>
Vessel<Frame>::Vessel(Celestial<Frame> const* parent)
    : body_(new Body<Frame>(GravitationalParameter())),
      parent_(CHECK_NOTNULL(parent)) {}

template<typename Frame>
Body<Frame> const& Vessel<Frame>::body() const {
  return *body_;
}

template<typename Frame>
Celestial<Frame> const* Vessel<Frame>::parent() const {
  return parent_;
}

template<typename Frame>
Trajectory<Frame>* Vessel<Frame>::history() const {
  return history_.get();
}

template<typename Frame>
Trajectory<Frame>* Vessel<Frame>::prolongation() const {
  return prolongation_;
}

template<typename Frame>
void Vessel<Frame>::set_parent(Celestial<Frame> const* parent) {
  parent_ = CHECK_NOTNULL(parent);
}

template<typename Frame>
void Vessel<Frame>::Append(Instant const& time,
                           DegreesOfFreedom<Frame> const& degrees_of_freedom) {
  history_ = std::make_unique<Trajectory<Barycentre>>(*body_);
  history_->Append(time, degrees_of_freedom);
}

template<typename Frame>
void Vessel<Frame>::ResetProlongation(Instant const& time) {
  history_->DeleteFork(&prolongation_);
  prolongation_ = history_->Fork(time);
}

}  // namespace ksp_plugin
}  // namespace principia
