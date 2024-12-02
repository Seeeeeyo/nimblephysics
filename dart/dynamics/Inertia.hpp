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

#ifndef DART_DYNAMICS_INERTIA_HPP_
#define DART_DYNAMICS_INERTIA_HPP_

#include <array>

#include "dart/math/MathTypes.hpp"

namespace dart {
namespace dynamics {

class Inertia
{
public:
  /// Enumeration for minimal inertia parameters
  enum Param
  {

    // Overall mass
    MASS = 0,

    // Center of mass components
    COM_X,
    COM_Y,
    COM_Z,

    // Moment of inertia components
    I_XX,
    I_YY,
    I_ZZ,
    I_XY,
    I_XZ,
    I_YZ

  };

  Inertia(
      s_t _mass = 1,
      const Eigen::Vector3s& _com = Eigen::Vector3s::Zero(),
      const Eigen::Matrix3s& _momentOfInertia = Eigen::Matrix3s::Identity());

  Inertia(const Eigen::Matrix6s& _spatialInertiaTensor);

  Inertia(
      s_t _mass,
      s_t _comX,
      s_t _comY,
      s_t _comZ,
      s_t _Ixx,
      s_t _Iyy,
      s_t _Izz,
      s_t _Ixy,
      s_t _Ixz,
      s_t _Iyz);

  /// Set an inertial parameter
  void setParameter(Param _param, s_t _value);

  /// Get an inertial parameter
  s_t getParameter(Param _param) const;

  /// Set the mass
  void setMass(s_t _mass, bool preserveDimsAndEuler = true);

  /// Get the mass
  s_t getMass() const;

  /// Set the mass bounds
  void setMassLowerBound(s_t _mass);
  s_t getMassLowerBound() const;
  void setMassUpperBound(s_t _mass);
  s_t getMassUpperBound() const;

  /// Set the center of mass with respect to the Body-fixed frame
  void setLocalCOM(const Eigen::Vector3s& _com);

  /// Get the center of mass with respect to the Body-fixed frame
  const Eigen::Vector3s& getLocalCOM() const;

  /// Get the center of mass bounds with respect to the Body-fixed frame
  void setLocalCOMLowerBound(Eigen::Vector3s bounds);
  const Eigen::Vector3s& getLocalCOMLowerBound() const;
  void setLocalCOMUpperBound(Eigen::Vector3s bounds);
  const Eigen::Vector3s& getLocalCOMUpperBound() const;

  /// Set the moment of inertia (about the center of mass). Note that only the
  /// top-right corner of the matrix will be used, because a well-formed inertia
  /// matrix is always symmetric.
  void setMoment(const Eigen::Matrix3s& _moment);

  /// Set the moment of inertia (about the center of mass)
  void setMoment(s_t _Ixx, s_t _Iyy, s_t _Izz, s_t _Ixy, s_t _Ixz, s_t _Iyz);

  /// Get the moment of inertia
  Eigen::Matrix3s getMoment() const;

  /// Set the moment of inertia (about the center of mass)
  void setMomentVector(Eigen::Vector6s moment);
  const Eigen::Vector6s getMomentVector() const;
  /// Set the moment of inertia bounds (about the center of mass)
  void setMomentLowerBound(Eigen::Vector6s bound);
  const Eigen::Vector6s& getMomentLowerBound() const;
  void setMomentUpperBound(Eigen::Vector6s bound);
  const Eigen::Vector6s& getMomentUpperBound() const;

  /// Set the dims and eulers (about the center of mass)
  void setDimsAndEulerVector(Eigen::Vector6s dimsAndEuler);
  const Eigen::Vector6s getDimsAndEulerVector() const;
  /// Set the dims and eulers bounds (about the center of mass)
  void setDimsAndEulerLowerBound(Eigen::Vector6s bound);
  const Eigen::Vector6s& getDimsAndEulerLowerBound() const;
  void setDimsAndEulerUpperBound(Eigen::Vector6s bound);
  const Eigen::Vector6s& getDimsAndEulerUpperBound() const;

  /// Set the spatial tensor
  void setSpatialTensor(const Eigen::Matrix6s& _spatial);

  /// Get the spatial inertia tensor
  const Eigen::Matrix6s& getSpatialTensor() const;

  /// Returns true iff _moment is a physically valid moment of inertia
  static bool verifyMoment(
      const Eigen::Matrix3s& _moment,
      bool _printWarnings = true,
      s_t _tolerance = 1e-8);

  /// Returns true iff _spatial is a physically valid spatial inertia tensor
  static bool verifySpatialTensor(
      const Eigen::Matrix6s& _spatial,
      bool _printWarnings = true,
      s_t _tolerance = 1e-8);

  /// Returns true iff this Inertia object is physically valid
  bool verify(bool _printWarnings = true, s_t _tolerance = 1e-8) const;

  /// Check for equality
  bool operator==(const Inertia& other) const;

  /// This rescales the object by "ratio" in each of the specified axis
  void rescale(Eigen::Vector3s ratio);

  /// This gets the gradient of the spatial tensor with respect to the mass
  Eigen::Matrix6s getSpatialTensorGradientWrtMass(
      bool preserveDimsAndEuler = true);

  /// This gets the gradient of the spatial tensor with respect to the mass
  Eigen::Matrix6s finiteDifferenceSpatialTensorGradientWrtMass(
      bool preserveDimsAndEuler = true);

  /// This gets the gradient of the spatial tensor with respect to a specific
  /// index in the COM vector
  Eigen::Matrix6s getSpatialTensorGradientWrtCOM(int index);

  /// This gets the gradient of the spatial tensor with respect to a specific
  /// index in the COM vector
  Eigen::Matrix6s finiteDifferenceSpatialTensorGradientWrtCOM(int index);

  /// This gets the gradient of the spatial tensor with respect to a specific
  /// index in the moment vector
  Eigen::Matrix6s getSpatialTensorGradientWrtMomentVector(int index);

  /// This gets the gradient of the spatial tensor with respect to a specific
  /// index in the moment vector
  Eigen::Matrix6s finiteDifferenceSpatialTensorGradientWrtMomentVector(
      int index);

  /// This gets the gradient of the spatial tensor with respect to a specific
  /// index in the moment vector
  Eigen::Matrix6s getSpatialTensorGradientWrtDimsAndEulerVector(int index);

  /// This gets the gradient of the spatial tensor with respect to a specific
  /// index in the moment vector
  Eigen::Matrix6s finiteDifferenceSpatialTensorGradientWrtDimsAndEulerVector(
      int index);

  /// This creates a copy of this inertia object
  Inertia clone() const;

  /// This computes the moment vector from a the mass, and a concatenated vector
  /// for the dimensions of a cube, and the euler angles by which to rotate the
  /// cube.
  ///
  /// Notes: We choose euler angles, instead of SO3, because the gradients are
  /// smoother in the very small rotation values, which is where we expect
  /// optimizers to spend most of their time.
  static Eigen::Vector6s computeMomentVector(
      s_t mass, Eigen::Vector6s dimsAndEuler);

  static Eigen::Matrix6s computeMomentVectorJacWrtDimsAndEuler(
      s_t mass, Eigen::Vector6s dimsAndEuler);

  static Eigen::Matrix6s finiteDifferenceMomentVectorJacWrtDimsAndEuler(
      s_t mass, Eigen::Vector6s dimsAndEuler);

  static Eigen::Vector6s computeMomentVectorGradWrtMass(
      s_t mass, Eigen::Vector6s dimsAndEuler);

  static Eigen::Vector6s finiteDifferenceMomentVectorGradWrtMass(
      s_t mass, Eigen::Vector6s dimsAndEuler);

  /// This reverses computeMomentVector(), to get into a more interpretable
  /// space of cube dimensions and rotations. This can then be used to visualize
  /// inertia in a GUI.
  static Eigen::Vector6s computeDimsAndEuler(
      s_t mass, Eigen::Vector6s momentVector);

  /// This creates the inertia for a rectangular prism, from the original
  /// formula.
  static Inertia createCubeInertia(s_t mass, Eigen::Vector3s dims);

  /// This computes the size of a rectangular prism, ignoring the off-diagonal
  /// inertia properties.
  Eigen::Vector3s getImpliedCubeDimensions() const;

  /// This gives a 3x6 Jacobian that relates changes in the moment vector to
  /// changes in the implied dimensions.
  Eigen::Matrix<s_t, 3, 6> getImpliedCubeDimensionsJacobianWrtMomentVector()
      const;

  /// This gives a 3x6 Jacobian that relates changes in the moment vector to
  /// changes in the implied dimensions.
  Eigen::Matrix<s_t, 3, 6>
  finiteDifferenceImpliedCubeDimensionsJacobianWrtMomentVector();

  /// This gets the gradient of implied dimensions wrt the mass
  Eigen::Vector3s getImpliedCubeDimensionsGradientWrtMass() const;

  /// This gets the gradient of implied dimensions wrt the mass
  Eigen::Vector3s finiteDifferenceImpliedCubeDimensionsGradientWrtMass();

  /// This gets the implied rectangular prism density. This is a useful
  /// constraint
  s_t getImpliedCubeDensity() const;

  /// This gets the gradient of implied density wrt the moment vector
  Eigen::Vector6s getImpliedCubeDensityGradientWrtMomentVector() const;

  /// This gets the gradient of implied density wrt the moment vector
  Eigen::Vector6s finiteDifferenceImpliedCubeDensityGradientWrtMomentVector();

  /// This gets the gradient of implied density wrt the mass
  s_t getImpliedCubeDensityGradientWrtMass() const;

  /// This gets the gradient of implied density wrt the mass
  s_t finiteDifferenceImpliedCubeDensityGradientWrtMass();

protected:
  /// Compute the spatial tensor based on the inertial parameters
  void computeSpatialTensor();

  /// Compute the inertial parameters from the spatial tensor
  void computeParameters();

  /// Overall mass
  s_t mMass;
  s_t mMassLowerBound;
  s_t mMassUpperBound;

  /// Center of mass in the Body frame
  Eigen::Vector3s mCenterOfMass;
  Eigen::Vector3s mCenterOfMassLowerBound;
  Eigen::Vector3s mCenterOfMassUpperBound;

  /// The six parameters of the moment of inertia located at the center of mass
  std::array<s_t, 6> mMoment;
  Eigen::Vector6s mMomentLowerBound;
  Eigen::Vector6s mMomentUpperBound;

  bool mCachedDimsAndEulerDirty;
  Eigen::Vector6s mCachedDimsAndEuler;
  Eigen::Vector6s mDimsAndEulerLowerBound;
  Eigen::Vector6s mDimsAndEulerUpperBound;

  /// Cache for generalized spatial inertia of the Body
  Eigen::Matrix6s mSpatialTensor;

public:
  // To get byte-aligned Eigen vectors
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

} // namespace dynamics
} // namespace dart

#endif // DART_DYNAMICS_INERTIA_HPP_
