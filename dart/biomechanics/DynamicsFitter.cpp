#include "dart/biomechanics/DynamicsFitter.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <tuple>
#include <utility>

#include <coin/IpIpoptApplication.hpp>
#include <coin/IpSolveStatistics.hpp>
#include <coin/IpTNLP.hpp>

#include "dart/biomechanics/C3DForcePlatforms.hpp"
#include "dart/biomechanics/ForcePlate.hpp"
#include "dart/biomechanics/MarkerFitter.hpp"
#include "dart/biomechanics/MarkerLabeller.hpp"
#include "dart/dynamics/BodyNode.hpp"
#include "dart/dynamics/Skeleton.hpp"
#include "dart/math/AssignmentMatcher.hpp"
#include "dart/math/FiniteDifference.hpp"
#include "dart/math/Geometry.hpp"
#include "dart/math/MathTypes.hpp"
#include "dart/neural/WithRespectTo.hpp"
#include "dart/server/GUIRecording.hpp"
#include "dart/utils/AccelerationSmoother.hpp"

namespace dart {
namespace biomechanics {

using namespace Ipopt;

//==============================================================================
ResidualForceHelper::ResidualForceHelper(
    std::shared_ptr<dynamics::Skeleton> skeleton, std::vector<int> forceBodies)
  : mSkel(skeleton)
{
  for (int i : forceBodies)
  {
    mForces.emplace_back(skeleton, i);
  }
}

//==============================================================================
// Computes the residual for a specific timestep
Eigen::Vector6s ResidualForceHelper::calculateResidual(
    Eigen::VectorXs q,
    Eigen::VectorXs dq,
    Eigen::VectorXs ddq,
    Eigen::VectorXs forcesConcat)
{
  Eigen::VectorXs originalPos = mSkel->getPositions();
  Eigen::VectorXs originalVel = mSkel->getVelocities();
  Eigen::VectorXs originalAcc = mSkel->getAccelerations();

  mSkel->setPositions(q);
  mSkel->setVelocities(dq);
  mSkel->setAccelerations(ddq);

  // TODO: there is certainly a more efficient way to do this, since we only
  // care about the first 6 values anyways
  Eigen::MatrixXs M = mSkel->getMassMatrix();
  Eigen::VectorXs C = mSkel->getCoriolisAndGravityForces();
  Eigen::VectorXs Fs = Eigen::VectorXs::Zero(mSkel->getNumDofs());
  for (int i = 0; i < mForces.size(); i++)
  {
    Eigen::VectorXs fTaus
        = mForces[i].computeTau(forcesConcat.segment<6>(i * 6));
    Fs += fTaus;
  }
  Eigen::VectorXs manualTau = M * ddq + C - Fs;

  mSkel->setPositions(originalPos);
  mSkel->setVelocities(originalVel);
  mSkel->setAccelerations(originalAcc);

  return manualTau.head<6>();
}

//==============================================================================
// Computes the residual norm for a specific timestep
s_t ResidualForceHelper::calculateResidualNorm(
    Eigen::VectorXs q,
    Eigen::VectorXs dq,
    Eigen::VectorXs ddq,
    Eigen::VectorXs forcesConcat,
    bool useL1)
{
  Eigen::Vector6s residual = calculateResidual(q, dq, ddq, forcesConcat);
  if (useL1)
  {
    return residual.head<3>().norm() + residual.tail<3>().norm();
  }
  else
  {
    return residual.squaredNorm();
  }
}

//==============================================================================
// Computes the Jacobian of the residual with respect to the first position
Eigen::MatrixXs ResidualForceHelper::calculateResidualJacobianWrt(
    Eigen::VectorXs q,
    Eigen::VectorXs dq,
    Eigen::VectorXs ddq,
    Eigen::VectorXs forcesConcat,
    neural::WithRespectTo* wrt)
{
  Eigen::VectorXs originalPos = mSkel->getPositions();
  Eigen::VectorXs originalVel = mSkel->getVelocities();
  Eigen::VectorXs originalAcc = mSkel->getVelocities();

  mSkel->setPositions(q);
  mSkel->setVelocities(dq);
  mSkel->setAccelerations(ddq);

  // Eigen::VectorXs manualTau = M * acc + C - Fs;
  if (wrt == neural::WithRespectTo::POSITION
      || wrt == neural::WithRespectTo::GROUP_SCALES)
  {
    Eigen::MatrixXs dM = mSkel->getJacobianOfM(ddq, wrt);
    Eigen::MatrixXs dC = mSkel->getJacobianOfC(wrt);
    Eigen::MatrixXs dFs
        = Eigen::MatrixXs::Zero(mSkel->getNumDofs(), wrt->dim(mSkel.get()));
    for (int i = 0; i < mForces.size(); i++)
    {
      Eigen::MatrixXs dfTaus
          = mForces[i].getJacobianOfTauWrt(forcesConcat.segment<6>(i * 6), wrt);
      dFs += dfTaus;
    }
    Eigen::MatrixXs jac = dM + dC - dFs;

    mSkel->setPositions(originalPos);
    mSkel->setVelocities(originalVel);
    mSkel->setAccelerations(originalAcc);

    // Only take the first 6 rows
    return jac.block(0, 0, 6, jac.cols());
  }
  else if (
      wrt == neural::WithRespectTo::GROUP_MASSES
      || wrt == neural::WithRespectTo::GROUP_COMS
      || wrt == neural::WithRespectTo::GROUP_INERTIAS)
  {
    Eigen::MatrixXs dM = mSkel->getJacobianOfM(ddq, wrt);
    Eigen::MatrixXs dC = mSkel->getJacobianOfC(wrt);
    Eigen::MatrixXs jac = dM + dC;

    mSkel->setPositions(originalPos);
    mSkel->setVelocities(originalVel);
    mSkel->setAccelerations(originalAcc);

    // Only take the first 6 rows
    return jac.block(0, 0, 6, jac.cols());
  }
  else if (wrt == neural::WithRespectTo::VELOCITY)
  {
    Eigen::MatrixXs dC = mSkel->getJacobianOfC(neural::WithRespectTo::VELOCITY);

    mSkel->setPositions(originalPos);
    mSkel->setVelocities(originalVel);
    mSkel->setAccelerations(originalAcc);

    // Only take the first 6 rows
    return dC.block(0, 0, 6, dC.cols());
  }
  else if (wrt == neural::WithRespectTo::ACCELERATION)
  {
    Eigen::MatrixXs M = mSkel->getMassMatrix();

    mSkel->setPositions(originalPos);
    mSkel->setVelocities(originalVel);
    mSkel->setAccelerations(originalAcc);

    // Only take the first 6 rows
    return M.block(0, 0, 6, M.cols());
  }
  else
  {
    Eigen::MatrixXs J
        = finiteDifferenceResidualJacobianWrt(q, dq, ddq, forcesConcat, wrt);

    mSkel->setPositions(originalPos);
    mSkel->setVelocities(originalVel);
    mSkel->setAccelerations(originalAcc);

    return J;
  }
}

//==============================================================================
// Computes the Jacobian of the residual with respect to the first position
Eigen::MatrixXs ResidualForceHelper::finiteDifferenceResidualJacobianWrt(
    Eigen::VectorXs q,
    Eigen::VectorXs dq,
    Eigen::VectorXs ddq,
    Eigen::VectorXs forcesConcat,
    neural::WithRespectTo* wrt)
{
  Eigen::MatrixXs result(6, wrt->dim(mSkel.get()));

  Eigen::VectorXs originalPos = mSkel->getPositions();
  Eigen::VectorXs originalVel = mSkel->getVelocities();
  Eigen::VectorXs originalAcc = mSkel->getAccelerations();

  mSkel->setPositions(q);
  mSkel->setVelocities(dq);
  mSkel->setAccelerations(ddq);

  Eigen::VectorXs originalWrt = wrt->get(mSkel.get());

  bool useRidders = true;
  s_t eps = useRidders ? 1e-2 : 1e-5;
  math::finiteDifference(
      [&](/* in*/ s_t eps,
          /* in*/ int dof,
          /*out*/ Eigen::VectorXs& perturbed) {
        Eigen::VectorXs newWrt = originalWrt;
        newWrt(dof) += eps;
        wrt->set(mSkel.get(), newWrt);
        perturbed = calculateResidual(
            mSkel->getPositions(),
            mSkel->getVelocities(),
            mSkel->getAccelerations(),
            forcesConcat);
        return true;
      },
      result,
      eps,
      useRidders);

  wrt->set(mSkel.get(), originalWrt);

  mSkel->setPositions(originalPos);
  mSkel->setVelocities(originalVel);
  mSkel->setAccelerations(originalAcc);
  return result;
}

//==============================================================================
// Computes the gradient of the residual norm with respect to `wrt`
Eigen::VectorXs ResidualForceHelper::calculateResidualNormGradientWrt(
    Eigen::VectorXs q,
    Eigen::VectorXs dq,
    Eigen::VectorXs ddq,
    Eigen::VectorXs forcesConcat,
    neural::WithRespectTo* wrt,
    bool useL1)
{
  Eigen::Vector6s res = calculateResidual(q, dq, ddq, forcesConcat);
  Eigen::MatrixXs jac
      = calculateResidualJacobianWrt(q, dq, ddq, forcesConcat, wrt);
  if (useL1)
  {
    res.head<3>().normalize();
    res.tail<3>().normalize();
    return jac.transpose() * res;
  }
  else
  {
    return jac.transpose() * 2 * res;
  }
}

//==============================================================================
// Computes the gradient of the residual norm with respect to `wrt`
Eigen::VectorXs ResidualForceHelper::finiteDifferenceResidualNormGradientWrt(
    Eigen::VectorXs q,
    Eigen::VectorXs dq,
    Eigen::VectorXs ddq,
    Eigen::VectorXs forcesConcat,
    neural::WithRespectTo* wrt,
    bool useL1)
{
  Eigen::VectorXs result = Eigen::VectorXs::Zero(wrt->dim(mSkel.get()));

  Eigen::VectorXs originalPos = mSkel->getPositions();
  Eigen::VectorXs originalVel = mSkel->getVelocities();
  Eigen::VectorXs originalAcc = mSkel->getAccelerations();

  mSkel->setPositions(q);
  mSkel->setVelocities(dq);
  mSkel->setAccelerations(ddq);

  Eigen::VectorXs originalWrt = wrt->get(mSkel.get());

  math::finiteDifference(
      [&](/* in*/ s_t eps,
          /* in*/ int dof,
          /*out*/ s_t& perturbed) {
        Eigen::VectorXs newWrt = originalWrt;
        newWrt(dof) += eps;
        wrt->set(mSkel.get(), newWrt);
        perturbed = calculateResidualNorm(
            mSkel->getPositions(),
            mSkel->getVelocities(),
            mSkel->getAccelerations(),
            forcesConcat,
            useL1);
        return true;
      },
      result,
      5e-4,
      true);

  wrt->set(mSkel.get(), originalWrt);

  mSkel->setPositions(originalPos);
  mSkel->setVelocities(originalVel);
  mSkel->setAccelerations(originalAcc);
  return result;
}

//==============================================================================
DynamicsFitProblem::DynamicsFitProblem(
    std::shared_ptr<DynamicsInitialization> init,
    std::shared_ptr<dynamics::Skeleton> skeleton,
    dynamics::MarkerMap markerMap,
    std::vector<std::string> trackingMarkers,
    std::vector<dynamics::BodyNode*> footNodes)
  : mInit(init),
    mSkeleton(skeleton),
    mMarkerMap(markerMap),
    mFootNodes(footNodes),
    mIncludeMasses(true),
    mIncludeCOMs(true),
    mIncludeInertias(true),
    mIncludeBodyScales(true),
    mIncludePoses(true),
    mIncludeMarkerOffsets(true),
    mResidualWeight(0.1),
    mMarkerWeight(1.0),
    mJointWeight(1.0),
    mResidualUseL1(false),
    mMarkerUseL1(false),
    mRegularizeMasses(1.0),
    mRegularizeCOMs(1.0),
    mRegularizeInertias(1.0),
    mRegularizeTrackingMarkerOffsets(0.05),
    mRegularizeAnatomicalMarkerOffsets(10.0),
    mRegularizeBodyScales(0.2),
    mRegularizePoses(0.0),
    mBestObjectiveValue(std::numeric_limits<s_t>::infinity())
{
  // 1. Set up the markers

  for (auto& pair : markerMap)
  {
    mMarkerNames.push_back(pair.first);
    mMarkers.push_back(pair.second);
    if (std::find(trackingMarkers.begin(), trackingMarkers.end(), pair.first)
        != trackingMarkers.end())
    {
      mMarkerIsTracking.push_back(true);
    }
    else
    {
      mMarkerIsTracking.push_back(false);
    }
  }

  // 2. Set up the q, dq, ddq, and GRF

  int dofs = skeleton->getNumDofs();
  for (int i = 0; i < init->poseTrials.size(); i++)
  {
    s_t dt = init->trialTimesteps[i];
    Eigen::MatrixXs& inputPoses = init->poseTrials[i];
    std::cout << "Trial " << i << ": " << inputPoses.cols() << std::endl;
    Eigen::MatrixXs poses = Eigen::MatrixXs::Zero(dofs, inputPoses.cols());
    Eigen::MatrixXs vels = Eigen::MatrixXs::Zero(dofs, inputPoses.cols() - 1);
    Eigen::MatrixXs accs = Eigen::MatrixXs::Zero(dofs, inputPoses.cols() - 2);
    for (int j = 0; j < inputPoses.cols(); j++)
    {
      poses.col(j) = inputPoses.col(j);
    }
    for (int j = 0; j < inputPoses.cols() - 1; j++)
    {
      vels.col(j) = (inputPoses.col(j + 1) - inputPoses.col(j)) / dt;
    }
    for (int j = 0; j < inputPoses.cols() - 2; j++)
    {
      accs.col(j) = (inputPoses.col(j + 2) - 2 * inputPoses.col(j + 1)
                     + inputPoses.col(j))
                    / (dt * dt);
    }
    mPoses.push_back(poses);
    mVels.push_back(vels);
    mAccs.push_back(accs);
  }

  for (auto* node : footNodes)
  {
    mForceBodyIndices.push_back(node->getIndexInSkeleton());
  }

  mResidualHelper
      = std::make_shared<ResidualForceHelper>(mSkeleton, mForceBodyIndices);
}

//==============================================================================
// This returns the dimension of the decision variables (the length of the
// flatten() vector), which depends on which variables we choose to include in
// the optimization problem.
int DynamicsFitProblem::getProblemSize()
{
  int size = 0;
  if (mIncludeMasses)
  {
    size += mSkeleton->getNumScaleGroups();
  }
  if (mIncludeCOMs)
  {
    size += mSkeleton->getNumScaleGroups() * 3;
  }
  if (mIncludeInertias)
  {
    size += mSkeleton->getNumScaleGroups() * 6;
  }
  if (mIncludeBodyScales)
  {
    size += mSkeleton->getGroupScaleDim();
  }
  if (mIncludeMarkerOffsets)
  {
    size += mMarkers.size() * 3;
  }
  if (mIncludePoses)
  {
    int dofs = mSkeleton->getNumDofs();
    for (int trial = 0; trial < mPoses.size(); trial++)
    {
      // Add pos + vel + acc
      size += mAccs[trial].cols() * dofs * 3;
      // Add last two q's, last dq
      size += dofs * 3;
    }
  }
  return size;
}

//==============================================================================
// This writes the problem state into a flat vector
Eigen::VectorXs DynamicsFitProblem::flatten()
{
  Eigen::VectorXs flat = Eigen::VectorXs::Zero(getProblemSize());
  int cursor = 0;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    flat.segment(cursor, dim) = mSkeleton->getGroupMasses();
    cursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    flat.segment(cursor, dim) = mSkeleton->getGroupCOMs();
    cursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    flat.segment(cursor, dim) = mSkeleton->getGroupInertias();
    cursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    flat.segment(cursor, dim) = mSkeleton->getGroupScales();
    cursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkers.size(); i++)
    {
      flat.segment(cursor, 3) = mMarkers[i].second;
      cursor += 3;
    }
  }
  if (mIncludePoses)
  {
    int dofs = mSkeleton->getNumDofs();

    for (int trial = 0; trial < mPoses.size(); trial++)
    {
      for (int t = 0; t < mAccs[trial].cols(); t++)
      {
        flat.segment(cursor, dofs) = mPoses[trial].col(t);
        cursor += dofs;
        flat.segment(cursor, dofs) = mVels[trial].col(t);
        cursor += dofs;
        flat.segment(cursor, dofs) = mAccs[trial].col(t);
        cursor += dofs;
      }
      // Get the last two q's, and last dq
      int lastAccT = mAccs[trial].cols() - 1;
      flat.segment(cursor, dofs) = mPoses[trial].col(lastAccT + 1);
      cursor += dofs;
      flat.segment(cursor, dofs) = mVels[trial].col(lastAccT + 1);
      cursor += dofs;
      flat.segment(cursor, dofs) = mPoses[trial].col(lastAccT + 2);
      cursor += dofs;
    }
  }

  assert(cursor == flat.size());

  return flat;
}

//==============================================================================
// This writes the upper bounds into a flat vector
Eigen::VectorXs DynamicsFitProblem::flattenUpperBound()
{
  Eigen::VectorXs flat = Eigen::VectorXs::Zero(getProblemSize());
  int cursor = 0;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    flat.segment(cursor, dim) = mSkeleton->getGroupMassesUpperBound();
    cursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    flat.segment(cursor, dim) = mSkeleton->getGroupCOMUpperBound();
    cursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    flat.segment(cursor, dim) = mSkeleton->getGroupInertiasUpperBound();
    cursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    flat.segment(cursor, dim) = mSkeleton->getGroupScalesUpperBound();
    cursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkers.size(); i++)
    {
      flat.segment(cursor, 3) = Eigen::Vector3s::Ones() * 5;
      cursor += 3;
    }
  }
  if (mIncludePoses)
  {
    int dofs = mSkeleton->getNumDofs();

    for (int trial = 0; trial < mPoses.size(); trial++)
    {
      for (int t = 0; t < mAccs[trial].cols(); t++)
      {
        flat.segment(cursor, dofs) = mSkeleton->getPositionUpperLimits();
        cursor += dofs;
        flat.segment(cursor, dofs) = mSkeleton->getVelocityUpperLimits();
        cursor += dofs;
        flat.segment(cursor, dofs) = mSkeleton->getAccelerationUpperLimits();
        cursor += dofs;
      }
      // Get the last two q's, and last dq
      flat.segment(cursor, dofs) = mSkeleton->getPositionUpperLimits();
      cursor += dofs;
      flat.segment(cursor, dofs) = mSkeleton->getVelocityUpperLimits();
      cursor += dofs;
      flat.segment(cursor, dofs) = mSkeleton->getPositionUpperLimits();
      cursor += dofs;
    }
  }

  assert(cursor == flat.size());

  return flat;
}

//==============================================================================
// This writes the upper bounds into a flat vector
Eigen::VectorXs DynamicsFitProblem::flattenLowerBound()
{
  Eigen::VectorXs flat = Eigen::VectorXs::Zero(getProblemSize());
  int cursor = 0;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    flat.segment(cursor, dim) = mSkeleton->getGroupMassesLowerBound();
    cursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    flat.segment(cursor, dim) = mSkeleton->getGroupCOMLowerBound();
    cursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    flat.segment(cursor, dim) = mSkeleton->getGroupInertiasLowerBound();
    cursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    flat.segment(cursor, dim) = mSkeleton->getGroupScalesLowerBound();
    cursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkers.size(); i++)
    {
      flat.segment(cursor, 3) = Eigen::Vector3s::Ones() * -5;
      cursor += 3;
    }
  }
  if (mIncludePoses)
  {
    int dofs = mSkeleton->getNumDofs();
    for (int trial = 0; trial < mPoses.size(); trial++)
    {
      for (int t = 0; t < mAccs[trial].cols(); t++)
      {
        flat.segment(cursor, dofs) = mSkeleton->getPositionLowerLimits();
        cursor += dofs;
        flat.segment(cursor, dofs) = mSkeleton->getVelocityLowerLimits();
        cursor += dofs;
        flat.segment(cursor, dofs) = mSkeleton->getAccelerationLowerLimits();
        cursor += dofs;
      }
      // Get the last two q's, and last dq
      flat.segment(cursor, dofs) = mSkeleton->getPositionLowerLimits();
      cursor += dofs;
      flat.segment(cursor, dofs) = mSkeleton->getVelocityLowerLimits();
      cursor += dofs;
      flat.segment(cursor, dofs) = mSkeleton->getPositionLowerLimits();
      cursor += dofs;
    }
  }

  assert(cursor == flat.size());

  return flat;
}

//==============================================================================
// This reads the problem state out of a flat vector, and into the init object
void DynamicsFitProblem::unflatten(Eigen::VectorXs x)
{
  if (x.size() == mLastX.size() && x == mLastX)
  {
    // No need to overwrite the same update twice
    return;
  }
  mLastX = x;

  int cursor = 0;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    mSkeleton->setGroupMasses(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    mSkeleton->setGroupCOMs(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    mSkeleton->setGroupInertias(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    mSkeleton->setGroupScales(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkers.size(); i++)
    {
      mMarkers[i].second = x.segment(cursor, 3);
      cursor += 3;
    }
  }
  if (mIncludePoses)
  {
    int dofs = mSkeleton->getNumDofs();
    for (int trial = 0; trial < mPoses.size(); trial++)
    {
      for (int t = 0; t < mAccs[trial].cols(); t++)
      {
        mPoses[trial].col(t) = x.segment(cursor, dofs);
        cursor += dofs;
        mVels[trial].col(t) = x.segment(cursor, dofs);
        cursor += dofs;
        mAccs[trial].col(t) = x.segment(cursor, dofs);
        cursor += dofs;
      }
      int lastAccT = mAccs[trial].cols() - 1;
      mPoses[trial].col(lastAccT + 1) = x.segment(cursor, dofs);
      cursor += dofs;
      mVels[trial].col(lastAccT + 1) = x.segment(cursor, dofs);
      cursor += dofs;
      mPoses[trial].col(lastAccT + 2) = x.segment(cursor, dofs);
      cursor += dofs;
    }
  }

  assert(cursor == x.size());
}

//==============================================================================
// This gets the value of the loss function, as a weighted sum of the
// discrepancy between measured and expected GRF data and other regularization
// terms.
s_t DynamicsFitProblem::computeLoss(Eigen::VectorXs x, bool logExplanation)
{
  unflatten(x);

  s_t sum = 0.0;

  /*
  if (logExplanation)
  {
    Eigen::MatrixXs compare
        = Eigen::MatrixXs::Zero(mSkeleton->getNumScaleGroups(), 5);
    compare.col(0) = mSkeleton->getGroupMasses();
    compare.col(1) = mInit->originalGroupMasses;
    compare.col(2) = mSkeleton->getGroupMassesUpperBound();
    compare.col(3) = mSkeleton->getGroupMassesLowerBound();
    compare.col(4) = (mSkeleton->getGroupMasses() - mInit->originalGroupMasses);
    std::cout << "masses - orig - upper - lower - diff" << std::endl
              << compare << std::endl;
  }
  */

  if (mInit->probablyMissingGRF.size() < mInit->poseTrials.size())
  {
    std::cout << "Don't ask for loss before you've called "
                 "DynamicsFitter::estimateFootGroundContacts() with this init "
                 "object! Killing the process with exit 1."
              << std::endl;
    exit(1);
  }

  s_t massRegularization
      = mRegularizeMasses * (1.0 / mSkeleton->getNumScaleGroups())
        * (mSkeleton->getGroupMasses() - mInit->originalGroupMasses)
              .squaredNorm();
  sum += massRegularization;
  assert(!isnan(sum));
  s_t comRegularization
      = mRegularizeCOMs * (1.0 / mSkeleton->getNumScaleGroups())
        * (mSkeleton->getGroupCOMs() - mInit->originalGroupCOMs).squaredNorm();
  sum += comRegularization;
  assert(!isnan(sum));
  s_t inertiaRegularization
      = mRegularizeInertias * (1.0 / mSkeleton->getNumScaleGroups())
        * (mSkeleton->getGroupInertias() - mInit->originalGroupInertias)
              .squaredNorm();
  sum += inertiaRegularization;
  assert(!isnan(sum));
  s_t scaleRegularization
      = mRegularizeBodyScales * (1.0 / mSkeleton->getNumScaleGroups())
        * (mSkeleton->getGroupScales() - mInit->originalGroupScales)
              .squaredNorm();
  sum += scaleRegularization;
  assert(!isnan(sum));
  s_t markerRegularization = 0.0;
  for (int i = 0; i < mMarkerNames.size(); i++)
  {
    if (mInit->originalMarkerOffsets.count(mMarkerNames.at(i)))
    {
      markerRegularization
          += (mMarkerIsTracking[i] ? mRegularizeTrackingMarkerOffsets
                                   : mRegularizeAnatomicalMarkerOffsets)
             * (1.0 / mMarkerNames.size())
             * (mMarkers[i].second
                - mInit->originalMarkerOffsets.at(mMarkerNames.at(i)))
                   .squaredNorm();
    }
    assert(!isnan(markerRegularization));
  }
  sum += markerRegularization;

  Eigen::VectorXs originalPos = mSkeleton->getPositions();
  mSkeleton->clearExternalForces();

  int totalTimesteps = 0;
  int totalAccTimesteps = 0;
  for (int trial = 0; trial < mPoses.size(); trial++)
  {
    totalTimesteps += mPoses[trial].cols();
    totalAccTimesteps += mAccs[trial].cols();
  }

  s_t residualRMS = 0.0;
  s_t markerRMS = 0.0;
  s_t poseRegularization = 0.0;
  s_t jointRMS = 0.0;
  s_t axisRMS = 0.0;
  int markerCount = 0;

  for (int trial = 0; trial < mPoses.size(); trial++)
  {
    for (int t = 0; t < mPoses[trial].cols(); t++)
    {
      mSkeleton->setPositions(mPoses[trial].col(t));

      // Add force residual RMS errors to all but the last 2 timesteps
      if (t < mAccs[trial].cols() && !mInit->probablyMissingGRF[trial][t])
      {
        residualRMS += mResidualWeight * (1.0 / totalAccTimesteps)
                       * mResidualHelper->calculateResidualNorm(
                           mPoses[trial].col(t),
                           mVels[trial].col(t),
                           mAccs[trial].col(t),
                           mInit->grfTrials[trial].col(t),
                           mResidualUseL1);
        assert(!isnan(residualRMS));
      }

      // Add marker RMS errors to every timestep
      auto markerPoses = mSkeleton->getMarkerWorldPositions(mMarkers);
      auto observedMarkerPoses = mInit->markerObservationTrials[trial][t];
      for (int i = 0; i < mMarkerNames.size(); i++)
      {
        Eigen::Vector3s marker = markerPoses.segment<3>(i * 3);
        if (observedMarkerPoses.count(mMarkerNames[i]))
        {
          Eigen::Vector3s diff
              = observedMarkerPoses.at(mMarkerNames[i]) - marker;
          s_t thisMarkerCost;
          if (mMarkerUseL1)
          {
            thisMarkerCost = diff.norm();
          }
          else
          {
            thisMarkerCost = diff.squaredNorm();
          }
          markerRMS += thisMarkerCost;
          markerCount++;
          assert(!isnan(markerRMS));
        }
      }

      // Add joints
      Eigen::VectorXs jointPoses
          = mSkeleton->getJointWorldPositions(mInit->joints);
      Eigen::VectorXs jointCenters = mInit->jointCenters[trial].col(t);
      Eigen::VectorXs jointAxis = mInit->jointAxis[trial].col(t);
      Eigen::VectorXs jointDiff = jointPoses - jointCenters;
      for (int i = 0; i < mInit->jointWeights.size(); i++)
      {
        jointRMS
            += (jointPoses.segment<3>(i * 3) - jointCenters.segment<3>(i * 3))
                   .squaredNorm()
               * mInit->jointWeights(i);
      }
      for (int i = 0; i < mInit->axisWeights.size(); i++)
      {
        Eigen::Vector3s axisCenter = jointAxis.segment<3>(i * 6);
        Eigen::Vector3s axisDir = jointAxis.segment<3>(i * 6 + 3).normalized();
        Eigen::Vector3s actualJointPos = jointPoses.segment<3>(i * 3);
        // Subtract out any component parallel to the axis
        Eigen::Vector3s jointDiff = actualJointPos - axisCenter;
        jointDiff -= jointDiff.dot(axisDir) * axisDir;
        axisRMS += jointDiff.squaredNorm() * mInit->axisWeights(i);
      }

      // Add regularization
      poseRegularization
          += mRegularizePoses * (1.0 / totalTimesteps)
             * (mPoses[trial].col(t) - mInit->originalPoses[trial].col(t))
                   .squaredNorm();
      assert(!isnan(poseRegularization));
    }
  }
  sum += residualRMS;
  markerRMS *= mMarkerWeight;
  if (markerCount > 0)
  {
    markerRMS /= markerCount;
  }
  sum += markerRMS;
  jointRMS *= mJointWeight;
  sum += jointRMS;
  axisRMS *= mJointWeight;
  sum += axisRMS;
  sum += poseRegularization;
  assert(!isnan(sum));

  if (logExplanation)
  {
    std::cout << "["
              << "massR=" << massRegularization << ",comR=" << comRegularization
              << ",inR=" << inertiaRegularization
              << ",scR=" << scaleRegularization
              << ",mkrR=" << markerRegularization << ",jntRMS=" << jointRMS
              << ",axisRMS=" << axisRMS << ",qR=" << poseRegularization
              << ",fRMS=" << residualRMS << ",mkRMS=" << markerRMS << "]"
              << std::endl;
  }

  return sum;
}

//==============================================================================
// This gets the gradient of the loss function
Eigen::VectorXs DynamicsFitProblem::computeGradient(Eigen::VectorXs x)
{
  unflatten(x);

  Eigen::VectorXs grad = Eigen::VectorXs::Zero(getProblemSize());
  const int dofs = mSkeleton->getNumDofs();

  /*
  //////////////////////
  // From flatten():
  //////////////////////

  int cursor = 0;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    mSkeleton->setGroupMasses(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    mSkeleton->setGroupCOMs(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    mSkeleton->setGroupInertias(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    mSkeleton->setGroupScales(x.segment(cursor, dim));
    cursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkers.size(); i++)
    {
      mMarkers[i].second = x.segment(cursor, 3);
      cursor += 3;
    }
  }
  if (mIncludePoses)
  {
  */
  if (mInit->probablyMissingGRF.size() < mInit->poseTrials.size())
  {
    std::cout << "Don't ask for gradients before you've called "
                 "DynamicsFitter::estimateFootGroundContacts() with this init "
                 "object! Killing the process with exit 1."
              << std::endl;
    exit(1);
  }

  int posesCursor = 0;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    grad.segment(posesCursor, dim)
        += mRegularizeMasses * 2 * (1.0 / mSkeleton->getNumScaleGroups())
           * (mSkeleton->getGroupMasses() - mInit->originalGroupMasses);
    posesCursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    grad.segment(posesCursor, dim)
        += mRegularizeCOMs * 2 * (1.0 / mSkeleton->getNumScaleGroups())
           * (mSkeleton->getGroupCOMs() - mInit->originalGroupCOMs);
    posesCursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    grad.segment(posesCursor, dim)
        += mRegularizeInertias * 2 * (1.0 / mSkeleton->getNumScaleGroups())
           * (mSkeleton->getGroupInertias() - mInit->originalGroupInertias);
    posesCursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    grad.segment(posesCursor, dim)
        += mRegularizeBodyScales * 2 * (1.0 / mSkeleton->getNumScaleGroups())
           * (mSkeleton->getGroupScales() - mInit->originalGroupScales);
    posesCursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkers.size(); i++)
    {
      grad.segment<3>(posesCursor)
          += 2
             * (mMarkerIsTracking[i] ? mRegularizeTrackingMarkerOffsets
                                     : mRegularizeAnatomicalMarkerOffsets)
             * (1.0 / mMarkerNames.size())
             * (mMarkers[i].second
                - mInit->originalMarkerOffsets[mMarkerNames[i]]);
      posesCursor += 3;
    }
  }

  int totalTimesteps = 0;
  int totalAccTimesteps = 0;
  for (int trial = 0; trial < mPoses.size(); trial++)
  {
    totalTimesteps += mPoses[trial].cols();
    totalAccTimesteps += mAccs[trial].cols();
  }
  int markerCount = 0;
  for (int trial = 0; trial < mPoses.size(); trial++)
  {
    for (int t = 0; t < mPoses[trial].cols(); t++)
    {
      auto& markerObservations = mInit->markerObservationTrials[trial][t];
      for (int i = 0; i < mMarkers.size(); i++)
      {
        if (markerObservations.count(mMarkerNames[i]))
        {
          markerCount++;
        }
      }
    }
  }

  for (int trial = 0; trial < mPoses.size(); trial++)
  {
    for (int t = 0; t < mPoses[trial].cols(); t++)
    {
      mSkeleton->setPositions(mPoses[trial].col(t));
      Eigen::VectorXs lossGradWrtMarkerError
          = Eigen::VectorXs::Zero(mMarkers.size() * 3);
      auto& markerObservations = mInit->markerObservationTrials[trial][t];
      auto markerMap = mSkeleton->getMarkerMapWorldPositions(mMarkerMap);
      for (int i = 0; i < mMarkers.size(); i++)
      {
        if (markerObservations.count(mMarkerNames[i]))
        {
          Eigen::Vector3s markerOffset
              = markerMap.at(mMarkerNames[i])
                - markerObservations.at(mMarkerNames[i]);
          if (mMarkerUseL1)
          {
            markerOffset.normalize();
          }
          else
          {
            markerOffset *= 2;
          }
          lossGradWrtMarkerError.segment<3>(i * 3)
              = (mMarkerWeight / markerCount) * markerOffset;
        }
      }

      Eigen::VectorXs jointGrad
          = Eigen::VectorXs::Zero(mInit->joints.size() * 3);
      Eigen::VectorXs worldJoints
          = mSkeleton->getJointWorldPositions(mInit->joints);
      Eigen::VectorXs targetJoints = mInit->jointCenters[trial].col(t);
      Eigen::VectorXs targetAxis = mInit->jointAxis[trial].col(t);
      for (int i = 0; i < mInit->joints.size(); i++)
      {
        Eigen::Vector3s worldDiff
            = worldJoints.segment<3>(i * 3) - targetJoints.segment<3>(i * 3);
        jointGrad.segment<3>(i * 3) += 2 * worldDiff * mInit->jointWeights(i);

        Eigen::Vector3s axisDiff
            = worldJoints.segment<3>(i * 3) - targetAxis.segment<3>(i * 6);
        Eigen::Vector3s axis = targetAxis.segment<3>(i * 6 + 3).normalized();
        axisDiff -= axisDiff.dot(axis) * axis;
        jointGrad.segment<3>(i * 3) += 2 * axisDiff * mInit->axisWeights(i);
      }
      jointGrad *= mJointWeight;

      if (t < mAccs[trial].cols())
      {
        int cursor = 0;
        if (mIncludeMasses)
        {
          int dim = mSkeleton->getNumScaleGroups();
          if (!mInit->probablyMissingGRF[trial][t])
          {
            grad.segment(cursor, dim)
                += mResidualWeight * (1.0 / totalAccTimesteps)
                   * mResidualHelper->calculateResidualNormGradientWrt(
                       mPoses[trial].col(t),
                       mVels[trial].col(t),
                       mAccs[trial].col(t),
                       mInit->grfTrials[trial].col(t),
                       neural::WithRespectTo::GROUP_MASSES,
                       mResidualUseL1);
          }
          cursor += dim;
        }
        if (mIncludeCOMs)
        {
          int dim = mSkeleton->getNumScaleGroups() * 3;
          if (!mInit->probablyMissingGRF[trial][t])
          {
            grad.segment(cursor, dim)
                += mResidualWeight * (1.0 / totalAccTimesteps)
                   * mResidualHelper->calculateResidualNormGradientWrt(
                       mPoses[trial].col(t),
                       mVels[trial].col(t),
                       mAccs[trial].col(t),
                       mInit->grfTrials[trial].col(t),
                       neural::WithRespectTo::GROUP_COMS,
                       mResidualUseL1);
          }
          cursor += dim;
        }
        if (mIncludeInertias)
        {
          int dim = mSkeleton->getNumScaleGroups() * 6;
          if (!mInit->probablyMissingGRF[trial][t])
          {
            grad.segment(cursor, dim)
                += mResidualWeight * (1.0 / totalAccTimesteps)
                   * mResidualHelper->calculateResidualNormGradientWrt(
                       mPoses[trial].col(t),
                       mVels[trial].col(t),
                       mAccs[trial].col(t),
                       mInit->grfTrials[trial].col(t),
                       neural::WithRespectTo::GROUP_INERTIAS,
                       mResidualUseL1);
          }
          cursor += dim;
        }
        if (mIncludeBodyScales)
        {
          int dim = mSkeleton->getGroupScaleDim();
          if (!mInit->probablyMissingGRF[trial][t])
          {
            grad.segment(cursor, dim)
                += mResidualWeight * (1.0 / totalAccTimesteps)
                   * mResidualHelper->calculateResidualNormGradientWrt(
                       mPoses[trial].col(t),
                       mVels[trial].col(t),
                       mAccs[trial].col(t),
                       mInit->grfTrials[trial].col(t),
                       neural::WithRespectTo::GROUP_SCALES,
                       mResidualUseL1);
          }

          // Record marker gradients
          grad.segment(cursor, dim)
              += MarkerFitter::getMarkerLossGradientWrtGroupScales(
                  mSkeleton, mMarkers, lossGradWrtMarkerError);

          // Record joint gradients
          grad.segment(cursor, dim)
              += mSkeleton
                     ->getJointWorldPositionsJacobianWrtGroupScales(
                         mInit->joints)
                     .transpose()
                 * jointGrad;

          cursor += dim;
        }
        if (mIncludeMarkerOffsets)
        {
          int dim = mMarkers.size() * 3;
          grad.segment(cursor, dim)
              += MarkerFitter::getMarkerLossGradientWrtMarkerOffsets(
                  mSkeleton, mMarkers, lossGradWrtMarkerError);
          cursor += dim;
        }

        if (mIncludePoses)
        {
          if (!mInit->probablyMissingGRF[trial][t])
          {
            grad.segment(posesCursor, dofs)
                += mResidualWeight * (1.0 / totalAccTimesteps)
                   * mResidualHelper->calculateResidualNormGradientWrt(
                       mPoses[trial].col(t),
                       mVels[trial].col(t),
                       mAccs[trial].col(t),
                       mInit->grfTrials[trial].col(t),
                       neural::WithRespectTo::POSITION,
                       mResidualUseL1);
          }

          // Record marker gradients
          grad.segment(posesCursor, dofs)
              += MarkerFitter::getMarkerLossGradientWrtJoints(
                  mSkeleton, mMarkers, lossGradWrtMarkerError);

          // Record regularization
          grad.segment(posesCursor, dofs)
              += mRegularizePoses * 2 * (1.0 / totalTimesteps)
                 * (mPoses[trial].col(t) - mInit->originalPoses[trial].col(t));

          // Record joint gradients
          grad.segment(posesCursor, dofs)
              += mSkeleton
                     ->getJointWorldPositionsJacobianWrtJointPositions(
                         mInit->joints)
                     .transpose()
                 * jointGrad;

          posesCursor += dofs;

          if (!mInit->probablyMissingGRF[trial][t])
          {
            grad.segment(posesCursor, dofs)
                += mResidualWeight * (1.0 / totalAccTimesteps)
                   * mResidualHelper->calculateResidualNormGradientWrt(
                       mPoses[trial].col(t),
                       mVels[trial].col(t),
                       mAccs[trial].col(t),
                       mInit->grfTrials[trial].col(t),
                       neural::WithRespectTo::VELOCITY,
                       mResidualUseL1);
          }
          posesCursor += dofs;

          if (!mInit->probablyMissingGRF[trial][t])
          {
            grad.segment(posesCursor, dofs)
                += mResidualWeight * (1.0 / totalAccTimesteps)
                   * mResidualHelper->calculateResidualNormGradientWrt(
                       mPoses[trial].col(t),
                       mVels[trial].col(t),
                       mAccs[trial].col(t),
                       mInit->grfTrials[trial].col(t),
                       neural::WithRespectTo::ACCELERATION,
                       mResidualUseL1);
          }
          posesCursor += dofs;
        }
      }
      else
      {
        int cursor = 0;
        if (mIncludeMasses)
        {
          int dim = mSkeleton->getNumScaleGroups();
          cursor += dim;
        }
        if (mIncludeCOMs)
        {
          int dim = mSkeleton->getNumScaleGroups() * 3;
          cursor += dim;
        }
        if (mIncludeInertias)
        {
          int dim = mSkeleton->getNumScaleGroups() * 6;
          cursor += dim;
        }
        if (mIncludeBodyScales)
        {
          int dim = mSkeleton->getGroupScaleDim();
          // Record marker gradients
          grad.segment(cursor, dim)
              += MarkerFitter::getMarkerLossGradientWrtGroupScales(
                  mSkeleton, mMarkers, lossGradWrtMarkerError);
          // Record joint gradients
          grad.segment(cursor, dim)
              += mSkeleton
                     ->getJointWorldPositionsJacobianWrtGroupScales(
                         mInit->joints)
                     .transpose()
                 * jointGrad;

          cursor += dim;
        }
        if (mIncludeMarkerOffsets)
        {
          int dim = mMarkers.size() * 3;
          grad.segment(cursor, dim)
              += MarkerFitter::getMarkerLossGradientWrtMarkerOffsets(
                  mSkeleton, mMarkers, lossGradWrtMarkerError);
          cursor += dim;
        }
        if (mIncludePoses)
        {
          // Record marker gradients
          grad.segment(posesCursor, dofs)
              += MarkerFitter::getMarkerLossGradientWrtJoints(
                  mSkeleton, mMarkers, lossGradWrtMarkerError);
          // Record regularization
          grad.segment(posesCursor, dofs)
              += mRegularizePoses * 2 * (1.0 / totalTimesteps)
                 * (mPoses[trial].col(t) - mInit->originalPoses[trial].col(t));
          // Record joint gradients
          grad.segment(posesCursor, dofs)
              += mSkeleton
                     ->getJointWorldPositionsJacobianWrtJointPositions(
                         mInit->joints)
                     .transpose()
                 * jointGrad;

          posesCursor += dofs;

          // Skip over the velocities at this last timestep where they're
          // available, they don't do anything
          if (t < mVels[trial].cols())
          {
            posesCursor += dofs;
          }
        }
      }
    }
  }

  assert(posesCursor == grad.size());

  // TODO
  return grad;
}

//==============================================================================
// This gets the gradient of the loss function
Eigen::VectorXs DynamicsFitProblem::finiteDifferenceGradient(
    Eigen::VectorXs x, bool useRidders)
{
  Eigen::VectorXs result = Eigen::VectorXs::Zero(getProblemSize());

  math::finiteDifference(
      [this, x](
          /* in*/ s_t eps,
          /* in*/ int dof,
          /*out*/ s_t& perturbed) {
        Eigen::VectorXs perturbedX = x;
        perturbedX(dof) += eps;
        perturbed = computeLoss(perturbedX);
        return true;
      },
      result,
      useRidders ? 1e-3 : 1e-8,
      useRidders);

  return result;
}

// This gets the number of constraints that the problem requires
int DynamicsFitProblem::getConstraintSize()
{
  if (mIncludePoses)
  {
    int numConstraints = 0;
    int dofs = mSkeleton->getNumDofs();
    for (int trial = 0; trial < mAccs.size(); trial++)
    {
      // Each ddq and dq need a constraint
      numConstraints += mAccs[trial].cols() * dofs * 2;
    }
    // Last dq needs a constraint as well
    numConstraints += dofs;
    return numConstraints;
  }

  return 0;
}

// This gets the value of the constraints vector. These constraints are only
// active when we're including positions in the decision variables, and they
// just enforce that finite differencing is valid to relate velocity,
// acceleration, and position.
Eigen::VectorXs DynamicsFitProblem::computeConstraints(Eigen::VectorXs x)
{
  if (mIncludePoses)
  {
    unflatten(x);
    int dim = getConstraintSize();
    Eigen::VectorXs constraints = Eigen::VectorXs::Zero(dim);
    int dofs = mSkeleton->getNumDofs();

    int cursor = 0;
    for (int trial = 0; trial < mAccs.size(); trial++)
    {
      s_t dt = mInit->trialTimesteps[trial];
      for (int t = 0; t < mAccs[trial].cols(); t++)
      {
        for (int i = 0; i < dofs; i++)
        {
          s_t fd = mPoses[trial](i, t + 1) - mPoses[trial](i, t);
          constraints(cursor) = (mVels[trial](i, t) * dt) - fd;
          cursor++;
        }
        for (int i = 0; i < dofs; i++)
        {
          s_t fd = mVels[trial](i, t + 1) - mVels[trial](i, t);
          constraints(cursor) = (mAccs[trial](i, t) * dt) - fd;
          cursor++;
        }
      }
      // Last dq needs a constraint as well
      int lastAccT = mAccs[trial].cols() - 1;
      for (int i = 0; i < dofs; i++)
      {
        s_t fd
            = mPoses[trial](i, lastAccT + 2) - mPoses[trial](i, lastAccT + 1);
        constraints(cursor) = (mVels[trial](i, lastAccT + 1) * dt) - fd;
        cursor++;
      }
    }
    assert(cursor == constraints.size());
    return constraints;
  }
  return Eigen::VectorXs::Zero(0);
}

// This gets the sparse version of the constraints jacobian, returning objects
// with (row,col,value).
std::vector<std::tuple<int, int, s_t>>
DynamicsFitProblem::computeSparseConstraintsJacobian()
{
  int colCursor = 0;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    colCursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    colCursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    colCursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    colCursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    colCursor += 3 * mMarkers.size();
  }

#ifndef NDEBUG
  int cols = getProblemSize();
#endif

  std::vector<std::tuple<int, int, s_t>> result;
  if (mIncludePoses)
  {
    int dofs = mSkeleton->getNumDofs();

    int rowCursor = 0;
    for (int trial = 0; trial < mAccs.size(); trial++)
    {
      s_t dt = mInit->trialTimesteps[trial];
      for (int t = 0; t < mAccs[trial].cols(); t++)
      {
        for (int i = 0; i < dofs; i++)
        {
          // s_t fd = mPoses[trial](i, t + 1) - mPoses[trial](i, t);
          // constraints(rowCursor) = (mVels[trial](i, t) * dt) - fd;
          int q_i_t0 = colCursor + i;
          int q_i_t1 = colCursor + (dofs * 3) + i;
          int dq_i_t0 = colCursor + dofs + i;
          result.emplace_back(rowCursor, q_i_t0, 1);
          result.emplace_back(rowCursor, q_i_t1, -1);
          result.emplace_back(rowCursor, dq_i_t0, dt);
          rowCursor++;
        }
        for (int i = 0; i < dofs; i++)
        {
          // s_t fd = mVels[trial](i, t + 1) - mVels[trial](i, t);
          // constraints(rowCursor) = (mAccs[trial](i, t) * dt) - fd;
          int dq_i_t0 = colCursor + dofs + i;
          int dq_i_t1 = colCursor + (dofs * 3) + dofs + i;
          int ddq_i_t0 = colCursor + (dofs * 2) + i;
          result.emplace_back(rowCursor, dq_i_t0, 1);
          result.emplace_back(rowCursor, dq_i_t1, -1);
          result.emplace_back(rowCursor, ddq_i_t0, dt);
          rowCursor++;
        }

        // add space for this t's [q, dq, ddq]
        colCursor += dofs * 3;
#ifndef NDEBUG
        assert(colCursor < cols);
#endif
      }
      // Do the last dq constraint
      for (int i = 0; i < dofs; i++)
      {
        // s_t fd
        //     = mPoses[trial](i, lastAccT + 2) - mPoses[trial](i, lastAccT +
        //     1);
        // constraints(cursor) = (mVels[trial](i, lastAccT + 1) * dt) - fd;
        int q_i_t0 = colCursor + i;
        int q_i_t1 = colCursor + (dofs * 2) + i;
        int dq_i_t0 = colCursor + dofs + i;
        result.emplace_back(rowCursor, q_i_t0, 1);
        result.emplace_back(rowCursor, q_i_t1, -1);
        result.emplace_back(rowCursor, dq_i_t0, dt);
        rowCursor++;
      }
      // add space for the last [q, dq, q]
      colCursor += dofs * 3;
#ifndef NDEBUG
      assert(colCursor <= cols);
#endif
    }
  }

#ifndef NDEBUG
  assert(colCursor == cols);
#endif
  return result;
}

// This gets the jacobian of the constraints vector with respect to x
Eigen::MatrixXs DynamicsFitProblem::computeConstraintsJacobian()
{
  if (!mIncludePoses)
  {
    return Eigen::MatrixXs::Zero(0, 0);
  }
  int problemDim = getProblemSize();
  int constraintDim = getConstraintSize();
  auto sparseConstraints = computeSparseConstraintsJacobian();
  Eigen::MatrixXs J = Eigen::MatrixXs::Zero(constraintDim, problemDim);
  for (auto& tuple : sparseConstraints)
  {
    J(std::get<0>(tuple), std::get<1>(tuple)) = std::get<2>(tuple);
  }
  return J;
}

// This gets the jacobian of the constraints vector with respect to x
Eigen::MatrixXs DynamicsFitProblem::finiteDifferenceConstraintsJacobian()
{
  int problemDim = getProblemSize();
  int constraintDim = getConstraintSize();
  Eigen::MatrixXs result = Eigen::MatrixXs::Zero(constraintDim, problemDim);
  Eigen::VectorXs original = flatten();

  const bool useRidders = false;
  s_t eps = useRidders ? 1e-3 : 1e-6;
  math::finiteDifference(
      [&](/* in*/ s_t eps,
          /* in*/ int dof,
          /*out*/ Eigen::VectorXs& perturbed) {
        Eigen::VectorXs tweaked = original;
        tweaked(dof) += eps;
        perturbed = computeConstraints(tweaked);
        return true;
      },
      result,
      eps,
      useRidders);

  return result;
}

//==============================================================================
bool debugVector(
    Eigen::VectorXs fd, Eigen::VectorXs analytical, std::string name, s_t tol)
{
  bool anyError = false;
  for (int i = 0; i < fd.size(); i++)
  {
    bool isError = false;
    s_t error = 0.0;
    if (fabs(fd(i)) > 1)
    {
      // Test relative error for values that are larger than 1
      if (fabs((fd(i) - analytical(i)) / fd(i)) > tol)
      {
        error = fabs((fd(i) - analytical(i)) / fd(i));
        isError = true;
      }
    }
    else if (fabs(fd(i) - analytical(i)) > tol)
    {
      error = fabs(fd(i) - analytical(i));
      isError = true;
    }
    if (isError)
    {
      std::cout << "Error on " << name << "[" << i << "]: " << fd(i) << " - "
                << analytical(i) << " = " << error << std::endl;
      anyError = true;
    }
  }
  return anyError;
}

//==============================================================================
// Print out the errors in a gradient vector in human readable form
bool DynamicsFitProblem::debugErrors(
    Eigen::VectorXs fd, Eigen::VectorXs analytical, s_t tol)
{
  int cursor = 0;
  bool anyError = false;
  if (mIncludeMasses)
  {
    int dim = mSkeleton->getNumScaleGroups();
    anyError |= debugVector(
        fd.segment(cursor, dim), analytical.segment(cursor, dim), "mass", tol);
    cursor += dim;
  }
  if (mIncludeCOMs)
  {
    int dim = mSkeleton->getNumScaleGroups() * 3;
    anyError |= debugVector(
        fd.segment(cursor, dim), analytical.segment(cursor, dim), "COM", tol);
    cursor += dim;
  }
  if (mIncludeInertias)
  {
    int dim = mSkeleton->getNumScaleGroups() * 6;
    anyError |= debugVector(
        fd.segment(cursor, dim),
        analytical.segment(cursor, dim),
        "inertia",
        tol);
    cursor += dim;
  }
  if (mIncludeBodyScales)
  {
    int dim = mSkeleton->getGroupScaleDim();
    anyError |= debugVector(
        fd.segment(cursor, dim),
        analytical.segment(cursor, dim),
        "bodyScales",
        tol);
    cursor += dim;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkers.size(); i++)
    {
      anyError |= debugVector(
          fd.segment(cursor, 3),
          analytical.segment(cursor, 3),
          "marker_" + std::to_string(i),
          tol);
      cursor += 3;
    }
  }
  if (mIncludePoses)
  {
    int dofs = mSkeleton->getNumDofs();

    for (int trial = 0; trial < mPoses.size(); trial++)
    {
      for (int t = 0; t < mAccs[trial].cols(); t++)
      {
        anyError |= debugVector(
            fd.segment(cursor, dofs),
            analytical.segment(cursor, dofs),
            "poses@t=" + std::to_string(t),
            tol);
        cursor += dofs;
        anyError |= debugVector(
            fd.segment(cursor, dofs),
            analytical.segment(cursor, dofs),
            "vels@t=" + std::to_string(t),
            tol);
        cursor += dofs;
        anyError |= debugVector(
            fd.segment(cursor, dofs),
            analytical.segment(cursor, dofs),
            "accs@t=" + std::to_string(t),
            tol);
        cursor += dofs;
      }
      int finalT = mAccs[trial].cols();
      anyError |= debugVector(
          fd.segment(cursor, dofs),
          analytical.segment(cursor, dofs),
          "poses@t=" + std::to_string(finalT),
          tol);
      cursor += dofs;
      anyError |= debugVector(
          fd.segment(cursor, dofs),
          analytical.segment(cursor, dofs),
          "vels@t=" + std::to_string(finalT),
          tol);
      cursor += dofs;
      anyError |= debugVector(
          fd.segment(cursor, dofs),
          analytical.segment(cursor, dofs),
          "poses@t=" + std::to_string(finalT + 1),
          tol);
      cursor += dofs;
    }
  }
  return anyError;
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setIncludeMasses(bool value)
{
  mIncludeMasses = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setIncludeCOMs(bool value)
{
  mIncludeCOMs = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setIncludeInertias(bool value)
{
  mIncludeInertias = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setIncludePoses(bool value)
{
  mIncludePoses = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setIncludeMarkerOffsets(bool value)
{
  mIncludeMarkerOffsets = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setIncludeBodyScales(bool value)
{
  mIncludeBodyScales = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setResidualWeight(s_t weight)
{
  mResidualWeight = weight;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setMarkerWeight(s_t weight)
{
  mMarkerWeight = weight;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setJointWeight(s_t weight)
{
  mJointWeight = weight;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setResidualUseL1(bool l1)
{
  mResidualUseL1 = l1;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setMarkerUseL1(bool l1)
{
  mMarkerUseL1 = l1;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setRegularizeMasses(s_t value)
{
  mRegularizeMasses = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setRegularizeCOMs(s_t value)
{
  mRegularizeCOMs = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setRegularizeInertias(s_t value)
{
  mRegularizeInertias = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setRegularizeBodyScales(s_t value)
{
  mRegularizeBodyScales = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setRegularizePoses(s_t value)
{
  mRegularizePoses = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setRegularizeTrackingMarkerOffsets(
    s_t value)
{
  mRegularizeTrackingMarkerOffsets = value;
  return *(this);
}

//==============================================================================
DynamicsFitProblem& DynamicsFitProblem::setRegularizeAnatomicalMarkerOffsets(
    s_t value)
{
  mRegularizeAnatomicalMarkerOffsets = value;
  return *(this);
}

//------------------------- Ipopt::TNLP --------------------------------------

//==============================================================================
/// \brief Method to return some info about the nlp
bool DynamicsFitProblem::get_nlp_info(
    Ipopt::Index& n,
    Ipopt::Index& m,
    Ipopt::Index& nnz_jac_g,
    Ipopt::Index& nnz_h_lag,
    Ipopt::TNLP::IndexStyleEnum& index_style)
{
  // Set the number of decision variables
  n = getProblemSize();

  // Set the total number of constraints
  m = getConstraintSize();

  // Set the number of entries in the constraint Jacobian
  nnz_jac_g = computeSparseConstraintsJacobian().size();

  // Set the number of entries in the Hessian
  nnz_h_lag = n * n;

  // use the C style indexing (0-based)
  index_style = Ipopt::TNLP::C_STYLE;

  return true;
}

//==============================================================================
/// \brief Method to return the bounds for my problem
bool DynamicsFitProblem::get_bounds_info(
    Ipopt::Index n,
    Ipopt::Number* x_l,
    Ipopt::Number* x_u,
    Ipopt::Index m,
    Ipopt::Number* g_l,
    Ipopt::Number* g_u)
{
  // Lower and upper bounds on X
  Eigen::Map<Eigen::VectorXd> upperBounds(x_u, n);
  upperBounds.setConstant(std::numeric_limits<double>::infinity());
  Eigen::Map<Eigen::VectorXd> lowerBounds(x_l, n);
  lowerBounds.setConstant(-1 * std::numeric_limits<double>::infinity());

  upperBounds = flattenUpperBound().cast<double>();
  lowerBounds = flattenLowerBound().cast<double>();

  // Our constraint function has to be 0
  Eigen::Map<Eigen::VectorXd> constraintUpperBounds(g_u, m);
  constraintUpperBounds.setZero();
  Eigen::Map<Eigen::VectorXd> constraintLowerBounds(g_l, m);
  constraintLowerBounds.setZero();

  return true;
}

//==============================================================================
/// \brief Method to return the starting point for the algorithm
bool DynamicsFitProblem::get_starting_point(
    Ipopt::Index n,
    bool init_x,
    Ipopt::Number* _x,
    bool init_z,
    Ipopt::Number* z_L,
    Ipopt::Number* z_U,
    Ipopt::Index m,
    bool init_lambda,
    Ipopt::Number* lambda)
{
  // Here, we assume we only have starting values for x
  (void)init_x;
  assert(init_x == true);
  (void)init_z;
  assert(init_z == false);
  (void)init_lambda;
  assert(init_lambda == false);
  // We don't set the lagrange multipliers
  (void)z_L;
  (void)z_U;
  (void)m;
  (void)lambda;

  if (init_x)
  {
    Eigen::Map<Eigen::VectorXd> x(_x, n);
    x = flatten().cast<double>();
  }

  return true;
}

//==============================================================================
/// \brief Method to return the objective value
bool DynamicsFitProblem::eval_f(
    Ipopt::Index _n,
    const Ipopt::Number* _x,
    bool _new_x,
    Ipopt::Number& _obj_value)
{
  (void)_new_x;
  Eigen::Map<const Eigen::VectorXd> x(_x, _n);

  _obj_value = (double)computeLoss(x.cast<s_t>(), true);

  return true;
}

//==============================================================================
/// \brief Method to return the gradient of the objective
bool DynamicsFitProblem::eval_grad_f(
    Ipopt::Index _n,
    const Ipopt::Number* _x,
    bool _new_x,
    Ipopt::Number* _grad_f)
{
  (void)_new_x;
  Eigen::Map<const Eigen::VectorXd> x(_x, _n);
  Eigen::Map<Eigen::VectorXd> grad(_grad_f, _n);

  grad = computeGradient(x.cast<s_t>()).cast<double>();

  return true;
}

//==============================================================================
/// \brief Method to return the constraint residuals
bool DynamicsFitProblem::eval_g(
    Ipopt::Index _n,
    const Ipopt::Number* _x,
    bool _new_x,
    Ipopt::Index _m,
    Ipopt::Number* _g)
{
  (void)_new_x;
  Eigen::Map<const Eigen::VectorXd> x(_x, _n);
  Eigen::Map<Eigen::VectorXd> g(_g, _m);

  g = computeConstraints(x.cast<s_t>()).cast<double>();

  return true;
}

//==============================================================================
/// \brief Method to return:
///        1) The structure of the jacobian (if "values" is nullptr)
///        2) The values of the jacobian (if "values" is not nullptr)
bool DynamicsFitProblem::eval_jac_g(
    Ipopt::Index _n,
    const Ipopt::Number* _x,
    bool _new_x,
    Ipopt::Index _m,
    Ipopt::Index _nnzj,
    Ipopt::Index* _iRow,
    Ipopt::Index* _jCol,
    Ipopt::Number* _values)
{
  (void)_new_x;
  (void)_m;

  // If the iRow and jCol arguments are not nullptr, then IPOPT wants you to
  // fill in the sparsity structure of the Jacobian (the row and column
  // indices only). At this time, the x argument and the values argument will
  // be nullptr.

  std::vector<std::tuple<int, int, s_t>> sparse
      = computeSparseConstraintsJacobian();

  if (nullptr == _x)
  {
    Eigen::Map<Eigen::VectorXi> rows(_iRow, _nnzj);
    Eigen::Map<Eigen::VectorXi> cols(_jCol, _nnzj);
    for (int i = 0; i < sparse.size(); i++)
    {
      rows(i) = (double)std::get<0>(sparse[i]);
      cols(i) = (double)std::get<1>(sparse[i]);
    }
  }
  else
  {
    // Return the concatenated gradient of everything
    Eigen::Map<const Eigen::VectorXd> x(_x, _n);
    Eigen::Map<Eigen::VectorXd> vals(_values, _nnzj);

    for (int i = 0; i < sparse.size(); i++)
    {
      vals(i) = (double)std::get<2>(sparse[i]);
    }
  }

  return true;
}

//==============================================================================
/// \brief Method to return:
///        1) The structure of the hessian of the lagrangian (if "values" is
///           nullptr)
///        2) The values of the hessian of the lagrangian (if "values" is not
///           nullptr)
bool DynamicsFitProblem::eval_h(
    Ipopt::Index _n,
    const Ipopt::Number* _x,
    bool _new_x,
    Ipopt::Number _obj_factor,
    Ipopt::Index _m,
    const Ipopt::Number* _lambda,
    bool _new_lambda,
    Ipopt::Index _nele_hess,
    Ipopt::Index* _iRow,
    Ipopt::Index* _jCol,
    Ipopt::Number* _values)
{
  (void)_n;
  (void)_x;
  (void)_new_x;
  (void)_obj_factor;
  (void)_m;
  (void)_lambda;
  (void)_new_lambda;
  (void)_nele_hess;
  (void)_iRow;
  (void)_jCol;
  (void)_values;
  return false;
}

//==============================================================================
/// \brief This method is called when the algorithm is complete so the TNLP
///        can store/write the solution
void DynamicsFitProblem::finalize_solution(
    Ipopt::SolverReturn _status,
    Ipopt::Index _n,
    const Ipopt::Number* _x,
    const Ipopt::Number* _z_L,
    const Ipopt::Number* _z_U,
    Ipopt::Index _m,
    const Ipopt::Number* _g,
    const Ipopt::Number* _lambda,
    Ipopt::Number _obj_value,
    const Ipopt::IpoptData* _ip_data,
    Ipopt::IpoptCalculatedQuantities* _ip_cq)
{
  (void)_status;
  (void)_n;
  (void)_x;
  (void)_z_L;
  (void)_z_U;
  (void)_m;
  (void)_g;
  (void)_lambda;
  (void)_obj_value;
  (void)_ip_data;
  (void)_ip_cq;
  // Eigen::Map<const Eigen::VectorXd> x(_x, _n);
  std::cout << "Recovering state with best loss: iteration "
            << mBestObjectiveValueIteration << " with " << mBestObjectiveValue
            << std::endl;
  Eigen::VectorXs x = mBestObjectiveValueState;

  unflatten(x);

  if (mIncludeBodyScales)
  {
    mInit->groupScales = mSkeleton->getGroupScales();
  }
  if (mIncludeCOMs)
  {
    mInit->bodyCom.resize(3, mSkeleton->getNumBodyNodes());
    for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
    {
      mInit->bodyCom.col(i)
          = mSkeleton->getBodyNode(i)->getInertia().getLocalCOM();
    }
  }
  if (mIncludeInertias)
  {
    mInit->bodyInertia.resize(6, mSkeleton->getNumBodyNodes());
    for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
    {
      mInit->bodyInertia.col(i)
          = mSkeleton->getBodyNode(i)->getInertia().getMomentVector();
    }
  }
  if (mIncludeMasses)
  {
    mInit->bodyMasses.resize(mSkeleton->getNumBodyNodes());
    for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
    {
      mInit->bodyMasses(i) = mSkeleton->getBodyNode(i)->getInertia().getMass();
    }
  }
  if (mIncludePoses)
  {
    mInit->poseTrials = mPoses;
  }
  if (mIncludeMarkerOffsets)
  {
    for (int i = 0; i < mMarkerNames.size(); i++)
    {
      mInit->markerOffsets[mMarkerNames[i]] = mMarkers[i].second;
    }
  }
}

//==============================================================================
bool DynamicsFitProblem::intermediate_callback(
    Ipopt::AlgorithmMode mode,
    Ipopt::Index iter,
    Ipopt::Number obj_value,
    Ipopt::Number inf_pr,
    Ipopt::Number inf_du,
    Ipopt::Number mu,
    Ipopt::Number d_norm,
    Ipopt::Number regularization_size,
    Ipopt::Number alpha_du,
    Ipopt::Number alpha_pr,
    Ipopt::Index ls_trials,
    const Ipopt::IpoptData* ip_data,
    Ipopt::IpoptCalculatedQuantities* ip_cq)
{
  (void)mode;
  (void)iter;
  (void)obj_value;
  (void)inf_pr;
  (void)inf_du;
  (void)mu;
  (void)d_norm;
  (void)regularization_size;
  (void)alpha_du;
  (void)alpha_pr;
  (void)ls_trials;
  (void)ip_data;
  (void)ip_cq;

  if (obj_value < mBestObjectiveValue && abs(inf_pr) < 1.0)
  {
    mBestObjectiveValueIteration = iter;
    mBestObjectiveValue = obj_value;
    mBestObjectiveValueState = mLastX;
  }

  return true;
}

//==============================================================================
DynamicsFitter::DynamicsFitter(
    std::shared_ptr<dynamics::Skeleton> skeleton,
    std::vector<dynamics::BodyNode*> footNodes,
    dynamics::MarkerMap markerMap,
    std::vector<std::string> trackingMarkers)
  : mSkeleton(skeleton),
    mFootNodes(footNodes),
    mMarkerMap(markerMap),
    mTrackingMarkers(trackingMarkers),
    mTolerance(1e-8),
    mIterationLimit(500),
    mLBFGSHistoryLength(8),
    mCheckDerivatives(false),
    mPrintFrequency(1),
    mSilenceOutput(false),
    mDisableLinesearch(false){

    };

//==============================================================================
// This bundles together the objects we need in order to track a dynamics
// problem around through multiple steps of optimization
std::shared_ptr<DynamicsInitialization> DynamicsFitter::createInitialization(
    std::shared_ptr<dynamics::Skeleton> skel,
    dynamics::MarkerMap markerMap,
    std::vector<std::string> trackingMarkers,
    std::vector<dynamics::BodyNode*> grfNodes,
    std::vector<std::vector<ForcePlate>> forcePlateTrials,
    std::vector<Eigen::MatrixXs> poseTrials,
    std::vector<int> framesPerSecond,
    std::vector<std::vector<std::map<std::string, Eigen::Vector3s>>>
        markerObservationTrials)
{
  std::shared_ptr<DynamicsInitialization> init
      = std::make_shared<DynamicsInitialization>();
  init->forcePlateTrials = forcePlateTrials;
  init->originalPoseTrials = poseTrials;
  init->markerObservationTrials = markerObservationTrials;
  init->trackingMarkers = trackingMarkers;
  init->updatedMarkerMap = markerMap;
  for (auto& pair : markerMap)
  {
    init->markerOffsets[pair.first] = pair.second.second;
  }
  init->bodyMasses = skel->getLinkMasses();
  init->groupScales = skel->getGroupScales();
  init->bodyCom.resize(3, skel->getNumBodyNodes());
  for (int i = 0; i < skel->getNumBodyNodes(); i++)
  {
    init->bodyCom.col(i) = skel->getBodyNode(i)->getInertia().getLocalCOM();
  }
  init->bodyInertia.resize(6, skel->getNumBodyNodes());
  for (int i = 0; i < skel->getNumBodyNodes(); i++)
  {
    init->bodyInertia.col(i)
        = skel->getBodyNode(i)->getInertia().getMomentVector();
  }
  init->bodyMasses.resize(skel->getNumBodyNodes());
  for (int i = 0; i < skel->getNumBodyNodes(); i++)
  {
    init->bodyMasses(i) = skel->getBodyNode(i)->getInertia().getMass();
  }

  // Initially smooth the accelerations just a little bit

  for (int i = 0; i < init->originalPoseTrials.size(); i++)
  {
    utils::AccelerationSmoother smoother(
        init->originalPoseTrials[i].cols(), 0.05);
    init->poseTrials.push_back(smoother.smooth(init->originalPoseTrials[i]));
    init->trialTimesteps.push_back(1.0 / framesPerSecond[i]);
  }

  // Match force plates to the feet

  Eigen::VectorXs originalPose = skel->getPositions();

  init->grfBodyNodes = grfNodes;
  for (int i = 0; i < grfNodes.size(); i++)
  {
    init->grfBodyIndices.push_back(grfNodes[i]->getIndexInSkeleton());
  }

  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    std::vector<ForcePlate> forcePlates = init->forcePlateTrials[trial];

    Eigen::MatrixXs& poses = init->poseTrials[trial];

    Eigen::MatrixXs GRF
        = Eigen::MatrixXs::Zero(grfNodes.size() * 6, poses.cols());

    for (int t = 0; t < poses.cols(); t++)
    {
      skel->setPositions(poses.col(t));

      for (int i = 0; i < forcePlates.size(); i++)
      {
        Eigen::Vector3s cop = forcePlates[i].centersOfPressure[t];
        Eigen::Vector3s force = forcePlates[i].forces[t];
        Eigen::Vector3s moments = forcePlates[i].moments[t];
        Eigen::Vector6s wrench = Eigen::Vector6s::Zero();
        wrench.head<3>() = moments;
        wrench.tail<3>() = force;
        Eigen::Isometry3s wrenchT = Eigen::Isometry3s::Identity();
        wrenchT.translation() = cop;
        Eigen::Vector6s worldWrench = math::dAdInvT(wrenchT, wrench);

        // Every force from force plates must be accounted for somewhere. Simply
        // assign it to the nearest foot
        int closestFoot = -1;
        s_t minDist = (s_t)std::numeric_limits<double>::infinity();
        for (int i = 0; i < grfNodes.size(); i++)
        {
          Eigen::Vector3s footLoc
              = grfNodes[i]->getWorldTransform().translation();
          s_t dist = (footLoc - cop).norm();
          if (dist < minDist)
          {
            minDist = dist;
            closestFoot = i;
          }
        }
        assert(closestFoot != -1);

        // If multiple force plates assign to the same foot, sum up the forces
        GRF.block<6, 1>(closestFoot * 6, t) += worldWrench;
      }
      std::cout << "Trial " << trial << " t=" << t << ": GRF norm "
                << GRF.col(t).norm() << std::endl;
    }
    init->grfTrials.push_back(GRF);
  }

  // Make copies of data to use for regularization
  init->originalPoses = init->originalPoseTrials;
  init->originalGroupMasses = skel->getGroupMasses();
  init->originalGroupCOMs = skel->getGroupCOMs();
  init->originalGroupInertias = skel->getGroupInertias();
  init->originalGroupScales = skel->getGroupScales();
  for (auto& pair : init->markerOffsets)
  {
    init->originalMarkerOffsets[pair.first] = pair.second;
  }

  return init;
}

//==============================================================================
// This creates an optimization problem from a kinematics initialization
std::shared_ptr<DynamicsInitialization> DynamicsFitter::createInitialization(
    std::shared_ptr<dynamics::Skeleton> skel,
    MarkerInitialization* kinematicInit,
    std::vector<std::string> trackingMarkers,
    std::vector<dynamics::BodyNode*> grfNodes,
    std::vector<std::vector<ForcePlate>> forcePlateTrials,
    std::vector<int> framesPerSecond,
    std::vector<std::vector<std::map<std::string, Eigen::Vector3s>>>
        markerObservationTrials)
{
  // Split the incoming poses into individual trial matrices
  std::vector<Eigen::MatrixXs> poseTrials;
  int cursor = 0;
  for (int trial = 0; trial < markerObservationTrials.size(); trial++)
  {
    Eigen::MatrixXs poses = Eigen::MatrixXs(
        skel->getNumDofs(), markerObservationTrials[trial].size());
    for (int i = 0; i < markerObservationTrials[trial].size(); i++)
    {
      poses.col(i) = kinematicInit->poses.col(cursor);
      cursor++;
    }
    poseTrials.push_back(poses);
  }

  // Create the basic initialization
  std::shared_ptr<DynamicsInitialization> init = createInitialization(
      skel,
      kinematicInit->updatedMarkerMap,
      trackingMarkers,
      grfNodes,
      forcePlateTrials,
      poseTrials,
      framesPerSecond,
      markerObservationTrials);

  // Copy over the joint data
  init->joints = kinematicInit->joints;
  init->jointsAdjacentMarkers = kinematicInit->jointsAdjacentMarkers;
  init->jointWeights = kinematicInit->jointWeights;
  init->axisWeights = kinematicInit->axisWeights;

  cursor = 0;
  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    Eigen::MatrixXs trialJointCenters = Eigen::MatrixXs::Zero(
        kinematicInit->jointCenters.rows(), init->poseTrials[trial].cols());
    Eigen::MatrixXs trialJointAxis = Eigen::MatrixXs::Zero(
        kinematicInit->jointAxis.rows(), init->poseTrials[trial].cols());

    for (int t = 0; t < init->poseTrials[trial].cols(); t++)
    {
      trialJointCenters.col(t) = kinematicInit->jointCenters.col(cursor);
      trialJointAxis.col(t) = kinematicInit->jointAxis.col(cursor);
      cursor++;
    }
    // getMarkerWorldPositionsJacobianWrtJointPositions

    init->jointCenters.push_back(trialJointCenters);
    init->jointAxis.push_back(trialJointAxis);
  }

  return init;
}

//==============================================================================
// This computes and returns the positions of the center of mass at each
// frame
std::vector<Eigen::Vector3s> DynamicsFitter::comPositions(
    std::shared_ptr<DynamicsInitialization> init, int trial)
{
  Eigen::VectorXs originalMasses = mSkeleton->getLinkMasses();
  Eigen::VectorXs originalPoses = mSkeleton->getPositions();

  if (trial >= init->poseTrials.size())
  {
    std::cout << "Trying to get accelerations on an out-of-bounds trial: "
              << trial << " >= " << init->poseTrials.size() << std::endl;
    exit(1);
  }
  const Eigen::MatrixXs& poses = init->poseTrials[trial];

  std::vector<Eigen::Vector3s> coms;

  mSkeleton->setLinkMasses(init->bodyMasses);
  for (int timestep = 0; timestep < poses.cols(); timestep++)
  {
    mSkeleton->setPositions(poses.col(timestep));
    Eigen::Vector3s weightedCOM = Eigen::Vector3s::Zero();
    s_t totalMass = 0.0;
    for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
    {
      totalMass += mSkeleton->getBodyNode(i)->getMass();
      weightedCOM += mSkeleton->getBodyNode(i)->getCOM()
                     * mSkeleton->getBodyNode(i)->getMass();
    }
    weightedCOM /= totalMass;
    coms.push_back(weightedCOM);
  }

  mSkeleton->setLinkMasses(originalMasses);
  mSkeleton->setPositions(originalPoses);

  return coms;
}

//==============================================================================
// This computes and returns the acceleration of the center of mass at each
// frame
std::vector<Eigen::Vector3s> DynamicsFitter::comAccelerations(
    std::shared_ptr<DynamicsInitialization> init, int trial)
{
  s_t dt = init->trialTimesteps[trial];
  std::vector<Eigen::Vector3s> coms = comPositions(init, trial);
  std::vector<Eigen::Vector3s> accs;
  for (int i = 0; i < coms.size() - 2; i++)
  {
    Eigen::Vector3s v1 = (coms[i + 1] - coms[i]) / dt;
    Eigen::Vector3s v2 = (coms[i + 2] - coms[i + 1]) / dt;
    Eigen::Vector3s acc = (v2 - v1) / dt;
    accs.push_back(acc);
  }
  return accs;
}

//==============================================================================
// This computes and returns a list of the net forces on the center of mass,
// given the motion and link masses
std::vector<Eigen::Vector3s> DynamicsFitter::impliedCOMForces(
    std::shared_ptr<DynamicsInitialization> init,
    int trial,
    bool includeGravity)
{
  std::vector<Eigen::Vector3s> accs = comAccelerations(init, trial);
  s_t totalMass = init->bodyMasses.sum();

  Eigen::Vector3s gravity = Eigen::Vector3s(0, -9.81, 0);

  std::vector<Eigen::Vector3s> forces;
  for (int i = 0; i < accs.size(); i++)
  {
    // f + m * g = m * a
    // f = m * (a - g)
    Eigen::Vector3s a = accs[i];
    if (includeGravity)
    {
      a -= gravity;
    }
    forces.push_back(a * totalMass);
  }
  return forces;
}

//==============================================================================
// This returns a list of the total GRF force on the body at each timestep
std::vector<Eigen::Vector3s> DynamicsFitter::measuredGRFForces(
    std::shared_ptr<DynamicsInitialization> init, int trial)
{
  std::vector<Eigen::Vector3s> forces;

  for (int timestep = 0; timestep < init->poseTrials[trial].cols() - 2;
       timestep++)
  {
    Eigen::Vector3s totalForce = Eigen::Vector3s::Zero();
    for (int i = 0; i < init->forcePlateTrials[trial].size(); i++)
    {
      totalForce += init->forcePlateTrials[trial][i].forces[timestep];
    }

    forces.push_back(totalForce);
  }

  return forces;
}

//==============================================================================
// 0. Estimate when each foot is in contact with the ground, which we can use
// to infer when we're missing GRF data on certain timesteps, so we don't let
// it mess with our optimization.
void DynamicsFitter::estimateFootGroundContacts(
    std::shared_ptr<DynamicsInitialization> init)
{
  Eigen::VectorXs originalPose = mSkeleton->getPositions();

  // 0. Expand the set of grf bodies to include any childen that are not already
  // themselves grf bodies
  //
  // Result goes in std::vector<std::vector<dynamics::BodyNode*>> contactBodies
  // in `init`
  for (int i = 0; i < init->grfBodyNodes.size(); i++)
  {
    dynamics::BodyNode* rootBody = init->grfBodyNodes[i];
    std::vector<dynamics::BodyNode*> extendedContactBodies;

    std::queue<dynamics::BodyNode*> queue;
    queue.push(rootBody);
    while (!queue.empty())
    {
      dynamics::BodyNode* cursor = queue.front();
      queue.pop();

      extendedContactBodies.push_back(cursor);
      for (int j = 0; j < cursor->getNumChildBodyNodes(); j++)
      {
        dynamics::BodyNode* child = cursor->getChildBodyNode(j);
        if (std::find(
                init->grfBodyNodes.begin(), init->grfBodyNodes.end(), child)
            == init->grfBodyNodes.end())
        {
          queue.push(child);
        }
      }
    }

    init->contactBodies.push_back(extendedContactBodies);
  }

  for (int trial = 0; trial < init->forcePlateTrials.size(); trial++)
  {
    bool noGroundCorners = true;

    // 1.1. First check for the ground level from the force plates

    s_t groundHeight = std::numeric_limits<s_t>::infinity();
    bool flatGround = true;
    for (ForcePlate& forcePlate : init->forcePlateTrials[trial])
    {
      for (Eigen::Vector3s corner : forcePlate.corners)
      {
        if (noGroundCorners)
        {
          groundHeight = corner(1);
          noGroundCorners = false;
        }
        else
        {
          if (abs(groundHeight - corner(1)) < 1e-8)
          {
            flatGround = false;
          }
        }
      }
    }

    // 1.2. Check the ground level from the GRF data, if we don't have force
    // plate data

    if (noGroundCorners)
    {
      for (int t = 0; t < init->poseTrials[trial].cols(); t++)
      {
        for (ForcePlate& forcePlate : init->forcePlateTrials[trial])
        {
          s_t height = forcePlate.centersOfPressure[t](1);
          if (noGroundCorners)
          {
            groundHeight = height;
            noGroundCorners = false;
          }
          else if (height < groundHeight)
          {
            groundHeight = height;
          }
        }
      }
    }

    assert(!isnan(groundHeight));

    // 2.0. Check for the size of the contact spheres to check for contact
    // Since each grf body actually gets a (potentially) extended set of contact
    // bodies attached to it, each grf body gets an array of contact sphere
    // radii, one for each contact sphere.

    std::vector<std::vector<s_t>> grfContactSphereSizes;
    for (int b = 0; b < init->contactBodies.size(); b++)
    {
      grfContactSphereSizes.emplace_back();
      for (int c = 0; c < init->contactBodies[b].size(); c++)
      {
        grfContactSphereSizes[grfContactSphereSizes.size() - 1].push_back(0.0);
      }
    }

    for (int t = 0; t < init->poseTrials[trial].cols(); t++)
    {
      mSkeleton->setPositions(init->poseTrials[trial].col(t));

      for (int b = 0; b < init->grfBodyNodes.size(); b++)
      {
        bool footActive
            = (init->grfTrials[trial].col(t).segment<6>(b * 6).squaredNorm()
               > 1e-3);

        // If this foot is active on this timestep, then we have to resize our
        // contact spheres to ensure that they show contact on this frame. We do
        // this by ensuring that the closest sphere is at least large enough to
        // hit contact.
        if (footActive)
        {
          // Check which of the contact bodies is closest to the ground
          s_t minDist = std::numeric_limits<s_t>::infinity();
          int closestBody = -1;
          for (int c = 0; c < init->contactBodies[b].size(); c++)
          {
            auto* body = init->contactBodies[b][c];
            Eigen::Vector3s worldPos = body->getWorldTransform().translation();
            s_t dist = worldPos(1) - groundHeight;
            if (dist < minDist)
            {
              minDist = dist;
              closestBody = c;
            }
          }

          // If our closest sphere needs to expand to hit the ground, then
          // expand it
          if (minDist > grfContactSphereSizes[b][closestBody])
          {
            grfContactSphereSizes[b][closestBody] = minDist;
          }
        }
      }
    }

    init->grfBodyContactSphereRadius.push_back(grfContactSphereSizes);
    init->groundHeight.push_back(groundHeight);
    init->flatGround.push_back(flatGround);

    // 3. Create the default force plate size, if needed.

    // 3.1. We only need a default force plate if any of the force plates in the
    // trials lack the corners array
    bool needDefaultForcePlate = false;
    for (ForcePlate& forcePlate : init->forcePlateTrials[trial])
    {
      if (forcePlate.corners.size() == 0)
      {
        needDefaultForcePlate = true;
        break;
      }
    }
    // 3.2. If we need the default force plate, now we want to go create a
    // rectangle to hold all the GRF data.
    std::vector<Eigen::Vector3s> defaultCorners;
    if (needDefaultForcePlate)
    {
      s_t minX = std::numeric_limits<s_t>::infinity();
      s_t maxX = -std::numeric_limits<s_t>::infinity();
      s_t minZ = std::numeric_limits<s_t>::infinity();
      s_t maxZ = -std::numeric_limits<s_t>::infinity();
      for (int t = 0; t < init->poseTrials[trial].cols(); t++)
      {
        for (ForcePlate& forcePlate : init->forcePlateTrials[trial])
        {
          if (forcePlate.centersOfPressure[t](0) < minX)
          {
            minX = forcePlate.centersOfPressure[t](0);
          }
          if (forcePlate.centersOfPressure[t](0) > maxX)
          {
            maxX = forcePlate.centersOfPressure[t](0);
          }
          if (forcePlate.centersOfPressure[t](2) < minZ)
          {
            minZ = forcePlate.centersOfPressure[t](2);
          }
          if (forcePlate.centersOfPressure[t](2) > maxZ)
          {
            maxZ = forcePlate.centersOfPressure[t](2);
          }
        }
      }

      // Add 10cm to each side, to be very conservative
      s_t padding = 0.10;
      minX -= padding;
      maxX += padding;
      minZ -= padding;
      maxZ += padding;

      defaultCorners.push_back(Eigen::Vector3s(minX, groundHeight, minZ));
      defaultCorners.push_back(Eigen::Vector3s(minX, groundHeight, maxZ));
      defaultCorners.push_back(Eigen::Vector3s(maxX, groundHeight, maxZ));
      defaultCorners.push_back(Eigen::Vector3s(maxX, groundHeight, minZ));
    }
    init->defaultForcePlateCorners.push_back(defaultCorners);

    // 4. Determine foot-ground contact at each trial, and figure out which
    // timesteps we think we're receiving force that isn't measured by a force
    // plate.

    std::vector<std::vector<Eigen::Vector3s>> sortedForcePlateCorners;
    for (ForcePlate& plate : init->forcePlateTrials[trial])
    {
      if (plate.corners.size() > 0)
      {
        sortedForcePlateCorners.push_back(plate.corners);
        math::prepareConvex2DShape(
            sortedForcePlateCorners[sortedForcePlateCorners.size() - 1],
            plate.corners[0],
            Eigen::Vector3s::UnitX(),
            Eigen::Vector3s::UnitZ());
      }
    }
    if (init->defaultForcePlateCorners[trial].size() > 0)
    {
      math::prepareConvex2DShape(
          init->defaultForcePlateCorners[trial],
          init->defaultForcePlateCorners[trial][0],
          Eigen::Vector3s::UnitX(),
          Eigen::Vector3s::UnitZ());
    }

    std::vector<std::vector<bool>> trialForceActive;
    std::vector<std::vector<bool>> trialSphereInContact;
    std::vector<std::vector<bool>> trialOffForcePlate;
    std::vector<bool> trialAnyOffForcePlate;
    for (int t = 0; t < init->poseTrials[trial].cols(); t++)
    {
      mSkeleton->setPositions(init->poseTrials[trial].col(t));

      std::vector<bool> forceActive;
      std::vector<bool> sphereInContact;
      std::vector<bool> offForcePlate;
      bool anyContactIsSus = false;
      for (int b = 0; b < init->grfBodyNodes.size(); b++)
      {
        // 4.1. Check the GRF to see if we are measured as being in contact
        bool footActive
            = (init->grfTrials[trial].col(t).segment<6>(b * 6).squaredNorm()
               > 1e-3);
        forceActive.push_back(footActive);

        // 4.2. Check all the contact bodies assigned to this GRF node to guess
        // if we think we might be in contact here
        bool inContact = false;
        for (int c = 0; c < init->contactBodies[b].size(); c++)
        {
          auto* body = init->contactBodies[b][c];
          Eigen::Vector3s worldPos = body->getWorldTransform().translation();
          s_t dist = worldPos(1) - groundHeight;
          if (dist < grfContactSphereSizes[b][c])
          {
            inContact = true;
          }
        }
        sphereInContact.push_back(inContact);

        // 4.3. If we think from the collider heuristic that we might be in
        // contact, but then we don't actually have any GRF, we need to be
        // suspicious that this might be a contact outside a force plate, which
        // would mean that our inverse dynamics should ignore these frames.
        bool contactIsSus = false;
        if (inContact && !footActive)
        {
          // 4.3.1. Iterate over each contact body, and check if its inside any
          // of the force plates.
          bool anyInPlate = false;
          for (int c = 0; c < init->contactBodies[b].size(); c++)
          {
            auto* body = init->contactBodies[b][c];
            Eigen::Vector3s worldPos = body->getWorldTransform().translation();
            for (ForcePlate& plate : init->forcePlateTrials[trial])
            {
              if (plate.corners.size() > 0)
              {
                if (math::convex2DShapeContains(
                        worldPos,
                        plate.corners,
                        plate.worldOrigin,
                        Eigen::Vector3s::UnitX(),
                        Eigen::Vector3s::UnitZ()))
                {
                  anyInPlate = true;
                  break;
                }
              }
            }
            if (init->defaultForcePlateCorners[trial].size() > 0)
            {
              if (math::convex2DShapeContains(
                      worldPos,
                      init->defaultForcePlateCorners[trial],
                      init->defaultForcePlateCorners[trial][0],
                      Eigen::Vector3s::UnitX(),
                      Eigen::Vector3s::UnitZ()))
              {
                anyInPlate = true;
              }
            }
            if (anyInPlate)
            {
              break;
            }
          }

          // 4.3.2. If we're NOT over a plate, then register this frame as
          // suspicious
          if (!anyInPlate)
          {
            contactIsSus = true;
            anyContactIsSus = true;
          }
        }

        offForcePlate.push_back(contactIsSus);
      }

      trialForceActive.push_back(forceActive);
      trialSphereInContact.push_back(sphereInContact);
      trialOffForcePlate.push_back(offForcePlate);
      trialAnyOffForcePlate.push_back(anyContactIsSus);
    }

    init->grfBodyForceActive.push_back(trialForceActive);
    init->grfBodySphereInContact.push_back(trialSphereInContact);
    init->grfBodyOffForcePlate.push_back(trialOffForcePlate);
    init->probablyMissingGRF.push_back(trialAnyOffForcePlate);
  }
}

//==============================================================================
// 1. Scale the total mass of the body (keeping the ratios of body links
// constant) to get it as close as possible to GRF gravity forces.
void DynamicsFitter::scaleLinkMassesFromGravity(
    std::shared_ptr<DynamicsInitialization> init)
{
  s_t totalGRFs = 0.0;
  s_t totalAccs = 0.0;
  s_t gravity = 9.81;
  for (int i = 0; i < init->poseTrials.size(); i++)
  {
    std::vector<Eigen::Vector3s> grfs = measuredGRFForces(init, i);
    for (Eigen::Vector3s& grf : grfs)
    {
      totalGRFs += grf(1);
    }
    std::vector<Eigen::Vector3s> accs = comAccelerations(init, i);
    for (Eigen::Vector3s& acc : accs)
    {
      totalAccs += acc(1) + gravity;
    }
  }

  std::cout << "Total ACCs: " << totalAccs << std::endl;
  std::cout << "Total mass: " << init->bodyMasses.sum() << std::endl;
  std::cout << "(Total ACCs) * (Total mass): "
            << totalAccs * init->bodyMasses.sum() << std::endl;
  std::cout << "Total GRFs: " << totalGRFs << std::endl;

  s_t impliedTotalMass = totalGRFs / totalAccs;
  std::cout << "Implied total mass: " << impliedTotalMass << std::endl;
  s_t ratio = impliedTotalMass / init->bodyMasses.sum();
  init->bodyMasses *= ratio;
  std::cout << "Adjusted total mass to match GRFs: " << init->bodyMasses.sum()
            << std::endl;
}

//==============================================================================
// 2. Estimate just link masses, while holding the positions, COMs, and inertias
// constant
void DynamicsFitter::estimateLinkMassesFromAcceleration(
    std::shared_ptr<DynamicsInitialization> init, s_t regularizationWeight)
{
  Eigen::VectorXs originalPose = mSkeleton->getPositions();

  int totalTimesteps = 0;
  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    if (init->poseTrials.at(trial).cols() > 0)
    {
      totalTimesteps += init->poseTrials.at(trial).cols() - 2;
    }
  }

  // constants
  Eigen::Vector3s gravityVector = Eigen::Vector3s(0, -9.81, 0);
  (void)gravityVector;

  // 1. Set up the problem matrices
  Eigen::MatrixXs A = Eigen::MatrixXs::Zero(
      (totalTimesteps * 3) + mSkeleton->getNumBodyNodes(),
      mSkeleton->getNumBodyNodes());
  Eigen::VectorXs g = Eigen::VectorXs::Zero(
      (totalTimesteps * 3) + mSkeleton->getNumBodyNodes());

#ifndef NDEBUG
  Eigen::MatrixXs A_no_gravity
      = Eigen::MatrixXs::Zero(totalTimesteps * 3, mSkeleton->getNumBodyNodes());
#endif

  int cursor = 0;
  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    Eigen::MatrixXs& poses = init->poseTrials[trial];
    if (poses.cols() <= 2)
      continue;

    // 1.1. Initialize empty position matrices for each body node

    std::map<std::string, Eigen::Matrix<s_t, 3, Eigen::Dynamic>>
        bodyPosesOverTime;
    for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
    {
      bodyPosesOverTime[mSkeleton->getBodyNode(i)->getName()]
          = Eigen::Matrix<s_t, 3, Eigen::Dynamic>::Zero(3, poses.cols());
    }

    // 1.2. Fill position matrices for each body node

    for (int t = 0; t < poses.cols(); t++)
    {
      mSkeleton->setPositions(poses.col(t));
      for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
      {
        bodyPosesOverTime.at(mSkeleton->getBodyNode(i)->getName()).col(t)
            = mSkeleton->getBodyNode(i)->getCOM();
      }
    }

    // 1.3. Finite difference out the accelerations for each body

    s_t dt = init->trialTimesteps[trial];
    for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
    {
      auto* body = mSkeleton->getBodyNode(i);
      for (int t = 0; t < bodyPosesOverTime.at(body->getName()).cols() - 2; t++)
      {
        Eigen::Vector3s v1 = (bodyPosesOverTime.at(body->getName()).col(t + 1)
                              - bodyPosesOverTime.at(body->getName()).col(t))
                             / dt;
        Eigen::Vector3s v2
            = (bodyPosesOverTime.at(body->getName()).col(t + 2)
               - bodyPosesOverTime.at(body->getName()).col(t + 1))
              / dt;
        Eigen::Vector3s acc = (v2 - v1) / dt;
        int timestep = cursor + t;
        A.block<3, 1>(timestep * 3, i) = acc - gravityVector;
#ifndef NDEBUG
        A_no_gravity.block<3, 1>(timestep * 3, i) = acc;
#endif
      }
    }

    // 1.4. Sum up the gravitational forces
    for (int t = 0; t < poses.cols() - 2; t++)
    {
      int timestep = cursor + t;
      for (int i = 0; i < init->forcePlateTrials[trial].size(); i++)
      {
        g.segment<3>(timestep * 3)
            += init->forcePlateTrials[trial][i].forces[t];
      }
    }

    cursor += poses.cols() - 2;
  }

  // 1.5. Add a regularization block
  int m = mSkeleton->getNumBodyNodes();
  A.block(totalTimesteps * 3, 0, m, m)
      = regularizationWeight * Eigen::MatrixXs::Identity(m, m);
  g.segment(totalTimesteps * 3, m) = regularizationWeight * init->bodyMasses;

  // 2. Now we'll go through and do some checks, if we're in debug mode
#ifndef NDEBUG
  // 2.1. Check gravity-less
  Eigen::VectorXs recoveredImpliedForces_noGravity
      = A_no_gravity * init->bodyMasses;
  std::vector<Eigen::Vector3s> comForces_noGravity;
  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    std::vector<Eigen::Vector3s> trialComForces_noGravity
        = impliedCOMForces(init, trial, false);
    comForces_noGravity.insert(
        comForces_noGravity.end(),
        trialComForces_noGravity.begin(),
        trialComForces_noGravity.end());
  }
  for (int i = 0; i < comForces_noGravity.size(); i++)
  {
    Eigen::Vector3s recovered
        = recoveredImpliedForces_noGravity.segment<3>(i * 3);
    s_t dist = (recovered - comForces_noGravity[i]).norm();
    if (dist > 1e-5)
    {
      std::cout << "Error in recovered force (no gravity) at timestep " << i
                << std::endl;
      std::cout << "Recovered from matrix form: " << recovered << std::endl;
      std::cout << "Explicit calculation: " << comForces_noGravity[i]
                << std::endl;
      std::cout << "Diff: " << dist << std::endl;
      assert(false);
    }
  }

  // 2.2. Check with gravity
  Eigen::VectorXs recoveredImpliedForces = A * init->bodyMasses;
  std::vector<Eigen::Vector3s> comForces;
  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    std::vector<Eigen::Vector3s> trialComForces
        = impliedCOMForces(init, trial, true);
    comForces.insert(
        comForces.end(), trialComForces.begin(), trialComForces.end());
  }
  for (int i = 0; i < comForces.size(); i++)
  {
    Eigen::Vector3s recovered = recoveredImpliedForces.segment<3>(i * 3);
    s_t dist = (recovered - comForces[i]).norm();
    if (dist > 1e-5)
    {
      std::cout << "Error in recovered force (with gravity) at timestep " << i
                << std::endl;
      std::cout << "Recovered from matrix form: " << recovered << std::endl;
      std::cout << "Explicit calculation: " << comForces[i] << std::endl;
      std::cout << "Diff: " << dist << std::endl;
      assert(false);
    }
  }

  // 2.3. Check GRF agreement
  std::vector<Eigen::Vector3s> grfForces;
  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    std::vector<Eigen::Vector3s> trialGrfForces
        = measuredGRFForces(init, trial);
    grfForces.insert(
        grfForces.end(), trialGrfForces.begin(), trialGrfForces.end());
  }
  for (int i = 0; i < grfForces.size(); i++)
  {
    Eigen::Vector3s recovered = g.segment<3>(i * 3);
    s_t dist = (recovered - grfForces[i]).norm();
    if (dist > 1e-5)
    {
      std::cout << "Error in GRF at timestep " << i << std::endl;
      std::cout << "Recovered from matrix form: " << recovered << std::endl;
      std::cout << "Explicit calculation: " << grfForces[i] << std::endl;
      std::cout << "Diff: " << dist << std::endl;
      assert(false);
    }
  }
#endif

  Eigen::MatrixXs debugMatrix
      = Eigen::MatrixXs::Zero(init->bodyMasses.size(), 3);
  debugMatrix.col(0) = init->bodyMasses;

  // Now that we've got the problem setup, we can factor and solve
  init->bodyMasses = A.completeOrthogonalDecomposition().solve(g);
  // TODO: solve this non-negatively, and closer to original values

  for (int i = 0; i < init->bodyMasses.size(); i++)
  {
    if (init->bodyMasses(i) < 0.01)
    {
      init->bodyMasses(i) = 0.01;
    }
  }

  debugMatrix.col(1) = init->bodyMasses;
  debugMatrix.col(2) = debugMatrix.col(1).cwiseQuotient(debugMatrix.col(0))
                       - Eigen::VectorXs::Ones(debugMatrix.rows());

  std::cout << "Original masses - New masses - Percent change: " << std::endl
            << debugMatrix << std::endl;

  mSkeleton->setPositions(originalPose);
}

//==============================================================================
// 3. Run larger optimization problems to minimize a weighted combination of
// residuals and marker RMSE, tweaking a controllable set of variables
void DynamicsFitter::runOptimization(
    std::shared_ptr<DynamicsInitialization> init,
    s_t residualWeight,
    s_t markerWeight,
    bool includeMasses,
    bool includeCOMs,
    bool includeInertias,
    bool includeBodyScales,
    bool includePoses,
    bool includeMarkerOffsets)
{
  // Before using Eigen in a multi-threaded environment, we need to explicitly
  // call this (at least prior to Eigen 3.3)
  Eigen::initParallel();

  // Create an instance of the IpoptApplication
  //
  // We are using the factory, since this allows us to compile this
  // example with an Ipopt Windows DLL
  SmartPtr<Ipopt::IpoptApplication> app = IpoptApplicationFactory();

  // Change some options
  app->Options()->SetNumericValue("tol", static_cast<double>(mTolerance));
  app->Options()->SetStringValue(
      "linear_solver",
      "mumps"); // ma27, ma55, ma77, ma86, ma97, parsido, wsmp, mumps, custom

  app->Options()->SetStringValue(
      "hessian_approximation", "limited-memory"); // limited-memory, exacty

  /*
  app->Options()->SetStringValue(
      "scaling_method", "none"); // none, gradient-based
  */

  app->Options()->SetIntegerValue("max_iter", mIterationLimit);

  // Disable LBFGS history
  app->Options()->SetIntegerValue(
      "limited_memory_max_history", mLBFGSHistoryLength);

  // Just for debugging
  if (mCheckDerivatives)
  {
    app->Options()->SetStringValue("check_derivatives_for_naninf", "yes");
    app->Options()->SetStringValue("derivative_test", "first-order");
    app->Options()->SetNumericValue("derivative_test_perturbation", 1e-6);
  }

  if (mPrintFrequency > 0)
  {
    app->Options()->SetIntegerValue("print_frequency_iter", mPrintFrequency);
  }
  else
  {
    app->Options()->SetIntegerValue(
        "print_frequency_iter", std::numeric_limits<int>::infinity());
  }
  if (mSilenceOutput)
  {
    app->Options()->SetIntegerValue("print_level", 0);
  }
  if (mDisableLinesearch)
  {
    app->Options()->SetIntegerValue("max_soc", 0);
    app->Options()->SetStringValue("accept_every_trial_step", "yes");
  }
  app->Options()->SetIntegerValue("watchdog_shortened_iter_trigger", 0);

  std::shared_ptr<BilevelFitResult> result
      = std::make_shared<BilevelFitResult>();

  // Initialize the IpoptApplication and process the options
  Ipopt::ApplicationReturnStatus status;
  status = app->Initialize();
  if (status != Solve_Succeeded)
  {
    std::cout << std::endl
              << std::endl
              << "*** Error during initialization!" << std::endl;
    return;
  }

  // This will automatically free the problem object when finished,
  // through `problemPtr`. `problem` NEEDS TO BE ON THE HEAP or it will
  // crash. If you try to leave `problem` on the stack, you'll get invalid
  // free exceptions when IPOpt attempts to free it.
  DynamicsFitProblem* problem = new DynamicsFitProblem(
      init, mSkeleton, mMarkerMap, mTrackingMarkers, mFootNodes);
  problem->setResidualWeight(
      problem->mResidualUseL1 ? residualWeight
                              : residualWeight * residualWeight);
  problem->setMarkerWeight(
      problem->mMarkerUseL1 ? markerWeight : markerWeight * markerWeight);
  problem->setIncludeMasses(includeMasses);
  problem->setIncludeCOMs(includeCOMs);
  problem->setIncludeInertias(includeInertias);
  problem->setIncludeBodyScales(includeBodyScales);
  problem->setIncludePoses(includePoses);
  problem->setIncludeMarkerOffsets(includeMarkerOffsets);

  /*
  Eigen::VectorXs x = problem->flatten();
  Eigen::VectorXs fd = problem->finiteDifferenceGradient(x);
  Eigen::VectorXs analytical = problem->computeGradient(x);
  if (problem->debugErrors(fd, analytical, 1e-8))
  {
    std::cout << "Detected gradient errors in DynamicsFitter!! Quitting."
              << std::endl;
    exit(1);
  }
  */

  SmartPtr<DynamicsFitProblem> problemPtr(problem);

  // This will automatically write results back to `init` on success.
  status = app->OptimizeTNLP(problemPtr);

  if (status == Solve_Succeeded)
  {
    // Retrieve some statistics about the solve
    Index iter_count = app->Statistics()->IterationCount();
    std::cout << std::endl
              << std::endl
              << "*** The problem solved in " << iter_count << " iterations!"
              << std::endl;

    Number final_obj = app->Statistics()->FinalObjective();
    std::cout << std::endl
              << std::endl
              << "*** The final value of the objective function is "
              << final_obj << '.' << std::endl;
  }
}

//==============================================================================
// Get the average RMSE, in meters, of the markers
s_t DynamicsFitter::computeAverageMarkerRMSE(
    std::shared_ptr<DynamicsInitialization> init)
{
  Eigen::VectorXs originalPoses = mSkeleton->getPositions();
  Eigen::VectorXs originalScales = mSkeleton->getGroupScales();
  mSkeleton->setGroupScales(init->groupScales);

  s_t result = 0;
  int count = 0;
  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    for (int i = 0; i < init->poseTrials[trial].cols(); i++)
    {
      mSkeleton->setPositions(init->poseTrials[trial].col(i));
      auto simulatedMarkers
          = mSkeleton->getMarkerMapWorldPositions(init->updatedMarkerMap);
      auto markers = init->markerObservationTrials[trial][i];
      for (auto pair : simulatedMarkers)
      {
        if (markers.count(pair.first))
        {
          result += (markers.at(pair.first) - pair.second).norm();
          count++;
        }
      }
    }
  }
  std::cout << "Marker raw RMS: " << result << std::endl;
  std::cout << "Count: " << count << std::endl;

  result /= count;

  mSkeleton->setPositions(originalPoses);
  mSkeleton->setGroupScales(originalScales);

  return result;
}

//==============================================================================
// Get the average residual force, in newtons, and torque, in
// newton-meters
std::pair<s_t, s_t> DynamicsFitter::computeAverageResidualForce(
    std::shared_ptr<DynamicsInitialization> init)
{
  Eigen::VectorXs originalPoses = mSkeleton->getPositions();
  Eigen::VectorXs originalScales = mSkeleton->getGroupScales();

  mSkeleton->setGroupScales(init->groupScales);

  std::vector<int> footIndices;
  for (auto foot : mFootNodes)
  {
    footIndices.push_back(foot->getIndexInSkeleton());
  }
  ResidualForceHelper helper(mSkeleton, footIndices);

  s_t force = 0;
  s_t torque = 0;
  int count = 0;

  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    s_t dt = init->trialTimesteps[trial];
    for (int t = 0; t < init->poseTrials[trial].cols() - 2; t++)
    {
      if (init->probablyMissingGRF[trial][t])
      {
        continue;
      }
      Eigen::VectorXs q = init->poseTrials[trial].col(t);
      Eigen::VectorXs dq = (init->poseTrials[trial].col(t + 1)
                            - init->poseTrials[trial].col(t))
                           / dt;
      Eigen::VectorXs ddq = (init->poseTrials[trial].col(t + 2)
                             - 2 * init->poseTrials[trial].col(t + 1)
                             + init->poseTrials[trial].col(t))
                            / (dt * dt);
      Eigen::Vector6s residual
          = helper.calculateResidual(q, dq, ddq, init->grfTrials[trial].col(t));
      torque += residual.head<3>().norm();
      s_t frameForce = residual.tail<3>().norm();
      // std::cout << t << ": " << frameForce << "N" << std::endl;
      force += frameForce;
      count++;
    }
  }
  force /= count;
  torque /= count;

  mSkeleton->setPositions(originalPoses);
  mSkeleton->setGroupScales(originalScales);

  return std::make_pair(force, torque);
}

//==============================================================================
// Get the average real measured force (in newtons) and torque (in
// newton-meters)
std::pair<s_t, s_t> DynamicsFitter::computeAverageRealForce(
    std::shared_ptr<DynamicsInitialization> init)
{
  s_t force = 0;
  s_t torque = 0;
  int count = 0;

  for (int trial = 0; trial < init->poseTrials.size(); trial++)
  {
    for (int t = 0; t < init->poseTrials[trial].cols() - 2; t++)
    {
      for (int i = 0; i < init->forcePlateTrials[trial].size(); i++)
      {
        force += init->forcePlateTrials[trial][i].forces[t].norm();
        torque += init->forcePlateTrials[trial][i].moments[t].norm();
      }
      count++;
    }
  }
  force /= count;
  torque /= count;

  return std::make_pair(force, torque);
}

//==============================================================================
// This debugs the current state, along with visualizations of errors
// where the dynamics do not match the force plate data
void DynamicsFitter::saveDynamicsToGUI(
    const std::string& path,
    std::shared_ptr<DynamicsInitialization> init,
    int trialIndex,
    int framesPerSecond)
{
  std::string skeletonLayerName = "Skeleton";
  Eigen::Vector4s skeletonLayerColor = Eigen::Vector4s(0.7, 0.7, 0.7, 1.0);
  std::string originalSkeletonLayerName = "Original Skeleton";
  Eigen::Vector4s originalSkeletonLayerColor
      = Eigen::Vector4s(1.0, 0.3, 0.3, 0.3);
  std::string markerErrorLayerName = "Marker Error";
  Eigen::Vector4s markerErrorLayerColor = Eigen::Vector4s(1.0, 0.0, 0.0, 1.0);
  std::string forcePlateLayerName = "Force Plates";
  Eigen::Vector4s forcePlateLayerColor = Eigen::Vector4s(1.0, 0.0, 0.0, 1.0);
  std::string measuredForcesLayerName = "Measured Forces";
  Eigen::Vector4s measuredForcesLayerColor
      = Eigen::Vector4s(0.0, 0.0, 1.0, 1.0);
  std::string residualLayerName = "Residual Forces";
  Eigen::Vector4s residualLayerColor = Eigen::Vector4s(1.0, 0.0, 0.0, 1.0);
  std::string impliedForcesLayerName = "Implied Forces";
  Eigen::Vector4s impliedForcesLayerColor = Eigen::Vector4s(1.0, 0.0, 0.0, 1.0);
  std::string functionalJointCenterLayerName = "Functional Joint Centers";
  Eigen::Vector4s functionalJointCenterLayerColor
      = Eigen::Vector4s(0.0, 1.0, 0.0, 1.0);
  std::string groundLayerName = "Ground";
  Eigen::Vector4s groundLayerColor = Eigen::Vector4s(0.7, 0.7, 0.7, 1.0);
  std::string groundContactLayerName = "Ground Contact";
  Eigen::Vector4s groundContactLayerColor = Eigen::Vector4s(1.0, 1.0, 1.0, 0.5);
  Eigen::Vector4s groundContactActiveColor
      = Eigen::Vector4s(1.0, 0.5, 0.5, 0.5);

  if (trialIndex >= init->poseTrials.size())
  {
    std::cout << "Trying to visualize an out-of-bounds trialIndex: "
              << trialIndex << " >= " << init->poseTrials.size() << std::endl;
    exit(1);
  }

  Eigen::VectorXs originalMasses = mSkeleton->getLinkMasses();
  Eigen::VectorXs originalPoses = mSkeleton->getPositions();

  mSkeleton->setLinkMasses(init->bodyMasses);

  ///////////////////////////////////////////////////
  // Start actually rendering out results
  server::GUIRecording server;
  server.setFramesPerSecond(framesPerSecond);
  server.createLayer(skeletonLayerName, skeletonLayerColor);
  server.createLayer(originalSkeletonLayerName, originalSkeletonLayerColor);
  server.createLayer(markerErrorLayerName, markerErrorLayerColor);
  server.createLayer(forcePlateLayerName, forcePlateLayerColor);
  server.createLayer(measuredForcesLayerName, measuredForcesLayerColor);
  server.createLayer(residualLayerName, residualLayerColor);
  server.createLayer(impliedForcesLayerName, impliedForcesLayerColor);
  server.createLayer(
      functionalJointCenterLayerName, functionalJointCenterLayerColor, false);
  server.createLayer(groundLayerName, groundLayerColor);
  server.createLayer(groundContactLayerName, groundContactLayerColor, false);

  std::vector<ForcePlate> forcePlates = init->forcePlateTrials[trialIndex];
  Eigen::MatrixXs poses = init->poseTrials[trialIndex];

  for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
  {
    server.createSphere(
        "body_com_" + std::to_string(i),
        0.1 * mSkeleton->getBodyNode(i)->getMass() / mSkeleton->getMass(),
        Eigen::Vector3s::Zero(),
        Eigen::Vector4s(0, 0, 1, 0.5));
    server.setObjectTooltip(
        "body_com_" + std::to_string(i),
        mSkeleton->getBodyNode(i)->getName() + " Center of Mass");
  }

  if (init->flatGround[trialIndex])
  {
    server.createBox(
        "ground",
        Eigen::Vector3s(10, 0.2, 10),
        Eigen::Vector3s(0, init->groundHeight[trialIndex] - 0.1, 0),
        Eigen::Vector3s::Zero(),
        groundLayerColor,
        groundLayerName,
        false,
        true);
  }

  for (int i = 0; i < init->contactBodies.size(); i++)
  {
    for (int j = 0; j < init->contactBodies[i].size(); j++)
    {
      server.createSphere(
          "contact_sphere_" + std::to_string(i) + "_" + std::to_string(j),
          init->grfBodyContactSphereRadius[trialIndex][i][j],
          Eigen::Vector3s::Zero(),
          init->grfBodyOffForcePlate[trialIndex][0][i]
              ? groundContactActiveColor
              : groundContactLayerColor,
          groundContactLayerName);
    }
  }

  // Render the plates as red rectangles
  for (int i = 0; i < forcePlates.size(); i++)
  {
    if (forcePlates[i].corners.size() > 0)
    {
      std::vector<Eigen::Vector3s> points;
      for (int j = 0; j < forcePlates[i].corners.size(); j++)
      {
        points.push_back(forcePlates[i].corners[j]);
      }
      points.push_back(forcePlates[i].corners[0]);

      server.createLine(
          "plate_" + std::to_string(i),
          points,
          forcePlateLayerColor,
          forcePlateLayerName);
    }
  }

  if (init->defaultForcePlateCorners[trialIndex].size() > 0)
  {
    std::vector<Eigen::Vector3s> points;
    for (int j = 0; j < init->defaultForcePlateCorners[trialIndex].size(); j++)
    {
      points.push_back(init->defaultForcePlateCorners[trialIndex][j]);
    }
    points.push_back(init->defaultForcePlateCorners[trialIndex][0]);

    server.createLine(
        "default_plate", points, forcePlateLayerColor, forcePlateLayerName);
  }

  std::vector<bool> useForces;
  s_t threshold = 0.1;
  for (int timestep = 0; timestep < poses.cols(); timestep++)
  {
    bool anyForceData = false;
    for (int i = 0; i < forcePlates.size(); i++)
    {
      if (forcePlates[i].forces[timestep].norm() > threshold)
      {
        anyForceData = true;
        break;
      }
    }
    // Force ignore frames where we think there's a contact that we're not
    // measuring
    if (init->probablyMissingGRF[trialIndex][timestep])
    {
      anyForceData = false;
    }
    useForces.push_back(anyForceData);
  }

  ResidualForceHelper helper
      = ResidualForceHelper(mSkeleton, init->grfBodyIndices);

  std::vector<Eigen::Vector3s> residualForces;
  s_t residualNorm = 0.0;
  for (int timestep = 0; timestep < poses.cols() - 2; timestep++)
  {
    if (init->probablyMissingGRF[trialIndex][timestep])
    {
      continue;
    }
    s_t dt = init->trialTimesteps[trialIndex];
    Eigen::VectorXs q = poses.col(timestep);
    Eigen::VectorXs dq = (poses.col(timestep + 1) - poses.col(timestep)) / dt;
    Eigen::VectorXs ddq = (poses.col(timestep + 2) - 2 * poses.col(timestep + 1)
                           + poses.col(timestep))
                          / (dt * dt);
    mSkeleton->setPositions(q);
    mSkeleton->setVelocities(dq);
    mSkeleton->setAccelerations(ddq);

    Eigen::Vector6s residual = helper.calculateResidual(
        q, dq, ddq, init->grfTrials[trialIndex].col(timestep));
    residualForces.push_back(residual.tail<3>());
    residualNorm += residual.squaredNorm();
  }

  std::cout << "Residual norm: " << residualNorm << std::endl;

  std::vector<Eigen::Vector3s> coms = comPositions(init, trialIndex);
  std::vector<Eigen::Vector3s> impliedForces
      = impliedCOMForces(init, trialIndex, true);
  std::vector<Eigen::Vector3s> measuredForces
      = measuredGRFForces(init, trialIndex);

  for (int i = 0; i < impliedForces.size(); i++)
  {
    if (i % 1 == 0 && useForces[i])
    {
      std::vector<Eigen::Vector3s> impliedVector;
      impliedVector.push_back(coms[i]);
      impliedVector.push_back(coms[i] + (impliedForces[i] * 0.001));
      server.createLine(
          "com_implied_" + std::to_string(i),
          impliedVector,
          impliedForcesLayerColor,
          impliedForcesLayerName);

      std::vector<Eigen::Vector3s> measuredVector;
      measuredVector.push_back(coms[i]);
      measuredVector.push_back(coms[i] + (measuredForces[i] * 0.001));
      server.createLine(
          "com_measured_" + std::to_string(i),
          measuredVector,
          measuredForcesLayerColor,
          measuredForcesLayerName);

      std::vector<Eigen::Vector3s> residualVector;
      residualVector.push_back(coms[i] + (measuredForces[i] * 0.001));
      residualVector.push_back(
          coms[i] + (measuredForces[i] * 0.001) + (residualForces[i] * 0.001));
      server.createLine(
          "com_residual_" + std::to_string(i),
          residualVector,
          residualLayerColor,
          residualLayerName);
    }
  }

  std::shared_ptr<dynamics::Skeleton> originalSkeleton
      = mSkeleton->cloneSkeleton();
  originalSkeleton->setGroupScales(init->originalGroupScales);

  // Render the joints, if we have them
  int numJoints = init->jointCenters[trialIndex].rows() / 3;
  server.createLayer(
      functionalJointCenterLayerName, functionalJointCenterLayerColor, true);
  for (int i = 0; i < numJoints; i++)
  {
    if (init->jointWeights(i) > 0)
    {
      server.setObjectTooltip(
          "joint_center_" + std::to_string(i),
          "Joint center: " + init->joints[i]->getName());
      server.createSphere(
          "joint_center_" + std::to_string(i),
          0.01 * min(3.0, (1.0 / init->jointWeights(i))),
          Eigen::Vector3s::Zero(),
          Eigen::Vector4s(
              functionalJointCenterLayerColor(0),
              functionalJointCenterLayerColor(1),
              functionalJointCenterLayerColor(2),
              init->jointWeights(i)),
          functionalJointCenterLayerName);
    }
  }
  int numAxis = init->jointAxis[trialIndex].rows() / 6;
  for (int i = 0; i < numAxis; i++)
  {
    if (init->axisWeights(i) > 0)
    {
      server.createCapsule(
          "joint_axis_" + std::to_string(i),
          0.003 * min(3.0, (1.0 / init->axisWeights(i))),
          0.1,
          Eigen::Vector3s::Zero(),
          Eigen::Vector3s::Zero(),
          Eigen::Vector4s(
              functionalJointCenterLayerColor(0),
              functionalJointCenterLayerColor(1),
              functionalJointCenterLayerColor(2),
              init->axisWeights(i)),
          functionalJointCenterLayerName);
    }
  }

  for (int timestep = 0; timestep < poses.cols(); timestep++)
  {
    mSkeleton->setPositions(poses.col(timestep));
    server.renderSkeleton(
        mSkeleton, "skel", Eigen::Vector4s::Ones() * -1, skeletonLayerName);

    // Render foot-ground contact spheres
    for (int i = 0; i < init->contactBodies.size(); i++)
    {
      for (int j = 0; j < init->contactBodies[i].size(); j++)
      {
        server.setObjectPosition(
            "contact_sphere_" + std::to_string(i) + "_" + std::to_string(j),
            init->contactBodies[i][j]->getWorldTransform().translation());
        if (init->grfBodyOffForcePlate[trialIndex][timestep][i])
        {
          server.setObjectColor(
              "contact_sphere_" + std::to_string(i) + "_" + std::to_string(j),
              groundContactActiveColor);
        }
        else
        {
          server.setObjectColor(
              "contact_sphere_" + std::to_string(i) + "_" + std::to_string(j),
              groundContactLayerColor);
        }
      }
    }

    // Render COMs
    for (int i = 0; i < mSkeleton->getNumBodyNodes(); i++)
    {
      server.setObjectPosition(
          "body_com_" + std::to_string(i), mSkeleton->getBodyNode(i)->getCOM());
    }
    for (int i = 0; i < forcePlates.size(); i++)
    {
      server.deleteObject("force_" + std::to_string(i));
      if (forcePlates[i].forces[timestep].squaredNorm() > 0)
      {
        std::vector<Eigen::Vector3s> forcePoints;
        forcePoints.push_back(forcePlates[i].centersOfPressure[timestep]);
        forcePoints.push_back(
            forcePlates[i].centersOfPressure[timestep]
            + (forcePlates[i].forces[timestep] * 0.001));
        server.createLine(
            "force_" + std::to_string(i),
            forcePoints,
            forcePlateLayerColor,
            forcePlateLayerName);
      }
    }

    // Render Marker Errors
    auto simulatedMarkers
        = mSkeleton->getMarkerMapWorldPositions(init->updatedMarkerMap);
    auto realMarkers = init->markerObservationTrials[trialIndex][timestep];
    for (auto pair : simulatedMarkers)
    {
      if (realMarkers.count(pair.first))
      {
        std::vector<Eigen::Vector3s> points;
        points.push_back(pair.second);
        points.push_back(realMarkers.at(pair.first));
        server.createLine(
            "error_" + pair.first,
            points,
            markerErrorLayerColor,
            markerErrorLayerName);
      }
    }

    // Render virtual joints
    for (int i = 0; i < numJoints; i++)
    {
      if (init->jointWeights(i) > 0)
      {
        Eigen::Vector3s inferredJointCenter
            = init->jointCenters[trialIndex].block<3, 1>(i * 3, timestep);
        server.setObjectPosition(
            "joint_center_" + std::to_string(i), inferredJointCenter);
        if (i < init->jointsAdjacentMarkers.size())
        {
          for (std::string marker : init->jointsAdjacentMarkers[i])
          {
            if (init->markerObservationTrials[trialIndex][timestep].count(
                    marker))
            {
              std::vector<Eigen::Vector3s> centerToMarker;
              centerToMarker.push_back(inferredJointCenter);
              centerToMarker.push_back(
                  init->markerObservationTrials[trialIndex][timestep][marker]);
              server.createLine(
                  "joint_center_" + std::to_string(i) + "_to_marker_" + marker,
                  centerToMarker,
                  functionalJointCenterLayerColor,
                  functionalJointCenterLayerName);
            }
          }
        }
      }
    }
    for (int i = 0; i < numAxis; i++)
    {
      if (init->axisWeights(i) > 0)
      {
        // Render an axis capsule
        server.setObjectPosition(
            "joint_axis_" + std::to_string(i),
            init->jointAxis[trialIndex].block<3, 1>(i * 6, timestep));
        Eigen::Vector3s dir
            = init->jointAxis[trialIndex].block<3, 1>(i * 6 + 3, timestep);
        Eigen::Matrix3s R = Eigen::Matrix3s::Identity();
        R.col(2) = dir;
        R.col(1) = Eigen::Vector3s::UnitZ().cross(dir);
        R.col(0) = R.col(1).cross(R.col(2));
        server.setObjectRotation(
            "joint_axis_" + std::to_string(i), math::matrixToEulerXYZ(R));
      }
    }

    // Render Original Skeleton
    originalSkeleton->setPositions(
        init->originalPoses[trialIndex].col(timestep));
    server.renderSkeleton(
        originalSkeleton,
        "original_skel",
        originalSkeletonLayerColor,
        originalSkeletonLayerName);
    server.saveFrame();
  }

  mSkeleton->setPositions(originalPoses);
  mSkeleton->setLinkMasses(originalMasses);

  server.writeFramesJson(path);
}

//==============================================================================
void DynamicsFitter::setTolerance(double tol)
{
  mTolerance = tol;
}

//==============================================================================
void DynamicsFitter::setIterationLimit(int limit)
{
  mIterationLimit = limit;
}

//==============================================================================
void DynamicsFitter::setLBFGSHistoryLength(int len)
{
  mLBFGSHistoryLength = len;
}

//==============================================================================
void DynamicsFitter::setCheckDerivatives(bool check)
{
  mCheckDerivatives = check;
}

//==============================================================================
void DynamicsFitter::setPrintFrequency(int freq)
{
  mPrintFrequency = freq;
}

//==============================================================================
void DynamicsFitter::setSilenceOutput(bool silent)
{
  mSilenceOutput = silent;
}

//==============================================================================
void DynamicsFitter::setDisableLinesearch(bool disable)
{
  mDisableLinesearch = disable;
}

} // namespace biomechanics
} // namespace dart