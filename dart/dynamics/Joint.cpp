/*
 * Copyright (c) 2011-2019, The DART development contributors
 * All rights reserved.
 *
 * The list of contributors can be found at:
 *   https://github.com/dartsim/dart/blob/master/LICENSE
 *
 * This file is provided under the following "BSD-style" License:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 */

#include "dart/dynamics/Joint.hpp"

#include <string>

#include "dart/common/Console.hpp"
#include "dart/dynamics/BallJoint.hpp"
#include "dart/dynamics/BodyNode.hpp"
#include "dart/dynamics/ConstantCurveIncompressibleJoint.hpp"
#include "dart/dynamics/DegreeOfFreedom.hpp"
#include "dart/dynamics/EllipsoidJoint.hpp"
#include "dart/dynamics/FreeJoint.hpp"
#include "dart/dynamics/PrismaticJoint.hpp"
#include "dart/dynamics/Skeleton.hpp"
#include "dart/math/FiniteDifference.hpp"
#include "dart/math/Geometry.hpp"
#include "dart/math/Helpers.hpp"
#include "dart/math/MathTypes.hpp"

namespace dart {
namespace dynamics {

//==============================================================================
const Joint::ActuatorType Joint::DefaultActuatorType
    = detail::DefaultActuatorType;
// These declarations are needed for linking to work
constexpr Joint::ActuatorType Joint::FORCE;
constexpr Joint::ActuatorType Joint::PASSIVE;
constexpr Joint::ActuatorType Joint::SERVO;
constexpr Joint::ActuatorType Joint::MIMIC;
constexpr Joint::ActuatorType Joint::ACCELERATION;
constexpr Joint::ActuatorType Joint::VELOCITY;
constexpr Joint::ActuatorType Joint::LOCKED;

namespace detail {

//==============================================================================
JointProperties::JointProperties(
    const std::string& _name,
    const Eigen::Isometry3s& _T_ParentBodyToJoint,
    const Eigen::Isometry3s& _T_ChildBodyToJoint,
    bool _isPositionLimitEnforced,
    ActuatorType _actuatorType,
    const Joint* _mimicJoint,
    s_t _mimicMultiplier,
    s_t _mimicOffset)
  : mName(_name),
    mT_ParentBodyToJoint(_T_ParentBodyToJoint),
    mT_ChildBodyToJoint(_T_ChildBodyToJoint),
    mParentScale(Eigen::Vector3s::Ones()),
    mChildScale(Eigen::Vector3s::Ones()),
    mOriginalParentTranslation(_T_ParentBodyToJoint.translation()),
    mOriginalChildTranslation(_T_ChildBodyToJoint.translation()),
    mIsPositionLimitEnforced(_isPositionLimitEnforced),
    mActuatorType(_actuatorType),
    mMimicJoint(_mimicJoint),
    mMimicMultiplier(_mimicMultiplier),
    mMimicOffset(_mimicOffset)
{
  // Do nothing
}

} // namespace detail

//==============================================================================
Joint::ExtendedProperties::ExtendedProperties(
    const Properties& standardProperties,
    const CompositeProperties& aspectProperties)
  : Properties(standardProperties), mCompositeProperties(aspectProperties)
{
  // Do nothing
}

//==============================================================================
Joint::ExtendedProperties::ExtendedProperties(
    Properties&& standardProperties, CompositeProperties&& aspectProperties)
  : Properties(std::move(standardProperties)),
    mCompositeProperties(std::move(aspectProperties))
{
  // Do nothing
}

//==============================================================================
/// Create a clone of this Joint, or (if this joint cannot be represented in
/// SDF or MJCF, like CustomJoint's) create a simplified approximation of this
/// joint.
Joint* Joint::simplifiedClone() const
{
  return clone();
}

//==============================================================================
Joint::~Joint()
{
  // Do nothing
}

//==============================================================================
void Joint::setProperties(const Properties& properties)
{
  setAspectProperties(properties);
}

//==============================================================================
void Joint::setAspectProperties(const AspectProperties& properties)
{
  setName(properties.mName);
  setTransformFromParentBodyNode(properties.mT_ParentBodyToJoint);
  mAspectProperties.mParentScale = properties.mParentScale;
  mAspectProperties.mOriginalParentTranslation
      = properties.mOriginalParentTranslation;
  setTransformFromChildBodyNode(properties.mT_ChildBodyToJoint);
  mAspectProperties.mChildScale = properties.mChildScale;
  mAspectProperties.mOriginalChildTranslation
      = properties.mOriginalChildTranslation;
  setPositionLimitEnforced(properties.mIsPositionLimitEnforced);
  setActuatorType(properties.mActuatorType);
  setMimicJoint(
      properties.mMimicJoint,
      properties.mMimicMultiplier,
      properties.mMimicOffset);
}

//==============================================================================
const Joint::Properties& Joint::getJointProperties() const
{
  return mAspectProperties;
}

//==============================================================================
void Joint::copy(const Joint& _otherJoint)
{
  if (this == &_otherJoint)
    return;

  setProperties(_otherJoint.getJointProperties());
}

//==============================================================================
void Joint::copy(const Joint* _otherJoint)
{
  if (nullptr == _otherJoint)
    return;

  copy(*_otherJoint);
}

//==============================================================================
Joint& Joint::operator=(const Joint& _otherJoint)
{
  copy(_otherJoint);
  return *this;
}

//==============================================================================
const std::string& Joint::setName(const std::string& _name, bool _renameDofs)
{
  if (mAspectProperties.mName == _name)
  {
    if (_renameDofs)
      updateDegreeOfFreedomNames();
    return mAspectProperties.mName;
  }

  const SkeletonPtr& skel
      = mChildBodyNode ? mChildBodyNode->getSkeleton() : nullptr;
  if (skel)
  {
    skel->mNameMgrForJoints.removeName(mAspectProperties.mName);
    mAspectProperties.mName = _name;

    skel->addEntryToJointNameMgr(this, _renameDofs);
  }
  else
  {
    mAspectProperties.mName = _name;

    if (_renameDofs)
      updateDegreeOfFreedomNames();
  }

  return mAspectProperties.mName;
}

//==============================================================================
const std::string& Joint::getName() const
{
  return mAspectProperties.mName;
}

//==============================================================================
void Joint::setActuatorType(Joint::ActuatorType _actuatorType)
{
  mAspectProperties.mActuatorType = _actuatorType;
}

//==============================================================================
Joint::ActuatorType Joint::getActuatorType() const
{
  return mAspectProperties.mActuatorType;
}

//==============================================================================
void Joint::setMimicJoint(
    const Joint* _mimicJoint, s_t _mimicMultiplier, s_t _mimicOffset)
{
  mAspectProperties.mMimicJoint = _mimicJoint;
  mAspectProperties.mMimicMultiplier = _mimicMultiplier;
  mAspectProperties.mMimicOffset = _mimicOffset;
}

//==============================================================================
const Joint* Joint::getMimicJoint() const
{
  return mAspectProperties.mMimicJoint;
}

//==============================================================================
s_t Joint::getMimicMultiplier() const
{
  return mAspectProperties.mMimicMultiplier;
}

//==============================================================================
s_t Joint::getMimicOffset() const
{
  return mAspectProperties.mMimicOffset;
}

//==============================================================================
bool Joint::isKinematic() const
{
  switch (mAspectProperties.mActuatorType)
  {
    case FORCE:
    case PASSIVE:
    case SERVO:
    case MIMIC:
      return false;
    case ACCELERATION:
    case VELOCITY:
    case LOCKED:
      return true;
    default: {
      dterr << "Unsupported actuator type." << std::endl;
      return false;
    }
  }
}

//==============================================================================
bool Joint::isDynamic() const
{
  return !isKinematic();
}

//==============================================================================
/// Return true if this joint has the same upperlimit and lowerlimit on
/// positions
bool Joint::isFixed() const
{
  return getPositionUpperLimits() == getPositionLowerLimits();
}

//==============================================================================
BodyNode* Joint::getChildBodyNode()
{
  return mChildBodyNode;
}

//==============================================================================
const BodyNode* Joint::getChildBodyNode() const
{
  return mChildBodyNode;
}

//==============================================================================
BodyNode* Joint::getParentBodyNode()
{
  if (mChildBodyNode)
    return mChildBodyNode->getParentBodyNode();

  return nullptr;
}

//==============================================================================
const BodyNode* Joint::getParentBodyNode() const
{
  return const_cast<Joint*>(this)->getParentBodyNode();
}

//==============================================================================
SkeletonPtr Joint::getSkeleton()
{
  return mChildBodyNode ? mChildBodyNode->getSkeleton() : nullptr;
}

//==============================================================================
std::shared_ptr<const Skeleton> Joint::getSkeleton() const
{
  return mChildBodyNode ? mChildBodyNode->getSkeleton() : nullptr;
}

//==============================================================================
const Eigen::Isometry3s& Joint::getLocalTransform() const
{
  return getRelativeTransform();
}

//==============================================================================
const Eigen::Vector6s& Joint::getLocalSpatialVelocity() const
{
  return getRelativeSpatialVelocity();
}

//==============================================================================
const Eigen::Vector6s& Joint::getLocalSpatialAcceleration() const
{
  return getRelativeSpatialAcceleration();
}

//==============================================================================
const Eigen::Vector6s& Joint::getLocalPrimaryAcceleration() const
{
  return getRelativePrimaryAcceleration();
}

//==============================================================================
const math::Jacobian Joint::getLocalJacobian() const
{
  return getRelativeJacobian();
}

//==============================================================================
math::Jacobian Joint::getLocalJacobian(const Eigen::VectorXs& positions) const
{
  return getRelativeJacobian(positions);
}

//==============================================================================
const math::Jacobian Joint::getLocalJacobianTimeDeriv() const
{
  return getRelativeJacobianTimeDeriv();
}

//==============================================================================
const Eigen::Isometry3s& Joint::getRelativeTransform() const
{
  if (mNeedTransformUpdate)
  {
    updateRelativeTransform();
    mNeedTransformUpdate = false;
  }

  return mT;
}

//==============================================================================
const Eigen::Vector6s& Joint::getRelativeSpatialVelocity() const
{
  if (mNeedSpatialVelocityUpdate)
  {
    updateRelativeSpatialVelocity();
    mNeedSpatialVelocityUpdate = false;
  }

  return mSpatialVelocity;
}

//==============================================================================
const Eigen::Vector6s& Joint::getRelativeSpatialAcceleration() const
{
  if (mNeedSpatialAccelerationUpdate)
  {
    updateRelativeSpatialAcceleration();
    mNeedSpatialAccelerationUpdate = false;
  }

  return mSpatialAcceleration;
}

//==============================================================================
const Eigen::Vector6s& Joint::getRelativePrimaryAcceleration() const
{
  if (mNeedPrimaryAccelerationUpdate)
  {
    updateRelativePrimaryAcceleration();
    mNeedPrimaryAccelerationUpdate = false;
  }

  return mPrimaryAcceleration;
}

//==============================================================================
/// Gets the derivative of the spatial Jacobian of the child BodyNode relative
/// to the parent BodyNode expressed in the child BodyNode frame, with respect
/// to the scaling of the parent body along a specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
math::Jacobian Joint::getRelativeJacobianDerivWrtParentScale(int /*axis*/) const
{
  return Eigen::MatrixXs::Zero(6, getNumDofs());
}

//==============================================================================
/// This uses finite differencing to compute the changes to the relative
/// position with respect to changes in the parent body's scale along a
/// specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::MatrixXs Joint::finiteDifferenceRelativeJacobianDerivWrtParentScale(
    int axis)
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::Vector3s originalParentScale = getParentScale();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::Vector3s perturbedScale = originalParentScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setParentScale(perturbedScale);
        perturbed = getRelativeJacobian();
        return true;
      },
      result,
      eps,
      useRidders);

  setParentScale(originalParentScale);

  return result;
}

//==============================================================================
/// Gets the derivative of the spatial Jacobian of the child BodyNode relative
/// to the parent BodyNode expressed in the child BodyNode frame, with respect
/// to the scaling of the child body along a specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
math::Jacobian Joint::getRelativeJacobianDerivWrtChildScale(int axis) const
{
  math::Jacobian J = getRelativeJacobian();

  /*
  //--------------------------------------------------------------------------
  // w' = R*w
  // v' = p x R*w + R*v
  //--------------------------------------------------------------------------
  Eigen::Vector6s res;
  res.head<3>().noalias() = _T.linear() * _V.head<3>();
  res.tail<3>().noalias()
      = _T.linear() * _V.tail<3>() + _T.translation().cross(res.head<3>());
  */

  Eigen::Vector3s dTrans = Joint::getOriginalTransformFromChildBodyNode();
  if (axis != -1)
  {
    dTrans = dTrans.cwiseProduct(Eigen::Vector3s::Unit(axis));
  }

  for (int i = 0; i < J.cols(); i++)
  {
    J.block<3, 1>(3, i) = dTrans.cross(J.block<3, 1>(0, i));
    J.block<3, 1>(0, i).setZero();
  }

  return J;
}

//==============================================================================
/// This uses finite differencing to compute the changes to the relative
/// position with respect to changes in the child body's scale along a
/// specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::MatrixXs Joint::finiteDifferenceRelativeJacobianDerivWrtChildScale(
    int axis)
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::Vector3s originalChildScale = getParentScale();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::Vector3s perturbedScale = originalChildScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setChildScale(perturbedScale);
        perturbed = getRelativeJacobian();
        return true;
      },
      result,
      eps,
      useRidders);

  setChildScale(originalChildScale);

  return result;
}

//==============================================================================
/// This gets the change in world translation of the child body, with respect
/// to an axis of parent scaling. Use axis = -1 for uniform scaling of all the
/// axis.
Eigen::Vector3s Joint::getWorldTranslationOfChildBodyWrtParentScale(
    int axis) const
{
  const dynamics::BodyNode* parentBody = getParentBodyNode();
  if (parentBody == nullptr)
  {
    return Eigen::Vector3s::Zero();
  }

  Eigen::Matrix3s R = parentBody->getWorldTransform().linear();
  Eigen::Vector3s parentOffset = getTransformFromParentBodyNode().translation();
  if (axis == -1)
  {
    return R * parentOffset.cwiseQuotient(getParentScale());
  }
  else
  {
    return (R.col(axis) * parentOffset(axis)) / getParentScale()(axis);
  }
}

//==============================================================================
/// This gets the change in world translation of the child body, with respect
/// to an axis of child scaling. Use axis = -1 for uniform scaling of all the
/// axis.
Eigen::Vector3s Joint::getWorldTranslationOfChildBodyWrtChildScale(
    int axis) const
{
  Eigen::Matrix3s R = getChildBodyNode()->getWorldTransform().linear();
  Eigen::Vector3s parentOffset = getTransformFromChildBodyNode().translation();
  if (axis == -1)
  {
    return -R * parentOffset.cwiseQuotient(getChildScale());
  }
  else
  {
    return -(R.col(axis) * parentOffset(axis)) / getChildScale()(axis);
  }
}

//==============================================================================
Eigen::Vector3s
Joint::finiteDifferenceWorldTranslationOfChildBodyWrtParentScale(int axis)
{
  Eigen::Vector3s originalParentScale = getParentScale();

  Eigen::Vector3s dT;
  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::Vector3s>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::Vector3s& perturbed) {
        Eigen::Vector3s perturbedScale = originalParentScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setParentScale(perturbedScale);
        updateRelativeTransform();
        perturbed = getChildBodyNode()->getWorldTransform().translation();
        return true;
      },
      dT,
      eps,
      useRidders);

  setParentScale(originalParentScale);

  return dT;
}

//==============================================================================
Eigen::Vector3s Joint::finiteDifferenceWorldTranslationOfChildBodyWrtChildScale(
    int axis)
{
  Eigen::Vector3s originalChildScale = getChildScale();

  Eigen::Vector3s dT;
  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::Vector3s>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::Vector3s& perturbed) {
        Eigen::Vector3s perturbedScale = originalChildScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setChildScale(perturbedScale);

        updateRelativeTransform();

        perturbed = getChildBodyNode()->getWorldTransform().translation();
        return true;
      },
      dT,
      eps,
      useRidders);

  setChildScale(originalChildScale);

  return dT;
}

//==============================================================================
/// This gets the column of "H" for the GEAR paper derivations, which is
/// defined as: log(T_{parent,self}^{-1} * dT_{parent,self}/dp) where "p" is
/// the scalar value we are changing.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::Vector6s Joint::getLocalTransformScrewWrtParentScale(int axis) const
{
  Eigen::Isometry3s T = getRelativeTransform();
  Eigen::Vector3s dTrans = Joint::getOriginalTransformFromParentBodyNode();
  if (axis != -1)
  {
    dTrans = dTrans.cwiseProduct(Eigen::Vector3s::Unit(axis));
  }
  return math::AdTLinear(T.inverse(), dTrans);
}

//==============================================================================
/// This gets the column of "H" for the GEAR paper derivations, which is
/// defined as: log(T_{parent,self}^{-1} * dT_{parent,self}/dp) where "p" is
/// the scalar value we are changing.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::Vector6s Joint::finiteDifferenceLocalTransformScrewWrtParentScale(
    int axis)
{
  Eigen::Vector3s originalParentScale = getParentScale();
  Eigen::Isometry3s originalT = getRelativeTransform();

  Eigen::Vector6s dT;
  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::Vector6s>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::Vector6s& perturbed) {
        Eigen::Vector3s perturbedScale = originalParentScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setParentScale(perturbedScale);
        perturbed = math::logMap(originalT.inverse() * getRelativeTransform());
        return true;
      },
      dT,
      eps,
      useRidders);

  setParentScale(originalParentScale);

  return dT;
}

//==============================================================================
/// This gets the column of "H" for the GEAR paper derivations, which is
/// defined as: log(T_{parent,self}^{-1} * dT_{parent,self}/dp) where "p" is
/// the scalar value we are changing.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::Vector6s Joint::getLocalTransformScrewWrtChildScale(int axis) const
{
  /*
  mT = Joint::mAspectProperties.mT_ParentBodyToJoint * mQ
       * Joint::mAspectProperties.mT_ChildBodyToJoint.inverse();
  */
  Eigen::Vector3s dTrans = Joint::getOriginalTransformFromChildBodyNode();
  if (axis != -1)
  {
    dTrans = dTrans.cwiseProduct(Eigen::Vector3s::Unit(axis));
  }

  Eigen::Vector6s result = Eigen::Vector6s::Zero();
  // Joint::mAspectProperties.mT_ChildBodyToJoint
  // result = math::AdTLinear(Joint::mAspectProperties.mT_ChildBodyToJoint,
  // dTrans);

  /*
  Analytical:
       0
       0
       0
0.982396
 1.02114
 0.11545
 */

  // result = math::AdTLinear(
  //     getRelativeTransform()
  //         * Joint::mAspectProperties.mT_ChildBodyToJoint.inverse(),
  //     -dTrans);

  result.tail<3>() = -dTrans;
  return result;
}

//==============================================================================
/// This gets the column of "H" for the GEAR paper derivations, which is
/// defined as: log(T_{parent,self}^{-1} * dT_{parent,self}/dp) where "p" is
/// the scalar value we are changing.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::Vector6s Joint::finiteDifferenceLocalTransformScrewWrtChildScale(
    int axis)
{
  Eigen::Vector3s originalChildScale = getChildScale();
  Eigen::Isometry3s originalT = getRelativeTransform();

  Eigen::Vector6s dT;
  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::Vector6s>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::Vector6s& perturbed) {
        Eigen::Vector3s perturbedScale = originalChildScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setChildScale(perturbedScale);
        perturbed = math::logMap(originalT.inverse() * getRelativeTransform());
        return true;
      },
      dT,
      eps,
      useRidders);

  setChildScale(originalChildScale);

  return dT;
}

//==============================================================================
/// Gets the derivative of the time derivative of the spatial Jacobian of the
/// child BodyNode relative to the parent BodyNode expressed in the child
/// BodyNode frame, with respect to the scaling of the parent body along a
/// specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
math::Jacobian Joint::getRelativeJacobianTimeDerivDerivWrtParentScale(
    int /*axis*/) const
{
  return Eigen::MatrixXs::Zero(6, getNumDofs());
}

//==============================================================================
/// This uses finite differencing to compute the changes to the time deriv of
/// the relative Jacobian with respect to changes in the parent body's scale
/// along a specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::MatrixXs
Joint::finiteDifferenceRelativeJacobianTimeDerivDerivWrtParentScale(int axis)
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::Vector3s originalParentScale = getParentScale();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::Vector3s perturbedScale = originalParentScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setParentScale(perturbedScale);
        perturbed = getRelativeJacobianTimeDeriv();
        return true;
      },
      result,
      eps,
      useRidders);

  setParentScale(originalParentScale);

  return result;
}

/// Gets the derivative of the time derivative of the spatial Jacobian of the
/// child BodyNode relative to the parent BodyNode expressed in the child
/// BodyNode frame, with respect to the scaling of the child body along a
/// specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
math::Jacobian Joint::getRelativeJacobianTimeDerivDerivWrtChildScale(
    int axis) const
{
  math::Jacobian J = getRelativeJacobianTimeDeriv();

  /*
  //--------------------------------------------------------------------------
  // w' = R*w
  // v' = p x R*w + R*v
  //--------------------------------------------------------------------------
  Eigen::Vector6s res;
  res.head<3>().noalias() = _T.linear() * _V.head<3>();
  res.tail<3>().noalias()
      = _T.linear() * _V.tail<3>() + _T.translation().cross(res.head<3>());
  */

  Eigen::Vector3s dTrans = Joint::getOriginalTransformFromChildBodyNode();
  if (axis != -1)
  {
    dTrans = dTrans.cwiseProduct(Eigen::Vector3s::Unit(axis));
  }

  for (int i = 0; i < J.cols(); i++)
  {
    J.block<3, 1>(3, i) = dTrans.cross(J.block<3, 1>(0, i));
    J.block<3, 1>(0, i).setZero();
  }

  return J;
}

//==============================================================================
/// This uses finite differencing to compute the changes to the time deriv of
/// the relative Jacobian with respect to changes in the child body's scale
/// along a specific axis.
///
/// Use axis = -1 for uniform scaling of all the axis.
Eigen::MatrixXs
Joint::finiteDifferenceRelativeJacobianTimeDerivDerivWrtChildScale(int axis)
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::Vector3s originalChildScale = getParentScale();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::Vector3s perturbedScale = originalChildScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setChildScale(perturbedScale);
        perturbed = getRelativeJacobianTimeDeriv();
        return true;
      },
      result,
      eps,
      useRidders);

  setChildScale(originalChildScale);

  return result;
}

//==============================================================================
/// This gets the relative spatial velocity of the joint, with finite
/// differencing
Eigen::Vector6s Joint::finiteDifferenceRelativeSpatialVelocity()
{
  Eigen::VectorXs pos = getPositions();
  Eigen::VectorXs vel = getVelocities();

  bool useRidders = true;
  s_t eps = 1e-2;
  Eigen::Isometry3s originalT = getRelativeTransform();

  Eigen::Vector6s result;
  math::finiteDifference<Eigen::Vector6s>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::Vector6s& perturbed) {
        setPositions(pos + (vel * eps));
        perturbed = math::logMap(originalT.inverse() * getRelativeTransform());
        return true;
      },
      result,
      eps,
      useRidders);
  setPositions(pos);

  return result;
}

//==============================================================================
Eigen::MatrixXs Joint::finiteDifferenceRelativeJacobian()
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::VectorXs originalVelocity = getVelocities();

  // Changes in velocity should be linearly related to changes in speed
  for (int i = 0; i < getNumDofs(); i++)
  {
    setVelocities(Eigen::VectorXs::Unit(getNumDofs(), i));
    result.col(i) = finiteDifferenceRelativeSpatialVelocity();
  }

  // This should be equivalent
  // bool useRidders = true;
  // s_t eps = 1e-3;
  // math::finiteDifference(
  //     [&](/* in*/ s_t eps,
  //         /* in*/ int dof,
  //         /*out*/ Eigen::VectorXs& perturbed) {
  //       s_t original = getVelocity(dof);
  //       setVelocity(dof, original + eps);
  //       perturbed = finiteDifferenceRelativeSpatialVelocity();
  //       setVelocity(dof, original);
  //       return true;
  //     },
  //     result,
  //     eps,
  //     useRidders);

  setVelocities(originalVelocity);

  return result;
}

//==============================================================================
/// This uses finite differencing to compute the relative Jacobian derivative
/// wrt the position of `dof`
Eigen::MatrixXs Joint::finiteDifferenceRelativeJacobianDerivWrtPosition(int dof)
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::VectorXs pos = getPositions();
  Eigen::VectorXs vel = getVelocities();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::VectorXs tweaked = pos;
        tweaked(dof) += eps;
        setPositions(tweaked);
        perturbed = getRelativeJacobian();
        return true;
      },
      result,
      eps,
      useRidders);

  setPositions(pos);

  return result;
}

//==============================================================================
/// This uses finite differencing to compute the relative Jacobian time
/// derivative
Eigen::MatrixXs Joint::finiteDifferenceRelativeJacobianTimeDeriv()
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::VectorXs pos = getPositions();
  Eigen::VectorXs vel = getVelocities();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::VectorXs tweaked = pos + eps * vel;
        setPositions(tweaked);
        perturbed = getRelativeJacobian();
        return true;
      },
      result,
      eps,
      useRidders);

  setPositions(pos);

  return result;
}

//==============================================================================
/// This uses finite differencing to compute the relative Jacobian time
/// derivative, derivative wrt position of `dof`
Eigen::MatrixXs
Joint::finiteDifferenceRelativeJacobianTimeDerivDerivWrtPosition(int dof)
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::VectorXs pos = getPositions();
  Eigen::VectorXs vel = getVelocities();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::VectorXs tweaked = pos;
        tweaked(dof) += eps;
        setPositions(tweaked);
        perturbed = getRelativeJacobianTimeDeriv();
        return true;
      },
      result,
      eps,
      useRidders);

  setPositions(pos);

  return result;
}

//==============================================================================
/// This uses finite differencing to compute the relative Jacobian time
/// derivative, derivative wrt velocity of `dof`
Eigen::MatrixXs
Joint::finiteDifferenceRelativeJacobianTimeDerivDerivWrtVelocity(int dof)
{
  Eigen::MatrixXs result(6, getNumDofs());

  Eigen::VectorXs pos = getPositions();
  Eigen::VectorXs vel = getVelocities();

  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::MatrixXs>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::MatrixXs& perturbed) {
        Eigen::VectorXs tweaked = vel;
        tweaked(dof) += eps;
        setVelocities(tweaked);
        perturbed = getRelativeJacobianTimeDeriv();
        return true;
      },
      result,
      eps,
      useRidders);

  setVelocities(vel);

  return result;
}

//==============================================================================
Eigen::MatrixXs Joint::finiteDifferenceRelativeJacobianInPositionSpace(
    bool useRidders)
{
  Eigen::Isometry3s T = getRelativeTransform();
  Eigen::MatrixXs result(6, getNumDofs());

  s_t eps = useRidders ? 1e-2 : 1e-5;
  math::finiteDifference(
      [&](/* in*/ s_t eps,
          /* in*/ int dof,
          /*out*/ Eigen::VectorXs& perturbed) {
        s_t original = getPosition(dof);
        setPosition(dof, original + eps);
        perturbed = math::logMap(T.inverse() * getRelativeTransform());
        setPosition(dof, original);
        return true;
      },
      result,
      eps,
      useRidders);
  return result;
}

//==============================================================================
void Joint::debugRelativeJacobianInPositionSpace()
{
  Eigen::MatrixXs bruteForce
      = finiteDifferenceRelativeJacobianInPositionSpace();
  Eigen::MatrixXs analytical = getRelativeJacobianInPositionSpace();
  const s_t threshold = 1e-9;
  if (((bruteForce - analytical).cwiseAbs().array() > threshold).any())
  {
    std::cout << "Relative Jacobian (in position space) disagrees on joint \""
              << getName() << "\" of type \"" << getType() << "\"!"
              << std::endl;
    std::cout << "Analytical:" << std::endl << analytical << std::endl;
    std::cout << "Brute Force:" << std::endl << bruteForce << std::endl;
    std::cout << "Diff (" << (analytical - bruteForce).minCoeff() << ","
              << (analytical - bruteForce).maxCoeff() << "):" << std::endl
              << analytical - bruteForce << std::endl;
  }
}

//==============================================================================
Eigen::Vector6s Joint::getWorldAxisScrewForPosition(int dof) const
{
  assert(dof >= 0 && dof < getNumDofs());
  return math::AdT(
      getChildBodyNode()->getWorldTransform(),
      getRelativeJacobianInPositionSpace().col(dof));
}

//==============================================================================
Eigen::Vector6s Joint::getWorldAxisScrewForVelocity(int dof) const
{
  assert(dof >= 0 && dof < getNumDofs());
  return math::AdT(
      getChildBodyNode()->getWorldTransform(), getRelativeJacobian().col(dof));
}

//==============================================================================
// Returns the gradient of the screw axis with respect to the rotate dof
Eigen::Vector6s Joint::getScrewAxisGradientForPosition(
    int axisDof, int rotateDof)
{
  // TODO: check if this works
  // getRelativeJacobianDerivWrtPosition(rotateDof).col(axisDof);

  // Defaults to Finite Differencing - this is slow, but at least it's
  // approximately correct. Child joints should override with a faster
  // implementation.
  return finiteDifferenceScrewAxisGradientForPosition(axisDof, rotateDof);
}

//==============================================================================
// Returns the gradient of the screw axis with respect to the rotate dof
Eigen::Vector6s Joint::getScrewAxisGradientForForce(int axisDof, int rotateDof)
{
  // Defaults to Finite Differencing - this is slow, but at least it's
  // approximately correct. Child joints should override with a faster
  // implementation.
  return finiteDifferenceScrewAxisGradientForForce(axisDof, rotateDof);
}

//==============================================================================
/// This uses finite differencing to compute the gradient of the screw axis on
/// `axisDof` as we rotate `rotateDof`.
Eigen::Vector6s Joint::finiteDifferenceScrewAxisGradientForPosition(
    int axisDof, int rotateDof)
{
  const s_t EPS = 1e-7;
  s_t original = getPosition(rotateDof);

  setPosition(rotateDof, original + EPS);
  Eigen::Vector6s plus = getWorldAxisScrewForPosition(axisDof);

  setPosition(rotateDof, original - EPS);
  Eigen::Vector6s minus = getWorldAxisScrewForPosition(axisDof);

  setPosition(rotateDof, original);

  return (plus - minus) / (2 * EPS);
}

//==============================================================================
/// This uses finite differencing to compute the gradient of the screw axis on
/// `axisDof` as we rotate `rotateDof`.
Eigen::Vector6s Joint::finiteDifferenceScrewAxisGradientForForce(
    int axisDof, int rotateDof)
{
  const s_t EPS = 1e-7;
  s_t original = getPosition(rotateDof);

  setPosition(rotateDof, original + EPS);
  Eigen::Vector6s plus = getWorldAxisScrewForVelocity(axisDof);

  setPosition(rotateDof, original - EPS);
  Eigen::Vector6s minus = getWorldAxisScrewForVelocity(axisDof);

  setPosition(rotateDof, original);

  return (plus - minus) / (2 * EPS);
}

//==============================================================================
// Returns the gradient of the screw axis with respect to the scaling axis of
// the child body
Eigen::Vector6s Joint::getScrewAxisGradientWrtChildBodyScale(
    int axisDof, int axis)
{
  // if (getType() == ConstantCurveIncompressibleJoint::getStaticType()
  //     || getType() == EllipsoidJoint::getStaticType())
  // {
  //   return getRelativeJacobianDerivWrtChildScale(axis).col(axisDof);
  // }
  return finiteDifferenceScrewAxisGradientWrtChildBodyScale(axisDof, axis);
}

//==============================================================================
// Returns the gradient of the screw axis with respect to the scaling axis of
// the parent body
Eigen::Vector6s Joint::getScrewAxisGradientWrtParentBodyScale(
    int axisDof, int axis)
{
  // if (getType() == ConstantCurveIncompressibleJoint::getStaticType()
  //     || getType() == EllipsoidJoint::getStaticType())
  // {
  //   return getRelativeJacobianDerivWrtParentScale(axis).col(axisDof);
  // }
  return finiteDifferenceScrewAxisGradientWrtParentBodyScale(axisDof, axis);
}

//==============================================================================
// Returns the gradient of the screw axis with respect to the scaling axis of
// the child body
Eigen::Vector6s Joint::finiteDifferenceScrewAxisGradientWrtChildBodyScale(
    int axisDof, int axis)
{
  Eigen::Vector3s originalChildScale = getChildScale();

  Eigen::Isometry3s childT = getChildBodyNode()->getWorldTransform();

  Eigen::Vector6s dT;
  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::Vector6s>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::Vector6s& perturbed) {
        Eigen::Vector3s perturbedScale = originalChildScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setChildScale(perturbedScale);
        updateRelativeTransform();

        // perturbed = getWorldAxisScrewForPosition(axisDof);
        // perturbed = math::AdR(
        //     getChildBodyNode()->getWorldTransform(),
        //     getRelativeJacobianInPositionSpace().col(axisDof));
        perturbed = math::AdR(
            childT, getRelativeJacobianInPositionSpace().col(axisDof));
        return true;
      },
      dT,
      eps,
      useRidders);

  setChildScale(originalChildScale);

  return dT;
}

//==============================================================================
// Returns the gradient of the screw axis with respect to the scaling axis of
// the parent body
Eigen::Vector6s Joint::finiteDifferenceScrewAxisGradientWrtParentBodyScale(
    int axisDof, int axis)
{
  Eigen::Vector3s originalParentScale = getParentScale();

  Eigen::Isometry3s childT = getChildBodyNode()->getWorldTransform();

  Eigen::Vector6s dT;
  bool useRidders = true;
  s_t eps = 1e-3;
  math::finiteDifference<Eigen::Vector6s>(
      [&](/* in*/ s_t eps,
          /*out*/ Eigen::Vector6s& perturbed) {
        Eigen::Vector3s perturbedScale = originalParentScale;
        if (axis == -1)
        {
          perturbedScale += Eigen::Vector3s::Ones() * eps;
        }
        else
        {
          perturbedScale += Eigen::Vector3s::Unit(axis) * eps;
        }
        setParentScale(perturbedScale);
        updateRelativeTransform();

        // perturbed = getWorldAxisScrewForPosition(axisDof);
        // perturbed = math::AdR(
        //     getChildBodyNode()->getWorldTransform(),
        //     getRelativeJacobianInPositionSpace().col(axisDof));
        perturbed = math::AdR(
            childT, getRelativeJacobianInPositionSpace().col(axisDof));
        return true;
      },
      dT,
      eps,
      useRidders);

  setParentScale(originalParentScale);

  return dT;
}

//==============================================================================
void Joint::setPositionLimitEnforced(bool _isPositionLimitEnforced)
{
  mAspectProperties.mIsPositionLimitEnforced = _isPositionLimitEnforced;
}

//==============================================================================
bool Joint::isPositionLimitEnforced() const
{
  return mAspectProperties.mIsPositionLimitEnforced;
}

//==============================================================================
std::size_t Joint::getJointIndexInSkeleton() const
{
  return mChildBodyNode->getIndexInSkeleton();
}

//==============================================================================
std::size_t Joint::getJointIndexInTree() const
{
  return mChildBodyNode->getIndexInTree();
}

//==============================================================================
std::size_t Joint::getTreeIndex() const
{
  return mChildBodyNode->getTreeIndex();
}

//==============================================================================
bool Joint::checkSanity(bool _printWarnings) const
{
  bool sane = true;
  for (std::size_t i = 0; i < getNumDofs(); ++i)
  {
    if (getInitialPosition(i) < getPositionLowerLimit(i)
        || getPositionUpperLimit(i) < getInitialPosition(i))
    {
      if (_printWarnings)
      {
        dtwarn << "[Joint::checkSanity] Initial position of index " << i << " ["
               << getDofName(i) << "] in Joint [" << getName() << "] is "
               << "outside of its position limits\n"
               << " -- Initial Position: " << getInitialPosition(i) << "\n"
               << " -- Limits: [" << getPositionLowerLimit(i) << ", "
               << getPositionUpperLimit(i) << "]\n";
      }
      else
      {
        return false;
      }

      sane = false;
    }

    if (getInitialVelocity(i) < getVelocityLowerLimit(i)
        || getVelocityUpperLimit(i) < getInitialVelocity(i))
    {
      if (_printWarnings)
      {
        dtwarn << "[Joint::checkSanity] Initial velocity of index " << i << " ["
               << getDofName(i) << "] is Joint [" << getName() << "] is "
               << "outside of its velocity limits\n"
               << " -- Initial Velocity: " << getInitialVelocity(i) << "\n"
               << " -- Limits: [" << getVelocityLowerLimit(i) << ", "
               << getVelocityUpperLimit(i) << "]\n";
      }
      else
      {
        return false;
      }

      sane = false;
    }
  }

  return sane;
}

//==============================================================================
s_t Joint::getPotentialEnergy() const
{
  return computePotentialEnergy();
}

//==============================================================================
void Joint::setTransformFromParentBodyNode(const Eigen::Isometry3s& _T)
{
  assert(math::verifyTransform(_T));
  mAspectProperties.mT_ParentBodyToJoint = _T;
  mAspectProperties.mOriginalParentTranslation = _T.translation();
  mAspectProperties.mT_ParentBodyToJoint.translation()
      = mAspectProperties.mT_ParentBodyToJoint.translation().cwiseProduct(
          mAspectProperties.mParentScale);
  // mAspectProperties.mParentScale = Eigen::Vector3s::Ones();
  notifyPositionUpdated();
}

//==============================================================================
void Joint::setTransformFromChildBodyNode(const Eigen::Isometry3s& _T)
{
  assert(math::verifyTransform(_T));
  mAspectProperties.mT_ChildBodyToJoint = _T;
  mAspectProperties.mOriginalChildTranslation = _T.translation();
  mAspectProperties.mT_ChildBodyToJoint.translation()
      = mAspectProperties.mT_ChildBodyToJoint.translation().cwiseProduct(
          mAspectProperties.mChildScale);
  // mAspectProperties.mChildScale = Eigen::Vector3s::Ones();
  updateRelativeJacobian();
  notifyPositionUpdated();
}

//==============================================================================
const Eigen::Isometry3s& Joint::getTransformFromParentBodyNode() const
{
  return mAspectProperties.mT_ParentBodyToJoint;
}

//==============================================================================
const Eigen::Isometry3s& Joint::getTransformFromChildBodyNode() const
{
  return mAspectProperties.mT_ChildBodyToJoint;
}

//==============================================================================
/// Get the unscaled transformation from parent body node to this joint
const Eigen::Vector3s& Joint::getOriginalTransformFromParentBodyNode() const
{
  return mAspectProperties.mOriginalParentTranslation;
}

//==============================================================================
/// Get the unscaled transformation from child body node to this joint
const Eigen::Vector3s& Joint::getOriginalTransformFromChildBodyNode() const
{
  return mAspectProperties.mOriginalChildTranslation;
}

//==============================================================================
/// Copy the transfromFromParentNode and transfromFromChildNode, and their
/// scales, from another joint
void Joint::copyTransformsFrom(const dynamics::Joint* other)
{
  mAspectProperties.mChildScale = other->mAspectProperties.mChildScale;
  mAspectProperties.mT_ChildBodyToJoint
      = other->mAspectProperties.mT_ChildBodyToJoint;
  mAspectProperties.mOriginalChildTranslation
      = other->mAspectProperties.mOriginalChildTranslation;
  mAspectProperties.mParentScale = other->mAspectProperties.mParentScale;
  mAspectProperties.mT_ParentBodyToJoint
      = other->mAspectProperties.mT_ParentBodyToJoint;
  mAspectProperties.mOriginalParentTranslation
      = other->mAspectProperties.mOriginalParentTranslation;
}

//==============================================================================
/// Set the scale of the child body
void Joint::setChildScale(Eigen::Vector3s scale)
{
  if (mAspectProperties.mChildScale == scale) return;
  mAspectProperties.mChildScale = scale;
  mAspectProperties.mT_ChildBodyToJoint.translation()
      = mAspectProperties.mOriginalChildTranslation.cwiseProduct(scale);
  mNeedTransformUpdate = true;
  updateRelativeJacobian();
  notifyPositionUpdated();
}

//==============================================================================
/// Set the scale of the parent body
void Joint::setParentScale(Eigen::Vector3s scale)
{
  if (mAspectProperties.mParentScale == scale) return;
  mAspectProperties.mParentScale = scale;
  mAspectProperties.mT_ParentBodyToJoint.translation()
      = mAspectProperties.mOriginalParentTranslation.cwiseProduct(scale);
  mNeedTransformUpdate = true;
  updateRelativeJacobian();
  notifyPositionUpdated();
}

//==============================================================================
/// Get the scale of the child body
Eigen::Vector3s Joint::getChildScale() const
{
  return mAspectProperties.mChildScale;
}

//==============================================================================
/// Get the scale of the parent body
Eigen::Vector3s Joint::getParentScale() const
{
  return mAspectProperties.mParentScale;
}

//==============================================================================
Joint::Joint()
  : mChildBodyNode(nullptr),
    mT(Eigen::Isometry3s::Identity()),
    mSpatialVelocity(Eigen::Vector6s::Zero()),
    mSpatialAcceleration(Eigen::Vector6s::Zero()),
    mPrimaryAcceleration(Eigen::Vector6s::Zero()),
    mNeedTransformUpdate(true),
    mNeedSpatialVelocityUpdate(true),
    mNeedSpatialAccelerationUpdate(true),
    mNeedPrimaryAccelerationUpdate(true),
    mIsRelativeJacobianDirty(true),
    mIsRelativeJacobianInPositionSpaceDirty(true),
    mIsRelativeJacobianTimeDerivDirty(true)
{
  // Do nothing. The Joint::Aspect must be created by a derived class.
}

//==============================================================================
DegreeOfFreedom* Joint::createDofPointer(std::size_t _indexInJoint)
{
  return new DegreeOfFreedom(this, _indexInJoint);
}

//==============================================================================
void Joint::updateLocalTransform() const
{
  updateRelativeTransform();
}

//==============================================================================
void Joint::updateLocalSpatialVelocity() const
{
  updateRelativeSpatialVelocity();
}

//==============================================================================
void Joint::updateLocalSpatialAcceleration() const
{
  updateRelativeSpatialAcceleration();
}

//==============================================================================
void Joint::updateLocalPrimaryAcceleration() const
{
  updateRelativePrimaryAcceleration();
}

//==============================================================================
void Joint::updateLocalJacobian(bool mandatory) const
{
  updateRelativeJacobian(mandatory);
}

//==============================================================================
void Joint::updateLocalJacobianTimeDeriv() const
{
  updateRelativeJacobianTimeDeriv();
}

//==============================================================================
void Joint::updateArticulatedInertia() const
{
  mChildBodyNode->getArticulatedInertia();
}

//==============================================================================
// Eigen::VectorXs Joint::getDampingForces() const
//{
//  int numDofs = getNumDofs();
//  Eigen::VectorXs dampingForce(numDofs);

//  for (int i = 0; i < numDofs; ++i)
//    dampingForce(i) = -mDampingCoefficient[i] * getGenCoord(i)->getVel();

//  return dampingForce;
//}

//==============================================================================
// Eigen::VectorXs Joint::getSpringForces(s_t _timeStep) const
//{
//  int dof = getNumDofs();
//  Eigen::VectorXs springForce(dof);
//  for (int i = 0; i < dof; ++i)
//  {
//    springForce(i) =
//        -mSpringStiffness[i] * (getGenCoord(i)->getPos()
//                                + getGenCoord(i)->getVel() * _timeStep
//                                - mRestPosition[i]);
//  }
//  assert(!math::isNan(springForce));
//  return springForce;
//}

//==============================================================================
void Joint::notifyPositionUpdate()
{
  notifyPositionUpdated();
}

//==============================================================================
void Joint::notifyPositionUpdated()
{
  if (mChildBodyNode)
  {
    mChildBodyNode->dirtyTransform();
    mChildBodyNode->dirtyJacobian();
    mChildBodyNode->dirtyJacobianDeriv();
  }

  mIsRelativeJacobianDirty = true;
  mIsRelativeJacobianInPositionSpaceDirty = true;
  mIsRelativeJacobianTimeDerivDirty = true;
  mNeedPrimaryAccelerationUpdate = true;

  mNeedTransformUpdate = true;
  mNeedSpatialVelocityUpdate = true;
  mNeedSpatialAccelerationUpdate = true;

  SkeletonPtr skel = getSkeleton();
  if (skel)
  {
    std::size_t tree = mChildBodyNode->mTreeIndex;
    skel->dirtyArticulatedInertia(tree);
    skel->mTreeCache[tree].mDirty.mExternalForces = true;
    skel->mSkelCache.mDirty.mExternalForces = true;
  }
}

//==============================================================================
void Joint::notifyVelocityUpdate()
{
  notifyVelocityUpdated();
}

//==============================================================================
void Joint::notifyVelocityUpdated()
{
  if (mChildBodyNode)
  {
    mChildBodyNode->dirtyVelocity();
    mChildBodyNode->dirtyJacobianDeriv();
  }

  mIsRelativeJacobianTimeDerivDirty = true;

  mNeedSpatialVelocityUpdate = true;
  mNeedSpatialAccelerationUpdate = true;
}

//==============================================================================
void Joint::notifyAccelerationUpdate()
{
  notifyAccelerationUpdated();
}

//==============================================================================
void Joint::notifyAccelerationUpdated()
{
  if (mChildBodyNode)
    mChildBodyNode->dirtyAcceleration();

  mNeedSpatialAccelerationUpdate = true;
  mNeedPrimaryAccelerationUpdate = true;
}

} // namespace dynamics
} // namespace dart
